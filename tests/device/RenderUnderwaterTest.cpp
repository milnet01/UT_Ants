// UTA-0215's underwater view through the draw path --
// docs/specs/UTA-0215-underwater-view.md INV-3 to INV-5.
//
// With no ROOM the camera is in zone 0, so the zone's water flag alone
// decides. Every case has a control frame differing in that flag only.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;
using uta::urender::Tier;

namespace {

constexpr double WATER_FOG_START = 800.0; // fog.glsl's
constexpr double WATER_FOG_END = 2400.0;
constexpr double WATER_KEEP = 0.72; // post.frag's

double decode(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
double encode(double l) {
    l = std::clamp(l, 0.0, 1.0);
    return l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055;
}

Config frameOf(std::uint32_t width) {
    Config config;
    config.width = width;
    config.height = 64;
    config.linearOutput = true;
    config.tier = Tier::Low;
    config.hazeScale = 0;
    return config;
}

uta::ubundle::Zone zoneOf(bool water) {
    uta::ubundle::Zone zone;
    zone.water = water ? 1 : 0;
    zone.viewFog = {0.1F, 0.2F, 0.3F};
    zone.viewFlash = -0.2F;
    return zone;
}

/// An unlit grey wall filling the view at `distance`.
uta::ubundle::Bundle wallAt(float distance, bool water) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, distance, 0, 0, 3000, "grey", PF_UNLIT);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "grey", Rgba{129, 129, 129, 255});
    bundle.zones = std::vector{zoneOf(water)};
    return bundle;
}

Rgba centre(Renderer& renderer, const uta::ubundle::Bundle& bundle, std::uint32_t width) {
    requireOk(renderer.draw(bundle, Camera{}));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    return pixelAt(*pixels, width, width / 2, 32);
}

/// The byte the original's underwater view gives a channel of linear light
/// `l` under § 4.2: its constant mix, then PlayerPawn's flash.
double tinted(double l, double fog) {
    return 255.0 * std::clamp((encode(l) * WATER_KEEP + fog) * 0.8 + fog, 0.0, 1.0);
}

} // namespace

TEST_CASE("UTA-0215 INV-3 and INV-4: under water the wall fades by its depth and takes the original's view", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(frameOf(160));
    const double grey = decode(129 / 255.0);
    for (const float distance : {1200.0F, 2000.0F}) {
        const Rgba dry = centre(renderer, wallAt(distance, false), 160);
        const Rgba wet = centre(renderer, wallAt(distance, true), 160);
        // SS 4.3: what survives the water. linearOutput: no exposure.
        const double kept = std::clamp((WATER_FOG_END - distance) / (WATER_FOG_END - WATER_FOG_START), 0.0, 1.0);
        CAPTURE(distance, dry, wet, tinted(grey * kept, 0.1), tinted(grey * kept, 0.2), tinted(grey * kept, 0.3));
        CHECK(std::abs(int(dry.r) - 129) <= 1);
        CHECK(std::abs(int(dry.b) - 129) <= 1);
        CHECK(std::abs(wet.r - tinted(grey * kept, 0.1)) <= 2.0);
        CHECK(std::abs(wet.g - tinted(grey * kept, 0.2)) <= 2.0);
        CHECK(std::abs(wet.b - tinted(grey * kept, 0.3)) <= 2.0);
    }
}

TEST_CASE("UTA-0215 INV-4: under water at the full output a far wall shows the water's colour through the tint", "[device]") {
    removeDisplay();
    // Exposure and the tone map apply here. Far off, the wall is gone and the
    // view is the original's for black: d' = (0 x KEEP + ViewFog) x 0.8 + ViewFog.
    Config config = frameOf(160);
    config.linearOutput = false;
    Renderer renderer = requireRenderer(config);
    const Rgba far = centre(renderer, wallAt(3000, true), 160);
    CAPTURE(far);
    CHECK(std::abs(far.r - 255.0 * 0.1 * 1.8) <= 2.0);
    CHECK(std::abs(far.g - 255.0 * 0.2 * 1.8) <= 2.0);
    CHECK(std::abs(far.b - 255.0 * 0.3 * 1.8) <= 2.0);
}

