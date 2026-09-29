#include "ubake/TextureCache.h"

#include "core/FileSystem.h"
#include "core/Sha256.h"
#include "ubake/Name.h"
#include "ubundle/Bundle.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace uta::ubake {
namespace {

/// Bumped when the file's own layout changes. What the material holds is
/// bakerVersion()'s, inside the key.
constexpr std::uint32_t FILE_VERSION = 1;
constexpr std::array<char, 4> MAGIC = {'U', 'T', 'A', 'T'};

// Every field umat::generate reads must enter the key, or a changed setting is
// served an old material. A new MaterialSettings field fails this first.
static_assert(sizeof(umat::MaterialSettings) == 12,
              "a MaterialSettings field was added or changed: put it in textureCacheKey");

void addU32(Sha256& hash, std::uint32_t value) {
    std::array<std::byte, 4> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::byte>(value >> (8 * i));
    hash.add(bytes);
}

void addText(Sha256& hash, std::string_view text) {
    addU32(hash, static_cast<std::uint32_t>(text.size()));
    hash.add(std::as_bytes(std::span(text.data(), text.size())));
}

/// Little-endian writer and bounds-checked reader for the file below.
struct Writer {
    std::vector<std::byte> bytes;
    void u8(std::uint8_t value) { bytes.push_back(static_cast<std::byte>(value)); }
    void u16(std::uint16_t value) {
        u8(static_cast<std::uint8_t>(value));
        u8(static_cast<std::uint8_t>(value >> 8));
    }
    void u32(std::uint32_t value) {
        for (int i = 0; i < 4; ++i) u8(static_cast<std::uint8_t>(value >> (8 * i)));
    }
    void raw(std::span<const std::byte> data) { bytes.insert(bytes.end(), data.begin(), data.end()); }
};

struct Reader {
    std::span<const std::byte> bytes;
    std::size_t at = 0;
    bool ok = true;
    std::span<const std::byte> raw(std::size_t count) {
        if (!ok || bytes.size() - at < count) {
            ok = false;
            return {};
        }
        const auto out = bytes.subspan(at, count);
        at += count;
        return out;
    }
    std::uint32_t le(std::size_t count) {
        const auto data = raw(count);
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < data.size(); ++i) value |= std::to_integer<std::uint32_t>(data[i]) << (8 * i);
        return value;
    }
    std::uint8_t u8() { return static_cast<std::uint8_t>(le(1)); }
    std::uint16_t u16() { return static_cast<std::uint16_t>(le(2)); }
    std::uint32_t u32() { return le(4); }
    std::string text() {
        const auto data = raw(u32());
        std::string out(data.size(), '\0');
        if (!data.empty()) std::memcpy(out.data(), data.data(), data.size());
        return out;
    }
};

void writeText(Writer& out, std::string_view text) {
    out.u32(static_cast<std::uint32_t>(text.size()));
    out.raw(std::as_bytes(std::span(text.data(), text.size())));
}

std::vector<std::byte> encode(const TextureCacheKey& key, const umat::Material& material) {
    Writer out;
    out.raw(std::as_bytes(std::span(MAGIC)));
    out.u32(FILE_VERSION);
    out.raw(key);
    writeText(out, material.id);
    out.u8(material.metallic ? 1 : 0);
    out.u8(material.parallaxDepth);
    out.u32(static_cast<std::uint32_t>(material.maps.size()));
    for (const ubundle::CompressedTexture& map : material.maps) {
        writeText(out, map.name);
        out.u8(static_cast<std::uint8_t>(map.format));
        out.u16(map.width);
        out.u16(map.height);
        out.u16(map.sourceWidth);
        out.u16(map.sourceHeight);
        out.u8(map.mipCount);
        out.u32(static_cast<std::uint32_t>(map.blocks.size()));
        out.raw(map.blocks);
    }
    return std::move(out.bytes);
}

