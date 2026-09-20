// Baked ambient occlusion through the draw path --
// docs/specs/UTA-0164-ambient-occlusion.md SS 4.5 and INV-8.
//
// The atlas here is written by hand rather than baked, so the value a pixel
// should read is known exactly: the bake's own values are INV-2's. What this
// grades is what the bake cannot see -- that the atlas is bound, that the
// second vertex stream lines up with the first, and that occlusion darkens the
// light from all around and never a light's own.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;

namespace {

constexpr std::uint32_t WIDTH = 160;
constexpr std::uint32_t HEIGHT = 64;
constexpr Rgba WHITE{255, 255, 255, 255};

/// The value every texel of the atlas holds outside its white block.
constexpr std::uint8_t HALF_OPEN = 128;

Config linearFrame() {
    Config config;
    config.width = WIDTH;
    config.height = HEIGHT;
    config.linearOutput = true;
    // UTA-0015: no haze, which would add to the pixel this compares.
    config.hazeScale = 0;
    return config;
}

/// A white square facing the camera, lit by zone 0's `ambient` and by `light`
/// when given, with an 8 by 8 atlas of HALF_OPEN when `occluded`. Every vertex
/// reads texel (6, 6), well inside the half-open region.
uta::ubundle::Bundle square(std::uint8_t ambient, bool occluded, std::optional<uta::ubundle::Light> light = {}) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, 40, "white", 0);
    const std::size_t vertices = geometry.vertices.size();
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "white", WHITE);
    // Hue 0 at saturation 255 is white (UTA-0112 SS 4.3).
    bundle.zones = std::vector<uta::ubundle::Zone>{{ambient, 0, 255}};
    if (light) bundle.lights = std::vector{*light};
    if (occluded) {
        uta::ubundle::Occlusion atlas;
        atlas.texelSize = 16;
        atlas.width = 8;
        atlas.height = 8;
        atlas.texels.assign(64, HALF_OPEN);
        for (std::uint32_t y = 0; y < 4; ++y)
            for (std::uint32_t x = 0; x < 4; ++x) atlas.texels[y * 8 + x] = 255;
        atlas.uv.assign(vertices, {6.5f / 8, 6.5f / 8});
        bundle.occlusion = std::move(atlas);
    }
    return bundle;
}

std::uint8_t redAtCentre(Renderer& renderer, const uta::ubundle::Bundle& bundle) {
    requireOk(renderer.draw(bundle, Camera{}));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    return pixelAt(*pixels, WIDTH, 80, 32).r;
}

} // namespace

TEST_CASE("UTA-0164 INV-8: occlusion darkens ambient light by the atlas value", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());

    // As RenderLightingTest's zone case: AMBIENT_SCALE times brightness 40
    // through FGetHSV's curve, kept below 1 so the readback does not clip.
    constexpr double AMBIENT_SCALE = 1.0; // UTA-0187's refit with DISPLAY_LIGHT_POWER
    const double ambient = AMBIENT_SCALE * 0.391061428321661;
    const std::uint8_t open = redAtCentre(renderer, square(40, false));
    const std::uint8_t occluded = redAtCentre(renderer, square(40, true));
    const double expectedOpen = litByte(ambient);
    const double expectedOccluded = litByte(ambient * HALF_OPEN / 255.0);
    CAPTURE(int(open), int(occluded), expectedOpen, expectedOccluded);
    CHECK(std::abs(open - expectedOpen) <= 2.0);
    CHECK(std::abs(occluded - expectedOccluded) <= 2.0);
}

TEST_CASE("UTA-0164 INV-8: occlusion leaves a light's own contribution alone", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());
    // No ambient and no probe, so the light is all there is.
    const uta::ubundle::Light light = steadyLight({95, 0, 0}, 128, 1);
    const std::uint8_t open = redAtCentre(renderer, square(0, false, light));
    const std::uint8_t occluded = redAtCentre(renderer, square(0, true, light));
    CAPTURE(int(open), int(occluded));
    CHECK(open > 0);
    CHECK(int(occluded) == int(open));
}
