// UTA-0256 INV-7, through the draw path: added lamps on, and off.
//
// docs/specs/UTA-0256-added-lamps.md SS 4.4. The setting is applied as the
// game applies it, by ubundle::applyAddedLamps once the bundle is read; the
// renderer has no lamp code of its own. BakeLampsTest.cpp holds the other
// half: off, a baked lamp's bundle is a bake without the lamp.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;

namespace {

constexpr std::uint32_t WIDTH = 160;
constexpr std::uint32_t HEIGHT = 64;
constexpr Rgba GREY{101, 101, 101, 255};
constexpr std::uint16_t CHART_SIZE = 12;
/// Where the lamp's holder lands: y = 50 at a depth of 100, as
/// RenderMoverTest's columnOf puts it.
constexpr std::uint32_t HOLDER_COLUMN = 96;

Config linearFrame() {
    Config config;
    config.width = WIDTH;
    config.height = HEIGHT;
    config.linearOutput = true;
    config.hazeScale = 0;
    config.tier = uta::urender::Tier::Low;
    return config;
}

uta::ubundle::MaskPair allLit(std::uint32_t light) {
    uta::ubundle::MaskPair pair;
    pair.light = light;
    pair.x = uta::ubundle::MASK_ALL_LIT;
    pair.y = uta::ubundle::MASK_ALL_LIT;
    return pair;
}

/// A grey square a hundred units ahead, lit by a dim light of its own, with a
/// chart holding an all-lit pair for each of `lights` light indices.
uta::ubundle::Bundle square(std::uint32_t lights) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, 40, "grey", 0);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "grey", GREY);
    bundle.lights = std::vector{steadyLight({60, -20, 0}, 32, 64)};
    uta::ubundle::ShadowMask mask;
    mask.texelSize = 8;
    mask.width = CHART_SIZE;
    mask.height = CHART_SIZE;
    uta::ubundle::MaskChart chart;
    chart.pairCount = lights;
    chart.width = CHART_SIZE;
    chart.height = CHART_SIZE;
    mask.charts.push_back(chart);
    for (std::uint32_t light = 0; light < lights; ++light) mask.pairs.push_back(allLit(light));
    mask.texels.assign(std::size_t{CHART_SIZE} * CHART_SIZE, 255);
    const std::size_t vertices = bundle.geometry->vertices.size();
    mask.vertexChart.assign(vertices, 0);
    mask.vertexTexel.assign(vertices, {CHART_SIZE / 2.0f, CHART_SIZE / 2.0f});
    bundle.shadowMask = std::move(mask);
    return bundle;
}

/// The square with a bright lamp near it, whose holder is a small grey
/// square off to the side.
uta::ubundle::Bundle withLamp() {
    uta::ubundle::Bundle bundle = square(2);
    uta::ubundle::AddedLamp lamp;
    lamp.light = steadyLight({60, 20, 0}, 200, 64);
    addSquare(lamp.shape, 100, 50, 0, 8, "grey", 0);
    bundle.lamps = std::vector{std::move(lamp)};
    return bundle;
}

std::vector<std::byte> frameOf(Renderer& renderer, const uta::ubundle::Bundle& bundle) {
    requireOk(renderer.draw(bundle, Camera{}));
    auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    return std::move(*pixels);
}

} // namespace

TEST_CASE("UTA-0256 INV-7: added lamps off draw the map as made", "[device]") {
    Renderer renderer = requireRenderer(linearFrame());
    const uta::ubundle::Bundle plain = square(1);
    uta::ubundle::Bundle off = withLamp();
    uta::ubundle::applyAddedLamps(off, false);
    CHECK(frameOf(renderer, off) == frameOf(renderer, plain));
}

TEST_CASE("UTA-0256 INV-7: added lamps on light the room and show their fitting", "[device]") {
    Renderer renderer = requireRenderer(linearFrame());
    const uta::ubundle::Bundle plain = square(1);
    uta::ubundle::Bundle on = withLamp();
    uta::ubundle::applyAddedLamps(on, true);
    const std::vector<std::byte> lit = frameOf(renderer, on);
    const std::vector<std::byte> made = frameOf(renderer, plain);
    CHECK(pixelAt(lit, WIDTH, WIDTH / 2, HEIGHT / 2).r > pixelAt(made, WIDTH, WIDTH / 2, HEIGHT / 2).r);
    // The holder is drawn where the plain frame shows nothing.
    CHECK(pixelAt(lit, WIDTH, HOLDER_COLUMN, HEIGHT / 2) != pixelAt(made, WIDTH, HOLDER_COLUMN, HEIGHT / 2));
}
