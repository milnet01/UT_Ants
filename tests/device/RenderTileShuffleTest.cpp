// UTA-0277: a natural picture's repeats drawn at hashed offsets --
// docs/specs/UTA-0277-per-tile-variation.md SS 4.4, INV-4 and INV-5.
//
// ONE SQUARE FILLS A NARROW VIEW with a 16-texel picture repeating every 32
// pixels across it, as RenderWaterTest's tiled pool does, at render scale 1 so
// the tiers draw the same pixels. Each frame is a fresh renderer's first.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

using namespace uta::test::render;
using uta::ubundle::TileKind;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;
using uta::urender::Tier;

namespace {

constexpr std::uint32_t SIZE = 128;
constexpr float NARROW_FOV = 10.0f;
const float DISTANCE = static_cast<float>(SIZE / 2.0 / std::tan(NARROW_FOV / 2 * std::numbers::pi / 180.0));
constexpr float REPEAT = 32; // pixels one repeat of the picture spans

Camera narrow() {
    Camera camera;
    camera.verticalFovDegrees = NARROW_FOV;
    return camera;
}

/// A square past the view's edge on every side, facing the camera, its picture
/// repeating every REPEAT pixels.
uta::ubundle::Geometry tiledSquare(std::uint32_t polyFlags) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, DISTANCE, 0, 0, SIZE, "rock", polyFlags);
    for (uta::ubundle::GeometryVertex& vertex : geometry.vertices) {
        vertex.u *= 2 * SIZE / REPEAT;
        vertex.v *= 2 * SIZE / REPEAT;
    }
    return geometry;
}

/// A 16x16 texture of sixteen 4x4 blocks, `block(i)` giving block i's bytes.
template <typename Block>
uta::ubundle::CompressedTexture sixteenBlocks(std::string name, uta::ubundle::BlockFormat format, Block block) {
    uta::ubundle::CompressedTexture texture;
    texture.name = std::move(name);
    texture.format = format;
    texture.width = texture.sourceWidth = 16;
    texture.height = texture.sourceHeight = 16;
    texture.mipCount = 1;
    for (std::uint8_t i = 0; i < 16; ++i)
        for (const std::byte b : block(i)) texture.blocks.push_back(b);
    return texture;
}

/// Sixteen different colours, so every block of a repeat differs from its
/// neighbours. Even bytes: bc7Solid's parity rule.
uta::ubundle::CompressedTexture patchwork() {
    return sixteenBlocks("rock:base", uta::ubundle::BlockFormat::BC7, [](std::uint8_t i) {
        const auto channel = [](unsigned v) { return static_cast<std::uint8_t>((v * 2u) & 0xFEu); };
        return bc7Solid(Rgba{channel(i * 37u + 20u), channel(i * 71u + 5u), channel(i * 13u + 90u), 254});
    });
}

/// The block INV-5's feature sits in.
constexpr std::uint8_t FEATURE = 5;

/// Grey, with FEATURE white when `marked`.
uta::ubundle::CompressedTexture greyBase(bool marked) {
    return sixteenBlocks("rock:base", uta::ubundle::BlockFormat::BC7, [marked](std::uint8_t i) {
        return bc7Solid(marked && i == FEATURE ? Rgba{254, 254, 254, 254} : Rgba{128, 128, 128, 254});
    });
}

/// Flat, with FEATURE tilted all the way along u when `tilted`.
uta::ubundle::CompressedTexture normals(bool tilted) {
    return sixteenBlocks("rock:normal", uta::ubundle::BlockFormat::BC5, [tilted](std::uint8_t i) {
        return tilted && i == FEATURE ? bc5Solid(255, 128) : bc5Solid(128, 128);
    });
}

uta::ubundle::Bundle rock(std::uint32_t polyFlags, TileKind kind, std::vector<uta::ubundle::CompressedTexture> maps) {
    uta::ubundle::Bundle bundle = bundleOf(tiledSquare(polyFlags));
    uta::ubundle::MaterialRecord record{"rock", false, 0};
    record.tileKind = kind;
    bundle.materials = std::vector<uta::ubundle::MaterialRecord>{record};
    bundle.textures = std::move(maps);
    return bundle;
}

std::vector<std::byte> frameOf(const uta::ubundle::Bundle& bundle, Tier tier) {
    Config config;
    config.width = SIZE;
    config.height = SIZE;
    config.linearOutput = true;
    config.tier = tier;
    config.fixedRenderScale = 1.0;
    config.hazeScale = 0;
    Renderer renderer = requireRenderer(config);
    renderer.pinLightSeconds(1.0);
    requireOk(renderer.draw(bundle, narrow()));
    auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    return std::move(*pixels);
}

