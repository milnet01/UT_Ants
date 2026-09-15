// UTA-0040 INV-5 -- docs/specs/UTA-0040-parallax-occlusion.md.
//
// A square whose picture is red on its left half and blue on its right, raised
// under the red and sunk under the blue. Seen from its right, a ray over the
// sunk blue travels toward the raised red edge and meets it, so on a tier that
// marches the red reaches further right. Seen from its left, a ray over the
// blue travels away from that edge, so the red reaches no further.
//
// BOTH SIDES ARE NEEDED. The march reaches far enough that a march going the
// WRONG way wraps into the picture's next repeat, and from one side alone a
// wrong-way march and a right one can look alike.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <utility>
#include <vector>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;
using uta::urender::Tier;

namespace {

constexpr std::uint32_t WIDTH = 160;
constexpr std::uint32_t HEIGHT = 64;
constexpr float HALF = 55;        ///< the square's half width, in units
constexpr std::uint8_t DEPTH = 4; ///< texels, of an 8-texel-wide picture
// Every channel odd, as bc7Solid requires for an exact colour.
constexpr Rgba RED{201, 21, 41, 255};
constexpr Rgba BLUE{21, 41, 201, 255};

/// One BC4 block whose every texel is `value`.
std::vector<std::byte> bc4Solid(std::uint8_t value) {
    std::vector<std::byte> bytes(8, std::byte{0});
    bytes[0] = bytes[1] = std::byte{value};
    return bytes;
}

/// The square and its "step" material: an 8x4 picture of two BC7 blocks, red
/// then blue, and a height map of two BC4 blocks, 255 then 0.
uta::ubundle::Bundle steppedSquare(std::uint8_t parallaxDepth) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, HALF, "step", PF_UNLIT);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    bundle.materials = std::vector<uta::ubundle::MaterialRecord>{{"step", false, parallaxDepth}};

    uta::ubundle::CompressedTexture base;
    base.name = "step:base";
    base.format = uta::ubundle::BlockFormat::BC7;
    base.width = base.sourceWidth = 8;
    base.height = base.sourceHeight = 4;
    base.mipCount = 1;
    base.blocks = bc7Solid(RED);
    for (const std::byte b : bc7Solid(BLUE)) base.blocks.push_back(b);

    uta::ubundle::CompressedTexture height = base;
    height.name = "step:height";
    height.format = uta::ubundle::BlockFormat::BC4;
    height.blocks = bc4Solid(255);
    for (const std::byte b : bc4Solid(0)) height.blocks.push_back(b);

    bundle.textures = std::vector<uta::ubundle::CompressedTexture>{std::move(base), std::move(height)};
    return bundle;
}

/// The frame drawn from `cameraY` along the square's width, looking along +X.
std::vector<std::byte> drawn(Tier tier, std::uint8_t parallaxDepth, float cameraY) {
    Config config;
    config.width = WIDTH;
    config.height = HEIGHT;
    config.linearOutput = true;
    config.tier = tier;
    // UTA-0015's haze is drawn at Medium and not at Low; these cases compare
    // parallax alone.
    config.hazeScale = 0;
    Renderer renderer = requireRenderer(config);
    Camera camera;
    camera.location = {0, cameraY, 0};
    requireOk(renderer.draw(steppedSquare(parallaxDepth), camera));
    auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    return std::move(*pixels);
}

/// The column one past the end of the longest run of red pixels on the middle
/// row: where the red half gives way to the blue.
std::uint32_t redEnds(const std::vector<std::byte>& pixels) {
    std::uint32_t bestEnd = 0, bestLength = 0, length = 0;
    for (std::uint32_t x = 0; x < WIDTH; ++x) {
        const bool red = pixelAt(pixels, WIDTH, x, HEIGHT / 2).r > (RED.r + BLUE.r) / 2;
        length = red ? length + 1 : 0;
        if (length > bestLength) {
            bestLength = length;
            bestEnd = x + 1;
        }
    }
    REQUIRE(bestLength > 4);
    return bestEnd;
}

} // namespace

TEST_CASE("UTA-0040 INV-5: parallax moves a raised edge over a sunk one on Medium and not on Low", "[device]") {
    removeDisplay();
    // Measured on lavapipe, red's end in columns, Low then Medium. The camera
    // stands outside the square's width, so every point sees it from one side.
    //   from the right: 57 then 64 -- a march going the wrong way gives 54
    //   from the left: 105 then 102 -- the wrong way gives 122
    // The left's 3 is the height map's filtered edge: it is the same at depths
    // 2, 3 and 4, where a march's shift grows with the depth.
    const float right = HALF + 20;
    const std::uint32_t lowRight = redEnds(drawn(Tier::Low, DEPTH, right));
    const std::uint32_t mediumRight = redEnds(drawn(Tier::Medium, DEPTH, right));
    const std::uint32_t lowLeft = redEnds(drawn(Tier::Low, DEPTH, -right));
    const std::uint32_t mediumLeft = redEnds(drawn(Tier::Medium, DEPTH, -right));
    CAPTURE(lowRight, mediumRight, lowLeft, mediumLeft);
    // Seven measured; four leaves room for a driver's rounding.
    CHECK(mediumRight >= lowRight + 4);
    CHECK(mediumLeft <= lowLeft);
}

TEST_CASE("UTA-0040 INV-5: a material with no depth draws the same on Medium as on Low", "[device]") {
    removeDisplay();
    CHECK(drawn(Tier::Medium, 0, HALF + 20) == drawn(Tier::Low, 0, HALF + 20));
}