/// Nothing unless every byte reads, the key and id match, and each map's
/// blocks are the size its fields require.
std::optional<umat::Material> decode(std::span<const std::byte> bytes, const TextureCacheKey& key,
                                     std::string_view id) {
    Reader in{bytes};
    const auto magic = in.raw(MAGIC.size());
    if (!in.ok || std::memcmp(magic.data(), MAGIC.data(), MAGIC.size()) != 0) return std::nullopt;
    if (in.u32() != FILE_VERSION) return std::nullopt;
    const auto stored = in.raw(key.size());
    if (!in.ok || !std::equal(stored.begin(), stored.end(), key.begin())) return std::nullopt;
    umat::Material material;
    material.id = in.text();
    if (!in.ok || material.id != id) return std::nullopt;
    const std::uint8_t metallic = in.u8();
    if (metallic > 1) return std::nullopt;
    material.metallic = metallic == 1;
    material.parallaxDepth = in.u8();
    const std::uint32_t count = in.u32();
    for (std::uint32_t i = 0; in.ok && i < count; ++i) {
        ubundle::CompressedTexture map;
        map.name = in.text();
        const std::uint8_t format = in.u8();
        if (format > static_cast<std::uint8_t>(ubundle::BlockFormat::BC7)) return std::nullopt;
        map.format = static_cast<ubundle::BlockFormat>(format);
        map.width = in.u16();
        map.height = in.u16();
        map.sourceWidth = in.u16();
        map.sourceHeight = in.u16();
        map.mipCount = in.u8();
        const auto blocks = in.raw(in.u32());
        if (!in.ok) return std::nullopt;
        map.blocks.assign(blocks.begin(), blocks.end());
        const std::uint64_t expected = ubundle::expectedBlockBytes(map);
        if (expected == 0 || expected != map.blocks.size()) return std::nullopt;
        material.maps.push_back(std::move(map));
    }
    if (!in.ok || in.at != bytes.size()) return std::nullopt;
    return material;
}

std::string hex(std::span<const std::byte> bytes) {
    static constexpr char DIGITS[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::byte b : bytes) {
        out.push_back(DIGITS[std::to_integer<unsigned>(b) >> 4]);
        out.push_back(DIGITS[std::to_integer<unsigned>(b) & 0xF]);
    }
    return out;
}

} // namespace

TextureCacheKey textureCacheKey(std::string_view id, const umat::Image& base,
                                const umat::MaterialSettings& settings) {
    Sha256 hash;
    addText(hash, "ut-ants texture cache");
    addText(hash, bakerVersion());
    addText(hash, id);
    addU32(hash, base.width);
    addU32(hash, base.height);
    addU32(hash, base.channels);
    addU32(hash, static_cast<std::uint32_t>(base.pixels.size()));
    hash.add(base.pixels);
    addU32(hash, settings.requestedUpscale);
    addU32(hash, settings.metallic ? 1u : 0u);
    addU32(hash, settings.baseRoughness);
    addU32(hash, settings.emissive ? 1u : 0u);
    addU32(hash, settings.emissiveThreshold);
    addU32(hash, settings.parallaxDepth);
    return hash.finish();
}

TextureCache::TextureCache(std::filesystem::path directory, std::uint64_t capBytes)
    : directory_(std::move(directory)), capBytes_(capBytes) {}

std::filesystem::path TextureCache::pathOf(const TextureCacheKey& key) const {
    const std::string name = hex(key);
    // Two hex digits of fan-out keep any one directory to a few hundred files.
    return directory_ / name.substr(0, 2) / (name + ".tex");
}

std::optional<umat::Material> TextureCache::find(const TextureCacheKey& key, std::string_view id) {
    const std::filesystem::path path = pathOf(key);
    const auto bytes = fs::readFile(path);
    std::optional<umat::Material> material;
    if (bytes.has_value()) material = decode(*bytes, key, id);
    if (!material.has_value()) {
        ++misses_;
        return std::nullopt;
    }
    std::error_code ignored;
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ignored);
    ++hits_;
    return material;
}

void TextureCache::store(const TextureCacheKey& key, const umat::Material& material) {
    const std::filesystem::path path = pathOf(key);
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);
    const std::vector<std::byte> bytes = encode(key, material);
    if (!fs::writeFileAtomically(path, bytes).has_value()) return; // best effort, as above
}

void TextureCache::trim() {
    struct Entry {
        std::filesystem::path path;
        std::filesystem::file_time_type used;
        std::uint64_t size = 0;
    };
    std::vector<Entry> entries;
    std::uint64_t total = 0;
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(directory_, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
        if (!it->is_regular_file(error) || it->path().extension() != ".tex") continue;
        Entry entry{it->path(), it->last_write_time(error), it->file_size(error)};
        if (error) {
            error.clear();
            continue;
        }
        total += entry.size;
        entries.push_back(std::move(entry));
    }
    if (total <= capBytes_) return;
    std::ranges::sort(entries, [](const Entry& a, const Entry& b) { return a.used < b.used; });
    for (const Entry& entry : entries) {
        if (total <= capBytes_) break;
        std::error_code ignored;
        std::filesystem::remove(entry.path, ignored);
        total -= entry.size;
    }
}

} // namespace uta::ubake
