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

constexpr double WATER_VISIBILITY = 600.0; // fog.glsl's

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

/// The byte UT99's tint gives a channel of linear light `l` under § 4.2.
double tinted(double l, double fog) { return 255.0 * std::clamp(encode(l) * 0.8 + fog, 0.0, 1.0); }

} // namespace

TEST_CASE("UTA-0215 INV-3 and INV-4: under water the wall is absorbed by its depth and takes UT99's tint", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(frameOf(160));
    const double grey = decode(129 / 255.0);
    for (const float distance : {100.0F, 200.0F}) {
        const Rgba dry = centre(renderer, wallAt(distance, false), 160);
        const Rgba wet = centre(renderer, wallAt(distance, true), 160);
        const double absorbed = grey * std::exp(-distance / WATER_VISIBILITY);
        CAPTURE(distance, dry, wet, tinted(absorbed, 0.1), tinted(absorbed, 0.2), tinted(absorbed, 0.3));
        CHECK(std::abs(int(dry.r) - 129) <= 1);
        CHECK(std::abs(int(dry.b) - 129) <= 1);
        CHECK(std::abs(wet.r - tinted(absorbed, 0.1)) <= 2.0);
        CHECK(std::abs(wet.g - tinted(absorbed, 0.2)) <= 2.0);
        CHECK(std::abs(wet.b - tinted(absorbed, 0.3)) <= 2.0);
    }
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
