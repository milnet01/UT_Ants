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
    // UTA-0180: and Low, whose lit surfaces have no large-scale variation to
    // scale it. Unset, the tier is the device's: High on a GPU.
    config.tier = uta::urender::Tier::Low;
    return config;
}

/// The bounced light a square gets on every face, when it gets any. Kept well
/// below 1, so the readback does not clip.
constexpr float BOUNCE = 0.25F;

/// A white square facing the camera, lit by an even field of BOUNCE when
/// `bounced` and by `light` when given, with an 8 by 8 atlas of HALF_OPEN when
/// `occluded`. Every vertex reads texel (6, 6), well inside the half-open region.
uta::ubundle::Bundle square(bool bounced, bool occluded, std::optional<uta::ubundle::Light> light = {}) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, 40, "white", 0);
    const std::size_t vertices = geometry.vertices.size();
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "white", WHITE);
    if (bounced) addEvenProbes(bundle, {100, -40, -40}, {100, 40, 40}, BOUNCE);
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

TEST_CASE("UTA-0164 INV-8: occlusion darkens bounced light by the atlas value", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());

    // UTA-0292 took zone ambient out of the shader, so the light from all
    // around that occlusion darkens is the probes' alone.
    const std::uint8_t open = redAtCentre(renderer, square(true, false));
    const std::uint8_t occluded = redAtCentre(renderer, square(true, true));
    const double expectedOpen = bouncedByte(BOUNCE);
    const double expectedOccluded = bouncedByte(BOUNCE * HALF_OPEN / 255.0);
    CAPTURE(int(open), int(occluded), expectedOpen, expectedOccluded);
    CHECK(std::abs(open - expectedOpen) <= 2.0);
    CHECK(std::abs(occluded - expectedOccluded) <= 2.0);
}

TEST_CASE("UTA-0164 INV-8: occlusion leaves a light's own contribution alone", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());
    // No probe, so the light is all there is.
    const uta::ubundle::Light light = steadyLight({95, 0, 0}, 128, 1);
    const std::uint8_t open = redAtCentre(renderer, square(false, false, light));
    const std::uint8_t occluded = redAtCentre(renderer, square(false, true, light));
    CAPTURE(int(open), int(occluded));
    CHECK(open > 0);
    CHECK(int(occluded) == int(open));
}
