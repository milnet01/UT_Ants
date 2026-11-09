// The materials' staging buffer -- review-code 2026-10-15 (UTA-0365).
// vkCmdCopyBufferToImage needs each bufferOffset to be a multiple of the
// format's block size, 16 bytes for BC5 and BC7. A BC4 map with a full mip
// chain is 8 bytes past a multiple of 16, so a plain running total started
// the next BC7 map misaligned.

#include "urender/Materials.h"

#include "core/Jobs.h"
#include "ubundle/Bundle.h"
#include "umat/Derive.h"
#include "umat/Material.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

TEST_CASE("UTA-0365: every texture starts its staging run on a 16-byte boundary", "[urender][materials]") {
    // A BC4 4x4 map with its 2x2 and 1x1 levels is three 8-byte blocks, then
    // a BC7 map, then an RGBA8 fire picture of 3x1 texels, then one more.
    const std::vector<std::size_t> sizes{24, 16, 12, 16};
    const uta::urender::StagingLayout layout = uta::urender::stagingLayout(sizes);
    REQUIRE(layout.starts.size() == sizes.size());
    CHECK(layout.starts == std::vector<VkDeviceSize>{0, 32, 48, 64});
    CHECK(layout.total == 80);
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        CAPTURE(i);
        CHECK(layout.starts[i] % 16 == 0);
        if (i + 1 < sizes.size()) CHECK(layout.starts[i] + sizes[i] <= layout.starts[i + 1]);
    }
}

// UTA-0451: which materials draw with highlights. A roughness map at umat's
// default must read as plain and a shinier one as glossy, from the encoder's
// own blocks, so the cut is tested on what a bake writes.
namespace {

uta::ubundle::CompressedTexture roughMap(std::uint8_t baseRoughness) {
    // A height ramp over every value, so the map spans baseRoughness's whole
    // range under umat::roughnessOf.
    uta::umat::Image height{.width = 16, .height = 16, .channels = 1, .pixels = {}};
    for (std::uint32_t i = 0; i < 256; ++i) height.pixels.push_back(static_cast<std::byte>(i));
    const uta::umat::Image rough = uta::umat::roughnessOf(height, baseRoughness);
    uta::JobSystem jobs(1);
    auto compressed = uta::umat::compress("t:rough", std::span<const uta::umat::Image>(&rough, 1),
                                          uta::ubundle::BlockFormat::BC4, 16, 16, jobs);
    REQUIRE(compressed.has_value());
    return *std::move(compressed);
}

} // namespace

TEST_CASE("UTA-0451: a roughness map at umat's default reads as plain, near its true floor",
          "[urender][materials]") {
    // roughnessOf: 191 + (128 - h) / 4 over h in 0..255 reaches 159 at its lowest.
    // The encoder moves a value by a few, so the floor sits near 159, not on it.
    const int floor = uta::urender::bc4Floor(roughMap(191));
    CHECK(floor >= int{uta::urender::GLOSSY_BELOW});
    CHECK(floor <= 159 + 4);
}

TEST_CASE("UTA-0451: a shinier roughness map reads as glossy", "[urender][materials]") {
    // 120 + (128 - h) / 4 reaches 88 at its lowest and 152 at its highest.
    const int floor = uta::urender::bc4Floor(roughMap(120));
    CHECK(floor < int{uta::urender::GLOSSY_BELOW});
    CHECK(floor <= 88 + 4);
}

TEST_CASE("UTA-0451: a BC4 block that decodes a texel to 0 has floor 0", "[urender][materials]") {
    // red0 <= red1 selects the six-value mode, where index 6 is 0. Texel 0
    // takes it; the endpoints alone would say 200.
    uta::ubundle::CompressedTexture texture{.name = "t:rough", .format = uta::ubundle::BlockFormat::BC4,
                                            .width = 4, .height = 4, .sourceWidth = 4, .sourceHeight = 4,
                                            .mipCount = 1, .blocks = {}};
    for (const unsigned value : {200u, 220u, 6u, 0u, 0u, 0u, 0u, 0u})
        texture.blocks.push_back(static_cast<std::byte>(value));
    CHECK(uta::urender::bc4Floor(texture) == 0);
    texture.blocks[2] = std::byte{0}; // every texel index 0: red0
    CHECK(uta::urender::bc4Floor(texture) == 200);
}

TEST_CASE("UTA-0451: a texture bc4Floor cannot read counts as glossy", "[urender][materials]") {
    uta::ubundle::CompressedTexture texture = roughMap(191);
    texture.format = uta::ubundle::BlockFormat::BC7;
    CHECK(uta::urender::bc4Floor(texture) == 0);
    texture = roughMap(191);
    texture.blocks.resize(8);
    CHECK(uta::urender::bc4Floor(texture) == 0);
}
