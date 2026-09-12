// Shadow maps through the draw path -- docs/specs/UTA-0014-vulkan-draw-path.md
// SS 4.8 and SS 6.
//
// The planning is graded device-free (tests/unit/RenderShadowsTest.cpp). These
// cases grade what the pixels show: a surface behind an occluder goes dark
// while the rest of it stays lit, for two faces of a point light and for a
// spotlight; a still frame reuses its tiles and still shows the shadow; and
// lights the atlas cannot hold are counted.
//
// EVERY SHADOW CASE HAS AN UNSHADOWED CONTROL PIXEL on the same square, so a
// shadow that darkened everything -- a bias gone wrong, a face read for the
// wrong direction -- fails the control rather than passing the shadow check.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"
#include "ubake/LightModel.h"
#include "urender/Shadows.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <vector>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;

namespace {

constexpr std::uint32_t WIDTH = 160;
constexpr std::uint32_t HEIGHT = 64;
constexpr Rgba WHITE{255, 255, 255, 255};

Config linearFrame() {
    Config config;
    config.width = WIDTH;
    config.height = HEIGHT;
    config.linearOutput = true;
    return config;
}

/// A white square at x = 100, lit by `light`, with -- when `occluded` -- a small
/// square at x = 80 centred on (y, z) between the light and the square's centre.
uta::ubundle::Bundle squareLitBy(const uta::ubundle::Light& light, bool occluded, float y, float z) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, 40, "white", 0);
    if (occluded) addSquare(geometry, 80, y, z, 6, "white", 0);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "white", WHITE);
    bundle.lights = std::vector{light};
    return bundle;
}

struct Pixel {
    std::uint32_t x, y;
};

std::uint8_t redAt(Renderer& renderer, const uta::ubundle::Bundle& bundle, Pixel pixel) {
    requireOk(renderer.draw(bundle, Camera{}));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    return pixelAt(*pixels, WIDTH, pixel.x, pixel.y).r;
}

struct Case {
    const char* what;
    uta::ubundle::Light light;
    float occluderY, occluderZ;
    Pixel shadowed; ///< the square's centre, behind the occluder
    Pixel control;  ///< a point of the square the occluder does not hide from the light
};

void checkShadow(const Case& c) {
    CAPTURE(c.what);
    Renderer renderer = requireRenderer(linearFrame());
    const uta::ubundle::Bundle open = squareLitBy(c.light, false, c.occluderY, c.occluderZ);
    const uta::ubundle::Bundle blocked = squareLitBy(c.light, true, c.occluderY, c.occluderZ);

    const int litCentre = redAt(renderer, open, c.shadowed);
    const int litControl = redAt(renderer, open, c.control);
    const int darkCentre = redAt(renderer, blocked, c.shadowed);
    const int blockedControl = redAt(renderer, blocked, c.control);
    CAPTURE(litCentre, darkCentre, litControl, blockedControl);

    CHECK(litCentre > 60);
    CHECK(darkCentre < 10);
    // The shadow is where the occluder is, and nowhere else.
    CHECK(std::abs(blockedControl - litControl) <= 3);
    CHECK(litControl > 60);
}

uta::ubundle::Light pointAt(std::array<float, 3> location) {
    return steadyLight(location, 255, 64); // radius 1625
}

} // namespace

TEST_CASE("SS 4.8: a surface behind an occluder is in a point light's shadow on its -Y face", "[device]") {
    removeDisplay();
    // Light up and to the right; the line to the square's centre crosses x = 80
    // at y = 30. The control point (100, -20.3, 0) crosses it at y = 19.9, clear
    // of the occluder's 24 to 36.
    checkShadow({"point light, -Y face", pointAt({60, 60, 0}), 30, 0, {80, 32}, {73, 32}});
}

TEST_CASE("SS 4.8: a surface behind an occluder is in a point light's shadow on its -Z face", "[device]") {
    removeDisplay();
    // Light above; the line crosses x = 80 at z = 30. The control point
    // (100, 1.6, -14.1) crosses it at z = 23, below the occluder's 24 to 36.
    checkShadow({"point light, -Z face", pointAt({60, 0, 60}), 0, 30, {80, 32}, {80, 36}});
}

