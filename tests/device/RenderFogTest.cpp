// UTA-0015's fog and flashlight through the draw path --
// docs/specs/UTA-0015-volumetric-fog.md INV-4 to INV-7. Which lights glow, and
// the flashlight's numbers, are graded device-free in
// tests/unit/RenderVolumeLightsTest.cpp.
//
// EVERY CASE COMPARES AGAINST A CONTROL FRAME that differs in the one thing the
// rule is about, so a fog pass that darkened or lit everything fails the
// control rather than passing the check.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <vector>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;
using uta::urender::Tier;

namespace {

constexpr std::uint32_t WIDTH = 160;
constexpr std::uint32_t HEIGHT = 64;
constexpr Rgba WHITE{255, 255, 255, 255};
constexpr Rgba BLACK{0, 0, 0, 0};

// shaders/fog.glsl's, which SS 7 sets; a case using one says so.
constexpr double HAZE_EXTINCTION = 1.28614e-5;
constexpr double HAZE_SCATTER = 6.0e-3;
constexpr double VOLUME_GLOW_SCALE = 4.0e-3;

Config fogFrame(Tier tier, float hazeScale) {
    Config config;
    config.width = WIDTH;
    config.height = HEIGHT;
    config.linearOutput = true;
    config.tier = tier;
    config.hazeScale = hazeScale;
    return config;
}

/// A square of one solid colour at x = `distance`, `half` units either side,
/// unless `lit` then unlit.
uta::ubundle::Bundle wallAt(float distance, float half, const Rgba& colour, bool lit = false) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, distance, 0, 0, half, "wall", lit ? 0 : PF_UNLIT);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "wall", colour);
    return bundle;
}

std::uint8_t redAt(Renderer& renderer, const uta::ubundle::Bundle& bundle, const Camera& camera,
                   std::uint32_t x = 80, std::uint32_t y = 32) {
    requireOk(renderer.draw(bundle, camera));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    return pixelAt(*pixels, WIDTH, x, y).r;
}

Camera withFlashlight(bool on) {
    Camera camera;
    camera.flashlight = on;
    return camera;
}

} // namespace

TEST_CASE("UTA-0015 INV-4: the flashlight lights the wall ahead only while it is on", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(fogFrame(Tier::Low, 0.0f));
    // No light, probe or ambient: the flashlight is the only light there is.
    const uta::ubundle::Bundle bundle = wallAt(100, 40, WHITE, true);
    const int off = redAt(renderer, bundle, withFlashlight(false));
    const int on = redAt(renderer, bundle, withFlashlight(true));
    CAPTURE(off, on);
    CHECK(off == 0);
    CHECK(on > 200);

    // Turned 45 degrees toward +Y, the camera sees the wall at y = 100, and the
    // beam must turn with it: one fixed along +X lights nothing there.
    const uta::ubundle::Bundle wide = wallAt(100, 400, WHITE, true);
    Camera turned = withFlashlight(true);
    turned.rotation = {0, 8192, 0};
    const int followed = redAt(renderer, wide, turned);
    CAPTURE(followed);
    CHECK(followed > 100);
}

TEST_CASE("UTA-0015 INV-5: haze thins an unlit wall by its transmittance at Medium and not at Low", "[device]") {
    removeDisplay();
    constexpr float HAZE = 50.0f;
    constexpr double DISTANCE = 1000.0;
    // Wide enough to fill the view at 1000 units.
    const uta::ubundle::Bundle bundle = wallAt(static_cast<float>(DISTANCE), 3000, WHITE);

    Renderer medium = requireRenderer(fogFrame(Tier::Medium, HAZE));
    const int thinned = redAt(medium, bundle, Camera{});
    const double expected = std::exp(-HAZE_EXTINCTION * HAZE * DISTANCE);
    CAPTURE(thinned, expected, srgbByte(expected));
    // Within 0.02 of the transmittance, measured where it lands: sRGB's slope
    // near 0.53 turns 0.02 into about five levels.
    CHECK(std::abs(thinned - srgbByte(expected)) <= 5.0);

    Renderer low = requireRenderer(fogFrame(Tier::Low, HAZE));
    CHECK(int(redAt(low, bundle, Camera{})) == 255);

    Config refused = fogFrame(Tier::Low, -1.0f);
    const auto created = Renderer::create(refused);
    REQUIRE_FALSE(created.has_value());
    CHECK(created.error().code() == uta::ErrorCode::InvalidArgument);
}

