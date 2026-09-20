// Direct light through the draw path -- docs/specs/UTA-0014-vulkan-draw-path.md
// SS 4.6 and SS 6.
//
// INV-6 grades the light model in isolation. These cases grade what it cannot
// see: that a shaded pixel receives its light at all through the cluster lists,
// that SS 6's overflow is counted, and that a normal map tilts the lit side the
// way umat encoded it -- a sign error there looks like light from the wrong
// side, and nothing else here would notice.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"
#include "ubake/LightModel.h"

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

Config linearFrame() {
    Config config;
    config.width = WIDTH;
    config.height = HEIGHT;
    config.linearOutput = true;
    // UTA-0015: no haze. On a device whose tier draws fog, in-scattering adds
    // to a pixel these cases compare against the light model alone.
    config.hazeScale = 0;
    return config;
}

/// The world point pixel (80, 32)'s centre sees on the plane x = 100: 250
/// units either side across 160 pixels, 100 above and below across 64.
uta::ubake::Vec3 centrePixelOnSquare() {
    return {100.0, (80.5 / WIDTH * 2 - 1) * 250.0, -(32.5 / HEIGHT * 2 - 1) * 100.0};
}

/// A white lit square facing the camera at x = 100, wearing `material`.
uta::ubundle::Bundle litSquare(const std::string& material) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, 40, material, 0);
    return bundleOf(std::move(geometry));
}

std::uint8_t redAtCentre(Renderer& renderer, const uta::ubundle::Bundle& bundle) {
    requireOk(renderer.draw(bundle, Camera{}));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    return pixelAt(*pixels, WIDTH, 80, 32).r;
}

} // namespace

TEST_CASE("a lit surface receives its light through its own cluster's list", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());
    uta::ubundle::Bundle bundle = litSquare("white");
    addSolidMaterial(bundle, "white", WHITE);
    // Radius byte 1 is 50 units. It reaches the clusters around the square and
    // not the near, corner ones -- so a pixel reading any list but its own
    // misses the light. A radius covering the whole view would hide that.
    const uta::ubundle::Light light = steadyLight({95, 0, 0}, 128, 1);
    bundle.lights = std::vector{light};

    const std::uint8_t red = redAtCentre(renderer, bundle);
    // Reflectance 1 times the light, encoded by the _SRGB target (SS 4.10).
    const double expected = litByte(uta::ubake::lightAt(light, centrePixelOnSquare(), {-1, 0, 0}).r);
    CAPTURE(int(red), expected);
    CHECK(std::abs(red - expected) <= 2.0);
    CHECK(renderer.lastFrameStats().overflowedClusters == 0u);
}

TEST_CASE("UTA-0162 INV-8: a strip lights a pixel near its far end through the cluster lists", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());
    uta::ubundle::Bundle bundle = litSquare("white");
    addSolidMaterial(bundle, "white", WHITE);
    // Radius byte 1 is 50 units, as the first case's. The strip runs from 400
    // units before the square to 5 before it, along the view axis: the pixel is
    // within 50 of its far end and 400 from its location, so a cluster that
    // tested the sphere at the location would list no light here.
    uta::ubundle::Light strip = steadyLight({-305, 0, 0}, 128, 1);
    strip.strip = uta::ubundle::STRIP_LEADER;
    strip.stripFrom = {-305, 0, 0};
    strip.stripTo = {95, 0, 0};
    bundle.lights = std::vector{strip};

    const std::uint8_t red = redAtCentre(renderer, bundle);
    const double expected = litByte(uta::ubake::lightAt(strip, centrePixelOnSquare(), {-1, 0, 0}).r);
    CAPTURE(int(red), expected);
    CHECK(expected > 100);
    CHECK(std::abs(red - expected) <= 2.0);
}

TEST_CASE("UTA-0156 INV-6: a zone's ambient lights a lit surface and leaves an unlit one alone", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());
    // No light and no probe, so ambient is the only light. The square is in
    // zone 1, so a vertex attribute or binding that reads zone 0 draws black.
    const auto squareInZone = [](std::uint8_t brightness, std::uint32_t polyFlags) {
        uta::ubundle::Geometry geometry;
        addSquare(geometry, 100, 0, 0, 40, "white", polyFlags);
        for (uta::ubundle::GeometryVertex& vertex : geometry.vertices) vertex.zone = 1;
        uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
        addSolidMaterial(bundle, "white", WHITE);
        // Hue 0 at saturation 255 is white (UTA-0112 SS 4.3).
        bundle.zones = std::vector<uta::ubundle::Zone>{{0, 0, 0}, {brightness, 0, 255}};
        return bundle;
    };

    // light.glsl's AMBIENT_SCALE, set by UTA-0156 SS 7's measurement. Brightness
    // 40 keeps the lit value below 1, where the 8-bit readback would clip it.
    constexpr double AMBIENT_SCALE = 1.0; // UTA-0187's refit with DISPLAY_LIGHT_POWER
    // UTA-0165: brightness 40 through FGetHSV's curve is 0.391061428321661.
    const double expected = litByte(AMBIENT_SCALE * 0.391061428321661);
    const std::uint8_t lit = redAtCentre(renderer, squareInZone(40, 0));
    CAPTURE(int(lit), expected);
    CHECK(std::abs(lit - expected) <= 2.0);

    CHECK(int(redAtCentre(renderer, squareInZone(0, 0))) == 0);

    constexpr std::uint32_t PF_UNLIT = 0x00400000u;
    CHECK(int(redAtCentre(renderer, squareInZone(128, PF_UNLIT))) == 255);
}

