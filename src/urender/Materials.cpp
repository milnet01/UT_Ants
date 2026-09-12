// A bundle's materials on the GPU -- Materials.h.

#include "urender/Materials.h"

#include "core/Log.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <map>
#include <span>
#include <string_view>

namespace uta::urender {

namespace {

/// One texture about to be uploaded: its image description and its bytes,
/// every mip level one after another.
struct Source {
    ImageDesc desc;
    std::span<const std::byte> bytes;
    std::vector<VkDeviceSize> levelSizes;
};

/// The built-in maps, one texel each and uncompressed. Base is magenta, the
/// conventional colour of something missing; normal is flat; roughness is
/// mid; height is zero.
constexpr std::array<std::byte, 4> DEFAULT_BASE{std::byte{255}, std::byte{0}, std::byte{255}, std::byte{255}};
constexpr std::array<std::byte, 4> DEFAULT_NORMAL{std::byte{128}, std::byte{128}, std::byte{255}, std::byte{255}};
constexpr std::array<std::byte, 4> DEFAULT_ROUGH{std::byte{128}, std::byte{128}, std::byte{128}, std::byte{255}};
constexpr std::array<std::byte, 4> DEFAULT_HEIGHT{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255}};

constexpr VkImageUsageFlags SAMPLED = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

Source defaultSource(std::span<const std::byte, 4> texel, VkFormat format) {
    Source source;
    source.desc = {format, 1, 1, 1, SAMPLED};
    source.bytes = texel;
    source.levelSizes = {4};
    return source;
}

/// SS 4.5's table: colour maps through an _SRGB format, so the sampler
/// returns linear (SS 4.10); everything else UNORM.
VkFormat formatOf(ubundle::BlockFormat format, bool colour) noexcept {
    switch (format) {
    case ubundle::BlockFormat::BC4: return VK_FORMAT_BC4_UNORM_BLOCK;
    case ubundle::BlockFormat::BC5: return VK_FORMAT_BC5_UNORM_BLOCK;
    default: return colour ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK;
    }
}

Result<Source> compressedSource(const ubundle::CompressedTexture& texture, bool colour) {
    if (ubundle::expectedBlockBytes(texture) != texture.blocks.size() || texture.width == 0 || texture.height == 0) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("TEXS entry '{}' holds {} bytes, which is not what its {}x{} and {} mip levels need",
                                texture.name, texture.blocks.size(), texture.width, texture.height,
                                texture.mipCount));
    }
    Source source;
    source.desc = {formatOf(texture.format, colour), texture.width, texture.height, texture.mipCount, SAMPLED};
    source.bytes = texture.blocks;
    for (std::uint32_t level = 0; level < texture.mipCount; ++level) {
        const std::uint64_t w = std::max<std::uint32_t>(1u, texture.width >> level);
        const std::uint64_t h = std::max<std::uint32_t>(1u, texture.height >> level);
        source.levelSizes.push_back((w + 3) / 4 * ((h + 3) / 4) * ubundle::bytesPerBlock(texture.format));
    }
    return source;
}

} // namespace

