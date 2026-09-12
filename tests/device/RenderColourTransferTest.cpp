// UTA-0014 INV-10 -- docs/specs/UTA-0014-vulkan-draw-path.md SS 4.10.
//
// The sRGB transfer is applied exactly once in each direction: with
// Config::linearOutput set, a base-colour texel sampled and written straight
// out, with no lighting, comes back with the value it was stored with.
//
// linearOutput IS LOAD-BEARING, not a convenience: exposure and the PBR Neutral
// tone map sit between the two ends otherwise, and neither is the identity.
//
// The colours are BC7 mode 6 blocks, whose stored texels are exact, and they
// span the curve: near black, a mid colour, and full scale.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;

namespace {

constexpr std::uint32_t WIDTH = 160;
constexpr std::uint32_t HEIGHT = 64;

/// The pixel column a square centred at world `y` on the plane x = 100 lands
/// on: 250 units either side at that distance, across 160 pixels.
std::uint32_t columnOf(float y) { return static_cast<std::uint32_t>(80.0f + y * 0.32f); }

const std::array<Rgba, 3> COLOURS = {{{1, 127, 255, 255}, {33, 65, 129, 255}, {201, 121, 61, 255}}};

uta::ubundle::Bundle threeColours() {
    uta::ubundle::Geometry geometry;
    const std::array<float, 3> ys = {-100.0f, 0.0f, 100.0f};
    for (std::size_t i = 0; i < COLOURS.size(); ++i)
        addSquare(geometry, 100, ys[i], 0, 30, "colour" + std::to_string(i), PF_UNLIT);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    for (std::size_t i = 0; i < COLOURS.size(); ++i) addSolidMaterial(bundle, "colour" + std::to_string(i), COLOURS[i]);
    return bundle;
}

} // namespace

TEST_CASE("INV-10: a base colour sampled and written straight out comes back as stored", "[device]") {
    removeDisplay();
    Config config;
    config.width = WIDTH;
    config.height = HEIGHT;
    config.linearOutput = true;
    auto renderer = requireRenderer(config);

    const uta::ubundle::Bundle bundle = threeColours();
    requireOk(renderer.draw(bundle, Camera{}));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());

    CHECK(pixelAt(*pixels, WIDTH, columnOf(-100), HEIGHT / 2) == COLOURS[0]);
    CHECK(pixelAt(*pixels, WIDTH, columnOf(0), HEIGHT / 2) == COLOURS[1]);
    CHECK(pixelAt(*pixels, WIDTH, columnOf(100), HEIGHT / 2) == COLOURS[2]);
}

TEST_CASE("the output stage applies to unlit surfaces too", "[device]") {
    // SS 4.10: PF_Unlit means no light is applied, not no output stage. So the
    // same frame without linearOutput must NOT come back as stored -- every
    // colour here is dark enough in one channel for PBR Neutral's toe to move it.
    removeDisplay();
    Config config;
    config.width = WIDTH;
    config.height = HEIGHT;
    auto renderer = requireRenderer(config);

    const uta::ubundle::Bundle bundle = threeColours();
    requireOk(renderer.draw(bundle, Camera{}));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());

    CHECK(pixelAt(*pixels, WIDTH, columnOf(-100), HEIGHT / 2) != COLOURS[0]);
    CHECK(pixelAt(*pixels, WIDTH, columnOf(0), HEIGHT / 2) != COLOURS[1]);
    CHECK(pixelAt(*pixels, WIDTH, columnOf(100), HEIGHT / 2) != COLOURS[2]);
}