TEST_CASE("UTA-0015 INV-6: a volumetric light thickens and lights the air only in a fog zone", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(fogFrame(Tier::Medium, 0.0f));
    // With no ROOM the camera and the light are both in zone 0, so the zone's
    // fog flag alone decides. The light's volume radius, 6400 units, holds the
    // whole view ray; its own radius, 50 units, lights nothing drawn.
    const auto scene = [](const Rgba& wall, std::uint8_t fog, std::uint8_t volumeFog, std::uint8_t volumeBrightness) {
        uta::ubundle::Bundle bundle = wallAt(1000, 3000, wall);
        uta::ubundle::Light light = steadyLight({500, 0, 0}, 255, 1);
        light.volumeRadius = 255;
        light.volumeFog = volumeFog;
        light.volumeBrightness = volumeBrightness;
        bundle.lights = std::vector{light};
        bundle.zones = std::vector<uta::ubundle::Zone>{{0, 0, 0, fog}};
        return bundle;
    };

    const int thick = redAt(renderer, scene(WHITE, 1, 255, 0), Camera{});
    const int clear = redAt(renderer, scene(WHITE, 0, 255, 0), Camera{});
    CAPTURE(thick, clear);
    CHECK(clear == 255);
    CHECK(thick < clear - 20);

    const int glowing = redAt(renderer, scene(BLACK, 1, 0, 255), Camera{});
    const int dark = redAt(renderer, scene(BLACK, 0, 0, 255), Camera{});
    CAPTURE(glowing, dark);
    CHECK(dark == 0);
    CHECK(glowing > 20);
}

TEST_CASE("UTA-0015 INV-6: a volumetric light glows in front of the wall that hides it", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(fogFrame(Tier::Medium, 0.0f));
    // The light is 100 units behind a black wall that fills the view, so every
    // froxel in front of the wall is in its shadow. Its own radius, 1625 units,
    // reaches the camera, so the shadow plan gives it faces; UT99 draws the glow
    // anyway (SS 4.3), and a glow read through shadowOf would draw none.
    // A second square just behind the wall faces the light: a one-sided wall
    // shows the light only its back, which the shadow pass culls.
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 1000, 0, 0, 3000, "wall", PF_UNLIT);
    addSquare(geometry, 1001, 0, 0, 3000, "wall", PF_UNLIT, true);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "wall", BLACK);
    uta::ubundle::Light light = steadyLight({1100, 0, 0}, 255, 64);
    light.volumeRadius = 255;
    // A glow of 8e-4 a unit: about 0.8 over the 1000 units in front of the
    // wall, well under the clip. The slice holding the wall reaches past it
    // into the lit gap and bleeds in whatever the rule (SS 6's halos), but at
    // most about 0.08 of this glow, so only an unshadowed column reads bright.
    light.volumeBrightness = static_cast<std::uint8_t>(std::lround(8.0e-4 / VOLUME_GLOW_SCALE * 64.0));
    bundle.lights = std::vector{light};
    bundle.zones = std::vector<uta::ubundle::Zone>{{0, 0, 0, 1}};
    const int hidden = redAt(renderer, bundle, Camera{});
    CAPTURE(hidden, int(light.volumeBrightness), renderer.lastFrameStats().unshadowedLights);
    CHECK(renderer.lastFrameStats().unshadowedLights == 0u);
    CHECK(hidden > 180);
}

TEST_CASE("UTA-0015 INV-7: a light's shaft is cut by an occluder's shadow", "[device]") {
    removeDisplay();
    // Measured on Mesa's software driver: at a scattering of 5e-4 a unit the open
    // ray drew 58 and the shadowed one 36, so seven times that keeps both well
    // above the readback's noise and under the clip.
    const float haze = static_cast<float>(3.5e-3 / HAZE_SCATTER);
    Renderer renderer = requireRenderer(fogFrame(Tier::Medium, haze));

    // A light above the view axis, and a small occluder between it and the
    // rays on the +Y side only. Column 88 looks along y = 0.27 x and passes
    // under the occluder's shadow from x = 0 to about 375; column 71 is its
    // mirror, which no shadow crosses. Neither ray meets the occluder itself:
    // at x = 450 they are 120 units to the side and 67 below it.
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 1000, 0, 0, 3000, "black", PF_UNLIT);
    addSquare(geometry, 450, 15, 75, 15, "black", PF_UNLIT);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "black", BLACK);
    bundle.lights = std::vector{steadyLight({500, 0, 100}, 255, 64)};

    const int shadowed = redAt(renderer, bundle, Camera{}, 88, 32);
    const int open = redAt(renderer, bundle, Camera{}, 71, 32);
    CAPTURE(shadowed, open);
    CHECK(open > 100);
    CHECK(shadowed < open - 20);

    // The flashlight has no shadow faces and still scatters.
    const uta::ubundle::Bundle unlitRoom = wallAt(1000, 3000, BLACK);
    const int torch = redAt(renderer, unlitRoom, withFlashlight(true));
    const int none = redAt(renderer, unlitRoom, withFlashlight(false));
    CAPTURE(torch, none);
    CHECK(none == 0);
    CHECK(torch > 20);
}
