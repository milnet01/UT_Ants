// The baked shadow mask through the draw path --
// docs/specs/UTA-0326-baked-shadow-mask.md SS 4.5 and INV-8 to INV-10.
//
// Each SMSK here is written by hand over a room with no occluder, so the
// shadow map alone lights every surface fully and only a renderer that reads
// the mask can produce what a case asserts (SS 5's preamble). The bake's own
// values are INV-2's. Each case draws its bundles one after another in one
// renderer, which also exercises shapeOf's sample of SMSK.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <vector>

using namespace uta::test::render;
using uta::ubundle::MaskPair;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;

namespace {

constexpr std::uint32_t WIDTH = 160;
constexpr std::uint32_t HEIGHT = 64;
constexpr Rgba GREY{101, 101, 101, 255};

/// The chart's rectangle, border included, and the atlas holding only it.
constexpr std::uint16_t CHART_SIZE = 12;

Config linearFrame() {
    Config config;
    config.width = WIDTH;
    config.height = HEIGHT;
    config.linearOutput = true;
    // UTA-0015: no haze, which would add to the pixel this compares.
    config.hazeScale = 0;
    // UTA-0180: and Low, whose lit surfaces have no large-scale variation.
    config.tier = uta::urender::Tier::Low;
    return config;
}

/// The column a point `y` units across, `depth` units ahead, lands in -- as
/// RenderMoverTest's columnOf, which is this at a depth of 100.
std::uint32_t columnOf(float y, float depth) { return static_cast<std::uint32_t>(80.0f + y * 32.0f / depth); }

/// One chart holding `pairs`, each texel `value`, laid over the vertices by
/// `cover`.
uta::ubundle::ShadowMask maskOf(std::vector<MaskPair> pairs, std::uint8_t value) {
    uta::ubundle::ShadowMask mask;
    mask.texelSize = 8;
    mask.width = CHART_SIZE;
    mask.height = CHART_SIZE;
    uta::ubundle::MaskChart chart;
    chart.pairCount = static_cast<std::uint32_t>(pairs.size());
    chart.width = CHART_SIZE;
    chart.height = CHART_SIZE;
    mask.charts.push_back(chart);
    mask.pairs = std::move(pairs);
    mask.texels.assign(std::size_t{CHART_SIZE} * CHART_SIZE, value);
    return mask;
}

/// `mask` over every vertex of `bundle`'s GEOM, each reading the chart's middle texel.
void cover(uta::ubundle::Bundle& bundle, uta::ubundle::ShadowMask mask) {
    const std::size_t vertices = bundle.geometry->vertices.size();
    mask.vertexChart.assign(vertices, 0);
    mask.vertexTexel.assign(vertices, {CHART_SIZE / 2.0f, CHART_SIZE / 2.0f});
    bundle.shadowMask = std::move(mask);
}

/// A pair storing texels at the atlas's corner.
MaskPair stored(std::uint32_t light, bool moverReach = false) {
    MaskPair pair;
    pair.light = light;
    pair.moverReach = moverReach ? 1 : 0;
    return pair;
}

MaskPair allLit(std::uint32_t light) {
    MaskPair pair;
    pair.light = light;
    pair.x = uta::ubundle::MASK_ALL_LIT;
    pair.y = uta::ubundle::MASK_ALL_LIT;
    return pair;
}

/// A grey square facing the camera a hundred units ahead, lit by `lights`
/// alone, with `mask` over it when given.
uta::ubundle::Bundle square(std::vector<uta::ubundle::Light> lights,
                            std::optional<uta::ubundle::ShadowMask> mask = {}) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, 40, "grey", 0);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "grey", GREY);
    bundle.lights = std::move(lights);
    if (mask) cover(bundle, std::move(*mask));
    return bundle;
}

Rgba colourAt(Renderer& renderer, const uta::ubundle::Bundle& bundle, std::uint32_t column,
              const Camera& camera = {}) {
    requireOk(renderer.draw(bundle, camera));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    return pixelAt(*pixels, WIDTH, column, HEIGHT / 2);
}

std::uint8_t redAtCentre(Renderer& renderer, const uta::ubundle::Bundle& bundle) {
    return colourAt(renderer, bundle, WIDTH / 2).r;
}

} // namespace