TEST_CASE("SS 4.8: a spotlight casts a shadow through its one tile", "[device]") {
    removeDisplay();
    uta::ubundle::Light spot = pointAt({60, 60, 0});
    spot.effect = 12; // LE_Spotlight
    spot.cone = 60;   // a half-angle of about 40 degrees
    // Yaw toward the square's centre: atan2(-60, 40) is -56.3 degrees.
    spot.rotation = {0, -10251, 0};
    checkShadow({"spotlight", spot, 30, 0, {80, 32}, {73, 32}});
}

TEST_CASE("SS 4.8: a still frame draws its tiles once and keeps showing the shadow", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());
    const uta::ubundle::Bundle blocked = squareLitBy(pointAt({60, 60, 0}), true, 30, 0);

    const int first = redAt(renderer, blocked, {80, 32});
    CHECK(renderer.lastFrameStats().renderedShadowTiles == 6u);
    const int second = redAt(renderer, blocked, {80, 32});
    CHECK(renderer.lastFrameStats().renderedShadowTiles == 0u);
    CAPTURE(first, second);
    CHECK(second < 10); // drawn from the kept tiles
    CHECK(second == first);
}

TEST_CASE("SS 4.8: a grazing light with a coarse tile leaves a lit surface lit", "[device]") {
    // The case where a surface shadows itself. The light is nearly in the wall's
    // plane, and far enough from the camera to get the smallest tile, so one
    // texel spans several units of wall. There is no occluder at all: every
    // pixel must be what the light model gives, and a surface reading its own
    // depth as an occluder goes dark in bands. What keeps it lit is the tile
    // pass's slope-scaled depth bias, and this is the case that grades it.
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 2000, 0, 0, 600, "white", 0);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "white", WHITE);
    const uta::ubundle::Light light = steadyLight({1990, 0, 300}, 255, 20); // radius 525
    bundle.lights = std::vector{light};

    // The fixture is only as hard as its tile is coarse, so that is asserted.
    CHECK(uta::urender::shadowTileSize(Camera{}, WIDTH, HEIGHT, light) == uta::urender::SMALLEST_SHADOW_TILE);

    requireOk(renderer.draw(bundle, Camera{}));
    CHECK(renderer.lastFrameStats().renderedShadowTiles == 6u);
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());

    // Pixels across the part of the wall the light reaches: 2,500 units either
    // side at this distance across 160 columns, 2,000 above and below across 64
    // rows.
    for (const std::uint32_t column : {72u, 80u, 87u}) {
        for (const std::uint32_t row : {24u, 28u, 32u}) {
            const uta::ubake::Vec3 x{2000.0, ((column + 0.5) / WIDTH * 2 - 1) * 5000.0,
                                     -((row + 0.5) / HEIGHT * 2 - 1) * 2000.0};
            const double expected = srgbByte(uta::ubake::lightAt(light, x, {-1, 0, 0}).r);
            const int red = pixelAt(*pixels, WIDTH, column, row).r;
            CAPTURE(column, row, red, expected);
            CHECK(std::abs(red - expected) <= 3.0);
        }
    }
}

TEST_CASE("SS 6: lights the shadow atlas cannot hold are lit unshadowed and counted", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, 40, "white", 0);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "white", WHITE);
    // Five point lights around the camera, each wanting the largest tile. The
    // atlas holds sixteen, a point light takes six, so two fit.
    std::vector<uta::ubundle::Light> lights;
    for (int i = 0; i < 5; ++i) lights.push_back(steadyLight({10.0f + i, 0, 0}, 10, 200));
    bundle.lights = lights;

    requireOk(renderer.draw(bundle, Camera{}));
    CHECK(renderer.lastFrameStats().unshadowedLights == 3u);
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    CHECK(pixelAt(*pixels, WIDTH, 80, 32).r > 0); // lit all the same
}
