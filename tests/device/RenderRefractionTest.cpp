// UTA-0273: see-through water -- docs/specs/UTA-0273-see-through-water.md
// SS 4, INV-1 to INV-5.
//
// THE SCENES STAND ON END. The camera looks along +X, as the fixture's squares
// face it, so the "floor" is a square further along +X than the water, and
// the water's depth is the gap between the two planes.
//
// EVERYTHING IS UNLIT, and there are no probes and no sky: the water's
// reflection is black, so a pixel is the water's picture and what is behind it.
//
// EACH FRAME IS A FRESH RENDERER'S FIRST, as RenderWaterTest's are.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

using namespace uta::test::render;
using uta::ubundle::LiquidKind;
using uta::ubundle::LiquidLook;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;
using uta::urender::Tier;

namespace {

constexpr std::uint32_t SIZE = 128;
constexpr double PINNED = 1.0;
constexpr float WATER_AT = 64; // the water's plane, units from the eye
constexpr float WIDE = 4096;   // half a square that fills the view at every depth used here

/// A liquid look of `kind` with ripple `amplitude`, its ramp one colour, so a
/// Wave's colour does not change with its ripple.
LiquidLook look(LiquidKind kind, float amplitude) {
    LiquidLook look;
    look.kind = kind;
    look.amplitude = amplitude;
    look.frequency = 8;
    look.pan = {128, 128};
    look.size = {16, 16};
    for (auto& entry : look.ramp) entry = {0.1f, 0.1f, 0.1f};
    return look;
}

/// The colour frame of `bundle` from the default camera, from a fresh renderer at `tier`.
std::vector<std::byte> frameOf(const uta::ubundle::Bundle& bundle, Tier tier) {
    Config config;
    config.width = SIZE;
    config.height = SIZE;
    config.linearOutput = true;
    config.tier = tier;
    config.hazeScale = 0;
    Renderer renderer = requireRenderer(config);
    renderer.pinLightSeconds(PINNED);
    requireOk(renderer.draw(bundle, Camera{}));
    auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    return std::move(*pixels);
}

Rgba centreOf(const std::vector<std::byte>& frame) { return pixelAt(frame, SIZE, SIZE / 2, SIZE / 2); }

/// The largest difference between `a` and `b` in any colour channel.
int channelGap(const Rgba& a, const Rgba& b) {
    return std::max({std::abs(int(a.r) - int(b.r)), std::abs(int(a.g) - int(b.g)), std::abs(int(a.b) - int(b.b))});
}

/// The water: a translucent, unlit square of material "pool" at WATER_AT, filling the view.
void addWater(uta::ubundle::Geometry& geometry) {
    addSquare(geometry, WATER_AT, 0, 0, WIDE, "pool", PF_TRANSLUCENT | PF_UNLIT);
}

/// Material "pool": a solid `picture` with `liquid`.
void addPool(uta::ubundle::Bundle& bundle, const Rgba& picture, const LiquidLook& liquid) {
    addSolidMaterial(bundle, "pool", picture);
    bundle.materials->back().liquid = liquid;
}

// -- INV-1 and INV-2: a light floor `depth` units behind dark, still water ----

uta::ubundle::Bundle poolOver(float depth) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, WATER_AT + depth, 0, 0, WIDE, "floor", PF_UNLIT);
    addWater(geometry);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "floor", Rgba{201, 201, 201, 255});
    addPool(bundle, Rgba{21, 21, 21, 255}, look(LiquidKind::Wet, 0));
    return bundle;
}

// -- INV-3: a floor sloping through the water's plane -------------------------

/// A floor that runs through the water's plane at a slope of a half: one side
/// of the view it stands in front of the water, the other side behind. With
/// `water`, white still water.
uta::ubundle::Bundle shore(bool water) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, WATER_AT, 0, 0, WIDE, "floor", PF_UNLIT);
    for (std::size_t i = geometry.vertices.size() - 4; i < geometry.vertices.size(); ++i) {
        auto& p = geometry.vertices[i].position;
        p[0] = WATER_AT + p[2] / 2; // deeper as the view goes up
    }
    if (water) addWater(geometry);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "floor", Rgba{101, 101, 101, 255});
    if (water) addPool(bundle, Rgba{255, 255, 255, 255}, look(LiquidKind::Wet, 0));
    return bundle;
}

// -- INV-4: a checker floor behind a Wave ------------------------------------

constexpr float CELL = 4; // half a checker square, units

uta::ubundle::Bundle waveOverChecker(float amplitude) {
    uta::ubundle::Geometry geometry;
    for (int j = -16; j < 16; ++j)
        for (int i = -16; i < 16; ++i)
            addSquare(geometry, WATER_AT + 64, float(2 * i + 1) * CELL * 2, float(2 * j + 1) * CELL * 2, CELL * 2,
                      ((i + j) & 1) != 0 ? "white" : "black", PF_UNLIT);
    addWater(geometry);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "white", Rgba{255, 255, 255, 255});
    addSolidMaterial(bundle, "black", Rgba{1, 1, 1, 255});
    addPool(bundle, Rgba{1, 1, 1, 255}, look(LiquidKind::Wave, amplitude));
    return bundle;
}

/// How many pixels of `a` and `b` differ by more than 8 levels in some channel.
int changedPixels(const std::vector<std::byte>& a, const std::vector<std::byte>& b) {
    int changed = 0;
    for (std::uint32_t y = 0; y < SIZE; ++y)
        for (std::uint32_t x = 0; x < SIZE; ++x)
            if (channelGap(pixelAt(a, SIZE, x, y), pixelAt(b, SIZE, x, y)) > 8) ++changed;
    return changed;
}