TEST_CASE("a flat normal map lights exactly as the surface's own normal", "[device]") {
    // umat writes zero tilt as the byte 128 (127.5 * 0 + 128), so that is what
    // must decode to zero. A light nearly in the surface's plane is where a
    // small tilt shows: its incidence is a sixtieth, and reading 128 as
    // 2 * 128 / 255 - 1 would brighten it by a fifth.
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());
    uta::ubundle::Bundle bundle = litSquare("flat");
    addNormalMappedMaterial(bundle, "flat", WHITE, 128, 128);
    const uta::ubundle::Light light = steadyLight({99, 0, 60}, 255, 64);
    bundle.lights = std::vector{light};

    const std::uint8_t red = redAtCentre(renderer, bundle);
    const double expected = litByte(uta::ubake::lightAt(light, centrePixelOnSquare(), {-1, 0, 0}).r);
    CAPTURE(int(red), expected);
    CHECK(std::abs(red - expected) <= 2.0);
}

TEST_CASE("a light behind the surface adds nothing", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());
    uta::ubundle::Bundle bundle = litSquare("white");
    addSolidMaterial(bundle, "white", WHITE);
    bundle.lights = std::vector{steadyLight({150, 0, 0}, 255, 64)};
    CHECK(redAtCentre(renderer, bundle) == 0);
}

TEST_CASE("SS 6: a cluster reached by more lights than its cap drops some and says so", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());
    uta::ubundle::Bundle bundle = litSquare("white");
    addSolidMaterial(bundle, "white", WHITE);

    // Every light reaches every cluster near the square, so a cluster holds
    // all of them or overflows. 64 is the cap (SS 4.6).
    const auto lightsNearTheSquare = [](std::size_t count) {
        std::vector<uta::ubundle::Light> lights;
        for (std::size_t i = 0; i < count; ++i)
            lights.push_back(steadyLight({60.0f, static_cast<float>(i % 8) - 4.0f, static_cast<float>(i) * 0.125f}, 4, 64));
        return lights;
    };

    bundle.lights = lightsNearTheSquare(64);
    requireOk(renderer.draw(bundle, Camera{}));
    CHECK(renderer.lastFrameStats().overflowedClusters == 0u);

    bundle.lights = lightsNearTheSquare(70);
    requireOk(renderer.draw(bundle, Camera{}));
    CHECK(renderer.lastFrameStats().overflowedClusters > 0u);
    // Lighting degrades; nothing fails -- the square is still lit.
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    CHECK(pixelAt(*pixels, WIDTH, 80, 32).r > 0);
}

TEST_CASE("a normal map tilts the lit side the way umat encodes it", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());

    // 217 is umat's byte for +0.7. The square's u runs toward +Y on screen and
    // its v runs down, so umat's +X tilts toward +Y and its +Y toward +Z.
    const auto brightness = [&](std::uint8_t normalX, std::uint8_t normalY, std::array<float, 3> lightAt) {
        uta::ubundle::Bundle bundle = litSquare("tilted");
        addNormalMappedMaterial(bundle, "tilted", WHITE, normalX, normalY);
        bundle.lights = std::vector{steadyLight(lightAt, 255, 64)};
        return redAtCentre(renderer, bundle);
    };

    const int towardPlusY = brightness(217, 128, {90, 40, 0});
    const int towardMinusY = brightness(217, 128, {90, -40, 0});
    CAPTURE(towardPlusY, towardMinusY);
    CHECK(towardPlusY > towardMinusY + 40);

    const int towardPlusZ = brightness(128, 217, {90, 0, 40});
    const int towardMinusZ = brightness(128, 217, {90, 0, -40});
    CAPTURE(towardPlusZ, towardMinusZ);
    CHECK(towardPlusZ > towardMinusZ + 40);
}

// UTA-0053's emissive bloom. A square 25 units either side of the view axis at
// x = 100 covers columns 72 to 88 and rows 24 to 40; column 93 is five pixels
// past its right edge, where only the cleared background is drawn.
TEST_CASE("UTA-0053: an emissive surface glows past its own edge", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, 25, "lamp", 0);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addEmissiveMaterial(bundle, "lamp", {0, 0, 0, 0}, WHITE);

    requireOk(renderer.draw(bundle, Camera{}));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    CHECK(int(pixelAt(*pixels, WIDTH, 80, 32).r) == 255);
    const int past = pixelAt(*pixels, WIDTH, 93, 32).r;
    CAPTURE(past);
    CHECK(past > 2);
}

TEST_CASE("UTA-0053: a brightly lit surface with no emission does not glow", "[device]") {
    // The same square lit to white by a light and emitting nothing: the pixel
    // past its edge stays the cleared background, so only emission feeds bloom.
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, 25, "white", 0);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "white", WHITE);
    bundle.lights = std::vector{steadyLight({95, 0, 0}, 255, 64)};

    requireOk(renderer.draw(bundle, Camera{}));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    // Brightly lit, as bright as the glowing square's source; it need not clip.
    CHECK(int(pixelAt(*pixels, WIDTH, 80, 32).r) > 200);
    CHECK(int(pixelAt(*pixels, WIDTH, 93, 32).r) == 0);
}