TEST_CASE("UTA-0215 INV-4: from under water a surface out of the water takes the tint and no fade", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(frameOf(160));
    // The camera's zone 0 is water; the wall's vertices are in dry zone 1.
    uta::ubundle::Bundle bundle = wallAt(1600, true);
    for (auto& vertex : bundle.geometry->vertices) vertex.zone = 1;
    bundle.zones->push_back(zoneOf(false));
    const Rgba wet = centre(renderer, bundle, 160);
    const double grey = decode(129 / 255.0);
    CAPTURE(wet, tinted(grey, 0.1), tinted(grey, 0.2), tinted(grey, 0.3));
    CHECK(std::abs(wet.r - tinted(grey, 0.1)) <= 2.0);
    CHECK(std::abs(wet.g - tinted(grey, 0.2)) <= 2.0);
    CHECK(std::abs(wet.b - tinted(grey, 0.3)) <= 2.0);
}

TEST_CASE("UTA-0215 INV-5: under water the view wobbles and out of it it does not", "[device]") {
    removeDisplay();
    constexpr std::uint32_t WIDTH = 640;
    Renderer renderer = requireRenderer(frameOf(WIDTH));
    // A black square on the left half of the view, white beyond: one edge.
    const auto edgeAt = [&](bool water, double seconds) {
        uta::ubundle::Geometry geometry;
        addSquare(geometry, 100, -3000, 0, 3000, "black", PF_UNLIT);
        addSquare(geometry, 101, 0, 0, 3000, "white", PF_UNLIT);
        uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
        addSolidMaterial(bundle, "black", Rgba{1, 1, 1, 255});
        addSolidMaterial(bundle, "white", Rgba{255, 255, 255, 255});
        bundle.zones = std::vector{zoneOf(water)};
        renderer.pinLightSeconds(seconds);
        requireOk(renderer.draw(bundle, Camera{}));
        const auto pixels = renderer.readback();
        if (!pixels.has_value()) FAIL(pixels.error().message());
        // Whichever side the white half lands on, the edge is where the row
        // first differs from its left end.
        const bool leftWhite = pixelAt(*pixels, WIDTH, 0, 32).r > 128;
        for (std::uint32_t x = 1; x < WIDTH; ++x)
            if ((pixelAt(*pixels, WIDTH, x, 32).r > 128) != leftWhite) return int(x);
        return -1;
    };
    const int dryEarly = edgeAt(false, 0.0);
    const int dryLate = edgeAt(false, 1.0);
    const int wetEarly = edgeAt(true, 0.0);
    const int wetLate = edgeAt(true, 1.0);
    CAPTURE(dryEarly, dryLate, wetEarly, wetLate);
    CHECK(dryEarly == dryLate);
    CHECK(wetEarly != wetLate);
}

namespace {

/// A lit grey wall whose vertices are in zone 1, `water` or not, seen from a
/// dry zone 0 -- so the view's own tint and wobble stay off -- at `seconds`.
std::vector<std::byte> litWallFrame(Renderer& renderer, bool water, double seconds) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 200, 0, 0, 3000, "grey", 0);
    for (auto& vertex : geometry.vertices) vertex.zone = 1;
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "grey", Rgba{129, 129, 129, 255});
    uta::ubundle::Zone dry;
    dry.brightness = 128;
    uta::ubundle::Zone wall = dry;
    wall.water = water ? 1 : 0;
    bundle.zones = std::vector{dry, wall};
    renderer.pinLightSeconds(seconds);
    requireOk(renderer.draw(bundle, Camera{}));
    auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    return std::move(*pixels);
}

/// The largest change in any channel between two frames.
int largestChange(const std::vector<std::byte>& a, const std::vector<std::byte>& b) {
    int largest = 0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
        largest = std::max(largest, std::abs(int(a[i]) - int(b[i])));
    return largest;
}