// -- INV-5: a red pillar in front of the water --------------------------------

constexpr float PILLAR_AT = WATER_AT / 2;
constexpr float PILLAR_HALF = 6;

/// A blue floor behind a strongly rippling Wave and, with `pillar`, a red
/// square nearer than the water, at the centre of the view.
uta::ubundle::Bundle pillarBeforePool(bool pillar) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, WATER_AT + 64, 0, 0, WIDE, "floor", PF_UNLIT);
    if (pillar) addSquare(geometry, PILLAR_AT, 0, 0, PILLAR_HALF, "pillar", PF_UNLIT);
    addWater(geometry);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "floor", Rgba{1, 1, 255, 255});
    if (pillar) addSolidMaterial(bundle, "pillar", Rgba{255, 1, 1, 255});
    addPool(bundle, Rgba{1, 1, 1, 255}, look(LiquidKind::Wave, 128));
    return bundle;
}

} // namespace

TEST_CASE("UTA-0273 INV-1: below Ultra water does not show its depth", "[device][refraction]") {
    removeDisplay();
    const Rgba shallow = centreOf(frameOf(poolOver(16), Tier::High));
    const Rgba deep = centreOf(frameOf(poolOver(256), Tier::High));
    CAPTURE(shallow, deep);
    CHECK(shallow == deep);
}

TEST_CASE("UTA-0273 INV-2: deeper water is darker and red goes first", "[device][refraction]") {
    removeDisplay();
    const Rgba shallow = centreOf(frameOf(poolOver(16), Tier::Ultra));
    const Rgba deep = centreOf(frameOf(poolOver(256), Tier::Ultra));
    CAPTURE(shallow, deep);
    CHECK(deep.r < shallow.r);
    CHECK(deep.g < shallow.g);
    CHECK(deep.b < shallow.b);
    CHECK(int(shallow.r) - int(deep.r) > int(shallow.b) - int(deep.b));
}

TEST_CASE("UTA-0273 INV-3: the shore is soft", "[device][refraction]") {
    removeDisplay();
    // At High the white water covers the floor wherever the floor lies behind
    // it, so in each column the first such pixel is the water's edge.
    const std::vector<std::byte> high = frameOf(shore(true), Tier::High);
    const std::vector<std::byte> ultra = frameOf(shore(true), Tier::Ultra);
    const std::vector<std::byte> dry = frameOf(shore(false), Tier::Ultra);
    const auto wet = [&](std::uint32_t x, std::uint32_t y) {
        return channelGap(pixelAt(high, SIZE, x, y), pixelAt(dry, SIZE, x, y)) > 32;
    };
    int edges = 0;
    for (std::uint32_t x = SIZE / 4; x < SIZE * 3 / 4; ++x)
        for (std::uint32_t y = 1; y < SIZE; ++y) {
            if (wet(x, y) == wet(x, y - 1)) continue;
            const std::uint32_t inside = wet(x, y) ? y : y - 1;
            const Rgba seen = pixelAt(ultra, SIZE, x, inside);
            const Rgba floor = pixelAt(dry, SIZE, x, inside);
            CAPTURE(x, inside, seen, floor);
            CHECK(channelGap(seen, floor) <= 4);
            ++edges;
            break;
        }
    CHECK(edges == int(SIZE / 2)); // every column read found its edge
}

TEST_CASE("UTA-0273 INV-4: a ripple bends what is behind", "[device][refraction]") {
    removeDisplay();
    const int ultra =
        changedPixels(frameOf(waveOverChecker(128), Tier::Ultra), frameOf(waveOverChecker(0), Tier::Ultra));
    const int high = changedPixels(frameOf(waveOverChecker(128), Tier::High), frameOf(waveOverChecker(0), Tier::High));
    CAPTURE(ultra, high);
    CHECK(ultra > high + int(SIZE)); // more than a row's worth more
}

TEST_CASE("UTA-0273 INV-5: nothing in front of the water shows through it", "[device][refraction]") {
    removeDisplay();
    const std::vector<std::byte> with = frameOf(pillarBeforePool(true), Tier::Ultra);
    const std::vector<std::byte> without = frameOf(pillarBeforePool(false), Tier::Ultra);
    // The pillar spans PILLAR_HALF / PILLAR_AT of the half-view either side of
    // the centre (a 90-degree view); its edges' neighbours are within 16 pixels.
    const int half = int(float(SIZE) / 2 * PILLAR_HALF / PILLAR_AT);
    int pillarPixels = 0, beside = 0;
    for (std::uint32_t y = 0; y < SIZE; ++y)
        for (std::uint32_t x = 0; x < SIZE; ++x) {
            const Rgba p = pixelAt(with, SIZE, x, y);
            if (p.r > 128 && p.b < 64) { // the pillar itself
                ++pillarPixels;
                continue;
            }
            const int dx = std::abs(int(x) - int(SIZE / 2)), dy = std::abs(int(y) - int(SIZE / 2));
            if (std::max(dx, dy) > half + 16) continue;
            ++beside;
            const Rgba bare = pixelAt(without, SIZE, x, y);
            CAPTURE(x, y, p, bare);
            CHECK(int(p.r) <= int(bare.r) + 4);
        }
    CHECK(pillarPixels > 0);
    CHECK(beside > 0);
}