/// Pixels whose red, green or blue differ between `a` and `b` by more than
/// `threshold`.
std::vector<bool> differing(const std::vector<std::byte>& a, const std::vector<std::byte>& b, int threshold) {
    std::vector<bool> out(std::size_t{SIZE} * SIZE);
    for (std::uint32_t y = 0; y < SIZE; ++y)
        for (std::uint32_t x = 0; x < SIZE; ++x) {
            const Rgba p = pixelAt(a, SIZE, x, y);
            const Rgba q = pixelAt(b, SIZE, x, y);
            out[std::size_t{y} * SIZE + x] =
                std::abs(p.r - q.r) > threshold || std::abs(p.g - q.g) > threshold || std::abs(p.b - q.b) > threshold;
        }
    return out;
}

std::size_t countOf(const std::vector<bool>& mask) {
    std::size_t n = 0;
    for (const bool set : mask) n += set ? 1 : 0;
    return n;
}

std::size_t overlap(const std::vector<bool>& a, const std::vector<bool>& b) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.size(); ++i) n += a[i] && b[i] ? 1 : 0;
    return n;
}

} // namespace

TEST_CASE("UTA-0277 INV-4: Unsure draws as Fixed and Shuffle below its tier draws as Fixed", "[device]") {
    removeDisplay();
    const auto fixedHigh = frameOf(rock(PF_UNLIT, TileKind::Fixed, {patchwork()}), Tier::High);
    const auto unsureHigh = frameOf(rock(PF_UNLIT, TileKind::Unsure, {patchwork()}), Tier::High);
    const auto shuffleHigh = frameOf(rock(PF_UNLIT, TileKind::Shuffle, {patchwork()}), Tier::High);
    const auto fixedLow = frameOf(rock(PF_UNLIT, TileKind::Fixed, {patchwork()}), Tier::Low);
    const auto shuffleLow = frameOf(rock(PF_UNLIT, TileKind::Shuffle, {patchwork()}), Tier::Low);
    CHECK(unsureHigh == fixedHigh);
    CHECK(shuffleLow == fixedLow);
    // And at its tier it moves the picture, so the two above are not vacuous.
    const std::size_t moved = countOf(differing(shuffleHigh, fixedHigh, 4));
    INFO("pixels Shuffle moved at High: " << moved);
    CHECK(moved > SIZE * SIZE / 4);
}

TEST_CASE("UTA-0277 INV-5: a shuffled normal map lights the feature where its colour is", "[device]") {
    // Three lit frames of one Shuffle material: grey with FEATURE white and
    // its normal tilted there; the same with a flat normal map; and plain grey
    // with a flat normal map. The first two differ where the tilt drew, the
    // last two where the colour drew, and the two must be the same pixels.
    removeDisplay();
    const auto litBy = [](TileKind kind, bool marked, bool tilted) {
        uta::ubundle::Bundle bundle = rock(0, kind, {greyBase(marked), normals(tilted)});
        bundle.lights = std::vector<uta::ubundle::Light>{steadyLight({DISTANCE - 40.0f, 0.0f, 0.0f}, 255, 255)};
        return frameOf(bundle, Tier::High);
    };
    const auto both = litBy(TileKind::Shuffle, true, true);
    const auto colourOnly = litBy(TileKind::Shuffle, true, false);
    const auto plain = litBy(TileKind::Shuffle, false, false);
    const std::vector<bool> lit = differing(both, colourOnly, 8);
    const std::vector<bool> coloured = differing(colourOnly, plain, 8);
    const std::size_t shared = overlap(lit, coloured);
    INFO("tilt pixels " << countOf(lit) << ", colour pixels " << countOf(coloured) << ", shared " << shared);
    REQUIRE(countOf(coloured) > 0);
    REQUIRE(countOf(lit) > 0);
    CHECK(shared * 10 >= countOf(lit) * 9);
    CHECK(shared * 10 >= countOf(coloured) * 7);
    // The feature did move: Fixed puts its colour elsewhere.
    const auto fixedColour = differing(litBy(TileKind::Fixed, true, false), litBy(TileKind::Fixed, false, false), 8);
    CHECK(overlap(fixedColour, coloured) * 2 < countOf(coloured));
}