Config causticFrame(Tier tier) {
    Config config = frameOf(160);
    config.tier = tier;
    return config;
}

} // namespace

TEST_CASE("UTA-0215: light ripples across a wall in a water zone from Medium and nowhere else", "[device]") {
    removeDisplay();
    Renderer medium = requireRenderer(causticFrame(Tier::Medium));
    const int wet = largestChange(litWallFrame(medium, true, 0.0), litWallFrame(medium, true, 1.0));
    const int dry = largestChange(litWallFrame(medium, false, 0.0), litWallFrame(medium, false, 1.0));
    Renderer low = requireRenderer(causticFrame(Tier::Low));
    const int lowWet = largestChange(litWallFrame(low, true, 0.0), litWallFrame(low, true, 1.0));
    CAPTURE(wet, dry, lowWet);
    CHECK(wet > 10);
    CHECK(dry == 0);
    CHECK(lowWet == 0);
}

namespace {

/// The centre's green with a shadowed light above the view or none, a black
/// unlit wall far off, the camera's zone 0 `water` or not, at `tier`, with no
/// haze -- so any light in the air is the water's.
int shaftGreen(Tier tier, bool water, bool light) {
    Config config = frameOf(160);
    config.tier = tier;
    Renderer renderer = requireRenderer(config);
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 1000, 0, 0, 3000, "black", PF_UNLIT);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "black", Rgba{1, 1, 1, 255});
    bundle.zones = std::vector{zoneOf(water)};
    if (light) bundle.lights = std::vector{steadyLight({60, 0, 40}, 255, 64)};
    renderer.pinLightSeconds(0);
    requireOk(renderer.draw(bundle, Camera{}));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    return pixelAt(*pixels, 160, 80, 32).g;
}

} // namespace

TEST_CASE("UTA-0215: under water a light scatters in the water from Medium and nowhere else", "[device]") {
    removeDisplay();
    const int wetLit = shaftGreen(Tier::Medium, true, true);
    const int wetDark = shaftGreen(Tier::Medium, true, false);
    const int dryLit = shaftGreen(Tier::Medium, false, true);
    const int dryDark = shaftGreen(Tier::Medium, false, false);
    const int lowLit = shaftGreen(Tier::Low, true, true);
    const int lowDark = shaftGreen(Tier::Low, true, false);
    CAPTURE(wetLit, wetDark, dryLit, dryDark, lowLit, lowDark);
    CHECK(wetLit >= wetDark + 10);
    CHECK(dryLit == dryDark);
    CHECK(lowLit == lowDark);
}

namespace {

/// How many pixels of a 160x64 frame stand out from a plain far wall: the
/// camera's zone 0 `water` or not, at `tier`. The wall fills the view, so
/// anything brighter than its own colour is a speck in front of it.
int specks(Tier tier, bool water) {
    Config config = frameOf(160);
    config.tier = tier;
    Renderer renderer = requireRenderer(config);
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 2000, 0, 0, 6000, "black", PF_UNLIT);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "black", Rgba{1, 1, 1, 255});
    bundle.zones = std::vector{zoneOf(water)};
    renderer.pinLightSeconds(0);
    requireOk(renderer.draw(bundle, Camera{}));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    const Rgba plain = pixelAt(*pixels, 160, 0, 0);
    int count = 0;
    for (std::uint32_t y = 0; y < 64; ++y)
        for (std::uint32_t x = 0; x < 160; ++x)
            if (pixelAt(*pixels, 160, x, y).g > plain.g + 6) ++count;
    return count;
}

} // namespace

TEST_CASE("UTA-0215: specks drift in the water around the eye from Medium and nowhere else", "[device]") {
    removeDisplay();
    const int wet = specks(Tier::Medium, true);
    const int dry = specks(Tier::Medium, false);
    const int low = specks(Tier::Low, true);
    CAPTURE(wet, dry, low);
    CHECK(wet > 0);
    CHECK(dry == 0);
    CHECK(low == 0);
}