Result<MaterialSet> MaterialSet::upload(Gpu& gpu, const ubundle::Bundle& bundle) {
    MaterialSet set;

    std::vector<Source> sources;
    sources.push_back(defaultSource(DEFAULT_BASE, VK_FORMAT_R8G8B8A8_SRGB));
    sources.push_back(defaultSource(DEFAULT_NORMAL, VK_FORMAT_R8G8B8A8_UNORM));
    sources.push_back(defaultSource(DEFAULT_ROUGH, VK_FORMAT_R8G8B8A8_UNORM));
    sources.push_back(defaultSource(DEFAULT_HEIGHT, VK_FORMAT_R8G8B8A8_UNORM));
    const gpu::Material defaults{0, 1, 2, 3, gpu::NONE, 0};

    std::unordered_map<std::string_view, const ubundle::CompressedTexture*> byName;
    if (bundle.textures)
        for (const ubundle::CompressedTexture& texture : *bundle.textures) byName.emplace(texture.name, &texture);

    std::vector<gpu::Material> records{defaults};
    std::unordered_map<std::string_view, std::uint32_t> uploaded; // TEXS name -> texture index
    const auto mapIndex = [&](const std::string& id, std::string_view map, bool colour)
        -> Result<std::uint32_t> {
        const std::string name = std::format("{}:{}", id, map);
        const auto found = byName.find(name);
        if (found == byName.end()) return gpu::NONE;
        if (const auto done = uploaded.find(found->first); done != uploaded.end()) return done->second;
        UTA_TRY(Source source, compressedSource(*found->second, colour));
        const auto index = static_cast<std::uint32_t>(sources.size());
        sources.push_back(std::move(source));
        uploaded.emplace(found->first, index);
        return index;
    };

    if (bundle.materials) {
        for (const ubundle::MaterialRecord& record : *bundle.materials) {
            UTA_TRY(const std::uint32_t base, mapIndex(record.id, "base", true));
            UTA_TRY(const std::uint32_t normal, mapIndex(record.id, "normal", false));
            UTA_TRY(const std::uint32_t rough, mapIndex(record.id, "rough", false));
            UTA_TRY(const std::uint32_t height, mapIndex(record.id, "height", false));
            UTA_TRY(const std::uint32_t emit, mapIndex(record.id, "emit", true));
            if (base == gpu::NONE && normal == gpu::NONE && rough == gpu::NONE && height == gpu::NONE) {
                // SS 6: ubundle does not check the pairing, so the renderer must.
                UTA_LOG(logRender, LogLevel::Warning,
                        "material '{}' has no TEXS maps; the default material stands in", record.id);
                set.byId_.emplace(record.id, DEFAULT_MATERIAL);
                set.reported_.insert(record.id);
                continue;
            }
            set.byId_.emplace(record.id, static_cast<std::uint32_t>(records.size()));
            records.push_back({base == gpu::NONE ? defaults.base : base,
                               normal == gpu::NONE ? defaults.normal : normal,
                               rough == gpu::NONE ? defaults.rough : rough,
                               height == gpu::NONE ? defaults.height : height, emit,
                               record.metallic ? 1u : 0u});
        }
    }

    // -- Images, bound into one block per memory type -------------------------
    struct Placement {
        std::uint32_t type;
        VkDeviceSize offset;
    };
    std::vector<Placement> placements;
    std::map<std::uint32_t, VkDeviceSize> blockSizes;
    set.textures_.reserve(sources.size());
    for (const Source& source : sources) {
        UTA_TRY(Image image, Image::createUnbound(gpu, source.desc));
        VkMemoryRequirements needs{};
        vkGetImageMemoryRequirements(gpu.device(), image.handle(), &needs);
        UTA_TRY(const std::uint32_t type, gpu.memoryType(needs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
        VkDeviceSize& end = blockSizes[type];
        const VkDeviceSize offset = (end + needs.alignment - 1) / needs.alignment * needs.alignment;
        end = offset + needs.size;
        placements.push_back({type, offset});
        set.textures_.push_back(std::move(image));
    }
    std::map<std::uint32_t, VkDeviceMemory> blockOf;
    for (const auto& [type, size] : blockSizes) {
        UTA_TRY(MemoryBlock block, MemoryBlock::allocate(gpu, size, type));
        blockOf[type] = block.handle();
        set.blocks_.push_back(std::move(block));
    }
    for (std::size_t i = 0; i < sources.size(); ++i)
        UTA_CHECK(set.textures_[i].bind(blockOf[placements[i].type], placements[i].offset));

    // -- Their contents, through one staging buffer ---------------------------
    VkDeviceSize total = 0;
    for (const Source& source : sources) total += source.bytes.size();
    UTA_TRY(Buffer staging, Buffer::create(gpu, total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true));
    std::vector<VkDeviceSize> starts;
    VkDeviceSize cursor = 0;
    for (const Source& source : sources) {
        std::memcpy(staging.mapped() + cursor, source.bytes.data(), source.bytes.size());
        starts.push_back(cursor);
        cursor += source.bytes.size();
    }
    UTA_CHECK(gpu.run([&](VkCommandBuffer commands) {
        for (std::size_t i = 0; i < sources.size(); ++i) {
            Image& image = set.textures_[i];
            image.transition(commands, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkDeviceSize offset = starts[i];
            std::vector<VkBufferImageCopy> copies;
            for (std::uint32_t level = 0; level < sources[i].desc.mipLevels; ++level) {
                VkBufferImageCopy copy{};
                copy.bufferOffset = offset;
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
                copy.imageExtent = {std::max(1u, sources[i].desc.width >> level),
                                    std::max(1u, sources[i].desc.height >> level), 1};
                copies.push_back(copy);
                offset += sources[i].levelSizes[level];
            }
            vkCmdCopyBufferToImage(commands, staging.handle(), image.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   static_cast<std::uint32_t>(copies.size()), copies.data());
            image.transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }));

    UTA_TRY(set.records_, Buffer::upload(gpu, std::as_bytes(std::span(records)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    return set;
}

std::uint32_t MaterialSet::indexOf(const std::string& id) {
    if (id.empty()) return DEFAULT_MATERIAL;
    if (const auto found = byId_.find(id); found != byId_.end()) return found->second;
    if (reported_.insert(id).second)
        UTA_LOG(logRender, LogLevel::Warning, "a batch names material '{}', which MATS does not carry; the default "
                                              "material stands in",
                id);
    return DEFAULT_MATERIAL;
}

} // namespace uta::urender