TEST_CASE("UTA-0326 INV-8: a level surface is shadowed by its pair's texels", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());
    const uta::ubundle::Light light = steadyLight({95, 0, 0}, 128, 1);
    uta::ubundle::Light switchedOff = light;
    switchedOff.brightness = 0;

    const std::uint8_t off = redAtCentre(renderer, square({switchedOff}));
    // One bundle edited in place, so only shapeOf's sample of the texels can
    // tell the renderer the mask changed: a new bundle is a new address.
    uta::ubundle::Bundle masked = square({light}, maskOf({stored(0)}, 0));
    const std::uint8_t dark = redAtCentre(renderer, masked);
    std::ranges::fill(masked.shadowMask->texels, std::uint8_t{128});
    const std::uint8_t half = redAtCentre(renderer, masked);
    const std::uint8_t lit = redAtCentre(renderer, square({light}, maskOf({allLit(0)}, 0)));
    CAPTURE(int(off), int(dark), int(half), int(lit));
    CHECK(std::abs(int(dark) - int(off)) <= 1);
    CHECK(half > off);
    CHECK(half < lit);
}

TEST_CASE("UTA-0326 INV-9: a moverReach pair reads the shadow map alone", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());
    // The level a hundred and twenty units ahead, and a mover twenty units in
    // front of it and thirty to the left, both lit by one light.
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 120, 0, 0, 60, "grey", 0);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "grey", GREY);
    bundle.lights = std::vector{steadyLight({60, 0, 0}, 128, 64)};
    uta::ubundle::MoverShape mover;
    mover.exportIndex = 7;
    mover.location = {100, -30, 0};
    mover.postScale = {1, 1, 1};
    addSquare(mover.geometry, 0, 0, 0, 20, "grey", 0);
    bundle.movers = std::vector{mover};
    const std::uint32_t open = columnOf(30, 120);
    const std::uint32_t onMover = columnOf(-30, 100);

    const Rgba openBefore = colourAt(renderer, bundle, open);
    const Rgba moverBefore = colourAt(renderer, bundle, onMover);
    cover(bundle, maskOf({stored(0, true)}, 0));
    const Rgba openAfter = colourAt(renderer, bundle, open);
    // An unmarked pair of 0, which a mover vertex carrying the chart would read.
    cover(bundle, maskOf({stored(0)}, 0));
    const Rgba moverAfter = colourAt(renderer, bundle, onMover);
    CAPTURE(openBefore, openAfter, moverBefore, moverAfter);
    // A mask of 0 read anywhere would leave that pixel black.
    CHECK(openBefore.r > 0);
    CHECK(std::abs(int(openAfter.r) - int(openBefore.r)) <= 1);
    CHECK(moverBefore.r > 0);
    CHECK(std::abs(int(moverAfter.r) - int(moverBefore.r)) <= 1);
}

TEST_CASE("UTA-0326 INV-10: a level fragment lights from its chart's pairs alone", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());
    const uta::ubundle::Light first = steadyLight({95, 0, 0}, 64, 1);
    const uta::ubundle::Light second = steadyLight({95, 0, 1}, 64, 1);
    uta::ubundle::Light secondOff = second;
    secondOff.brightness = 0;

    const std::uint8_t both = redAtCentre(renderer, square({first, second}));
    const std::uint8_t firstOnly = redAtCentre(renderer, square({first, secondOff}));
    const std::uint8_t masked = redAtCentre(renderer, square({first, second}, maskOf({allLit(0)}, 0)));
    CAPTURE(int(both), int(firstOnly), int(masked));
    // The second light shows when nothing leaves it out.
    CHECK(both > firstOnly);
    CHECK(std::abs(int(masked) - int(firstOnly)) <= 1);
}

TEST_CASE("UTA-0326 SS 4.5: a masked level surface still takes the flashlight", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());
    // No light of the level's own, so no pair names one: the flashlight is all
    // there is, and no chart lists it.
    Camera camera;
    camera.flashlight = true;
    const std::uint8_t plain = colourAt(renderer, square({}), WIDTH / 2, camera).r;
    const std::uint8_t masked = colourAt(renderer, square({}, maskOf({}, 0)), WIDTH / 2, camera).r;
    CAPTURE(int(plain), int(masked));
    CHECK(plain > 0);
    CHECK(std::abs(int(masked) - int(plain)) <= 1);
}
