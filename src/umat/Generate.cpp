// umat -- see Generate.h.

#include "umat/Generate.h"

#include "core/Ascii.h"
#include "umat/Derive.h"
#include "umat/Enlarge.h"

#include <array>
#include <bit>
#include <cstddef>
#include <utility>

namespace uta::umat {

namespace {

struct MapSpec {
    MapKind kind;
    const char* suffix;
    ubundle::BlockFormat format;
};

/// MapKind order and UTA-0052 SS 4.2's formats (UTA-0009 INV-8). `chains` in
/// generate() pairs with this by position.
constexpr std::array<MapSpec, 5> MAPS{{
    {MapKind::Base, "base", ubundle::BlockFormat::BC7},
    {MapKind::Normal, "normal", ubundle::BlockFormat::BC5},
    {MapKind::Rough, "rough", ubundle::BlockFormat::BC4},
    {MapKind::Height, "height", ubundle::BlockFormat::BC4},
    {MapKind::Emit, "emit", ubundle::BlockFormat::BC7},
}};

} // namespace

std::string materialId(std::string_view package, std::string_view path, bool masked) {
    std::string id;
    id += asciiFolded(package);
    id.push_back('.');
    id += asciiFolded(path);
    if (masked) id += "#masked";
    return id;
}

Result<Material> generate(std::string id, const Image& base, const MaterialSettings& settings,
                          JobSystem& jobs) {
    const auto code = ErrorCode::InvalidArgument;
    if (base.channels != 4)
        return fail(code, "generate: '" + id + "' has " + std::to_string(base.channels)
                              + " channels; a base is RGBA");
    if (!std::has_single_bit(base.width) || base.width > ubundle::MAX_TEXTURE_EDGE
        || !std::has_single_bit(base.height) || base.height > ubundle::MAX_TEXTURE_EDGE)
        return fail(code, "generate: '" + id + "' is " + std::to_string(base.width) + "x"
                              + std::to_string(base.height) + ", not a power of two in [1, "
                              + std::to_string(ubundle::MAX_TEXTURE_EDGE) + "] in both axes");
    if (base.pixels.size() != std::size_t{base.width} * base.height * 4)
        return fail(code, "generate: '" + id + "' holds " + std::to_string(base.pixels.size())
                              + " bytes, which its dimensions do not");

    const std::uint32_t factor = upscaleFactor(base.width, base.height, settings.requestedUpscale);
    UTA_TRY(const Image enlarged, enlarge(base, factor, jobs));

    // SS 4.5: base colour and height are box-averaged down the chain; normal,
    // roughness and emissive are derived per level, never averaged.
    const std::vector<Image> colour = mipChain(enlarged);
    const std::vector<Image> height = mipChain(heightOf(enlarged));
    std::vector<Image> normal;
    std::vector<Image> rough;
    std::vector<Image> emit;
    for (std::size_t l = 0; l < height.size(); ++l) {
        normal.push_back(normalOf(height[l], factor, static_cast<std::uint32_t>(l)));
        rough.push_back(roughnessOf(height[l], settings.baseRoughness));
        if (settings.emissive)
            emit.push_back(emissiveOf(colour[l], height[l], settings.emissiveThreshold));
    }
    // In MAPS order, which the static_assert pins to MapKind's.
    static_assert(MAPS[0].kind == MapKind::Base && MAPS[1].kind == MapKind::Normal && MAPS[2].kind == MapKind::Rough
                  && MAPS[3].kind == MapKind::Height && MAPS[4].kind == MapKind::Emit);
    const std::array<const std::vector<Image>*, MAPS.size()> chains{&colour, &normal, &rough,
                                                                    &height, &emit};

    Material material;
    material.id = std::move(id);
    material.metallic = settings.metallic;
    material.parallaxDepth = settings.parallaxDepth;
    for (std::size_t m = 0; m < MAPS.size(); ++m) {
        if (MAPS[m].kind == MapKind::Emit && !settings.emissive) continue;
        UTA_TRY(ubundle::CompressedTexture map,
                compress(material.id + ":" + MAPS[m].suffix, *chains[m], MAPS[m].format,
                         static_cast<std::uint16_t>(base.width),
                         static_cast<std::uint16_t>(base.height), jobs));
        material.maps.push_back(std::move(map));
    }
    return material;
}

} // namespace uta::umat
