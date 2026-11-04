// UTA-0293: grass -- docs/specs/UTA-0293-grass.md SS 4.4 to SS 4.6, INV-8 to
// INV-13.
//
// THE SCENE: a green floor 40 units below the eye, from 40 to 300 units ahead,
// seen along +X by a default Camera. Its far edge lands just below the image's
// middle row, so the rows above that edge show nothing but what stands on the
// floor: a blade at the edge reaches 20 units up, above it.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;
using uta::urender::Tier;

namespace {

constexpr std::uint32_t SIZE = 128;
constexpr float FLOOR_Z = -40;
constexpr float NEAR_X = 40;
constexpr float FAR_X = 300;
constexpr float HALF_WIDTH = 150;
/// The floor's far edge, 40 below the eye at 300 ahead, is row 64 + 64 x 40 /
/// 300, 72.5. Every row above 71 shows only what stands above the floor.
constexpr std::uint32_t ABOVE_FLOOR = 71;
/// Every channel odd, as bc7Solid needs.
constexpr Rgba GREEN{41, 121, 31, 255};

/// A floor of material 0 facing up at FLOOR_Z, from `nearX` to `farX` ahead.
uta::ubundle::Geometry floorFrom(float nearX, float farX) {
    uta::ubundle::Geometry geometry;
    for (const auto& [x, y] : {std::pair{nearX, -HALF_WIDTH}, std::pair{farX, -HALF_WIDTH}, std::pair{farX, HALF_WIDTH},
                               std::pair{nearX, HALF_WIDTH}}) {
        uta::ubundle::GeometryVertex vertex;
        vertex.position = {x, y, FLOOR_Z};
        vertex.normal = {0, 0, 1};
        vertex.u = x / 64;
        vertex.v = y / 64;
        geometry.vertices.push_back(vertex);
    }
    geometry.indices = {0, 1, 2, 0, 2, 3};
    geometry.batches = {uta::ubundle::GeometryBatch{"grass", PF_TWO_SIDED, 0, 6, {}}};
    return geometry;
}

/// Clumps every 16 units over [x0, x1] by [-HALF_WIDTH, HALF_WIDTH], the even
/// seeds first as the baker lays them, in one cell; `texelOf` gives each its
/// mask texel from its y.
uta::ubundle::Grass grassOver(float x0, float x1, std::uint32_t chart = uta::ubundle::MASK_NO_CHART,
                              float (*texelOf)(float) = nullptr) {
    std::vector<uta::ubundle::GrassClump> clumps;
    std::uint32_t seed = 0;
    for (float x = x0; x <= x1; x += 16)
        for (float y = -HALF_WIDTH + 8; y < HALF_WIDTH; y += 16) {
            uta::ubundle::GrassClump clump;
            clump.base = {x, y, FLOOR_Z};
            clump.material = 0;
            clump.uv = {x / 64, y / 64};
            clump.maskChart = chart;
            if (texelOf != nullptr) clump.maskTexel = {texelOf(y), 6.0f};
            clump.seed = seed++;
            clumps.push_back(clump);
        }
    std::ranges::stable_partition(clumps, [](const uta::ubundle::GrassClump& c) { return c.seed % 2 == 0; });
    uta::ubundle::Grass grass;
    grass.cells = {uta::ubundle::GrassCell{0, static_cast<std::uint32_t>(clumps.size()),
                                           {x0 - 20, -HALF_WIDTH - 20, FLOOR_Z}, {x1 + 20, HALF_WIDTH + 20, FLOOR_Z + 20}}};
    grass.clumps = std::move(clumps);
    return grass;
}

/// The floor from NEAR_X to FAR_X lit by even probes, with grass over it.
uta::ubundle::Bundle meadow() {
    uta::ubundle::Bundle bundle = bundleOf(floorFrom(NEAR_X, FAR_X));
    addSolidMaterial(bundle, "grass", GREEN);
    addEvenProbes(bundle, {0, -200, -100}, {400, 200, 100}, 0.5f);
    bundle.grass = grassOver(NEAR_X + 8, FAR_X - 8);
    return bundle;
}

Config configAt(Tier tier) {
    Config config;
    config.width = SIZE;
    config.height = SIZE;
    config.linearOutput = true;
    config.tier = tier;
    config.hazeScale = 0; // UTA-0015: no haze over the far floor
    return config;
}

std::vector<std::byte> drawOnce(Renderer& renderer, const uta::ubundle::Bundle& bundle, double seconds = 0) {
    renderer.pinLightSeconds(seconds);
    requireOk(renderer.draw(bundle, Camera{}));
    auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    return std::move(*pixels);
}

/// The colour frame from a fresh renderer.
std::vector<std::byte> frameOf(const uta::ubundle::Bundle& bundle, Tier tier, double seconds = 0) {
    Renderer renderer = requireRenderer(configAt(tier));
    return drawOnce(renderer, bundle, seconds);
}

/// How many pixels in rows [0, rows) differ between two frames.
std::size_t differing(const std::vector<std::byte>& a, const std::vector<std::byte>& b, std::uint32_t rows = SIZE) {
    REQUIRE(a.size() == b.size());
    std::size_t count = 0;
    for (std::uint32_t y = 0; y < rows; ++y)
        for (std::uint32_t x = 0; x < SIZE; ++x) count += pixelAt(a, SIZE, x, y) != pixelAt(b, SIZE, x, y) ? 1 : 0;
    return count;
}

/// The green channel summed over rows [first, last) and columns [x0, x1).
std::uint64_t greenIn(const std::vector<std::byte>& image, std::uint32_t first, std::uint32_t last, std::uint32_t x0,
                      std::uint32_t x1) {
    std::uint64_t sum = 0;
    for (std::uint32_t y = first; y < last; ++y)
        for (std::uint32_t x = x0; x < x1; ++x) sum += pixelAt(image, SIZE, x, y).g;
    return sum;
}

} // namespace

TEST_CASE("UTA-0293 INV-8: below High grass draws nothing", "[device][grass]") {
    removeDisplay();
    uta::ubundle::Bundle bare = meadow();
    bare.grass.reset();
    CHECK(differing(frameOf(meadow(), Tier::Medium), frameOf(bare, Tier::Medium)) == 0);
}

TEST_CASE("UTA-0293 INV-9: at High grass stands above the floor", "[device][grass]") {
    removeDisplay();
    uta::ubundle::Bundle bare = meadow();
    bare.grass.reset();
    const std::size_t above = differing(frameOf(meadow(), Tier::High), frameOf(bare, Tier::High), ABOVE_FLOOR);
    INFO("pixels above the floor's edge that grass changed: " << above);
    // Blades at the far edge are under a pixel wide at this size; 17 changed
    // on Mesa's CPU driver (2026-11-04). Flat grass changes none.
    CHECK(above > 5);
}

TEST_CASE("UTA-0293 INV-10: grass takes the ground's shadow", "[device][grass]") {
    removeDisplay();
    // One light above the floor and no probes, so the blades are lit by it
    // alone; its one pair stores a chart whose left half is shadow (0) and
    // right half lit (255). Each clump reads the half its `texelOf` names.
    const auto litBundle = [](float (*texelOf)(float)) {
        uta::ubundle::Bundle bundle = bundleOf(floorFrom(NEAR_X, FAR_X));
        addSolidMaterial(bundle, "grass", GREEN);
        bundle.lights = std::vector<uta::ubundle::Light>{steadyLight({200, 0, 40}, 255, 255)};
        uta::ubundle::ShadowMask mask;
        mask.texelSize = 8;
        mask.width = 12;
        mask.height = 12;
        uta::ubundle::MaskChart chart;
        chart.pairCount = 1;
        chart.width = 12;
        chart.height = 12;
        mask.charts.push_back(chart);
        mask.pairs.push_back(uta::ubundle::MaskPair{0, 0, 0, 0, {}});
        for (std::uint32_t y = 0; y < 12; ++y)
            for (std::uint32_t x = 0; x < 12; ++x) mask.texels.push_back(x < 6 ? 0 : 255);
        mask.vertexChart.assign(4, 0);
        mask.vertexTexel.assign(4, {9.5f, 6.0f});
        bundle.shadowMask = std::move(mask);
        bundle.grass = grassOver(NEAR_X + 8, FAR_X - 8, 0, texelOf);
        return bundle;
    };
    // Negative y in the shadow, positive lit; then the other way round.
    const auto leftDark = frameOf(litBundle([](float y) { return y < 0 ? 2.5f : 9.5f; }), Tier::High);
    const auto rightDark = frameOf(litBundle([](float y) { return y < 0 ? 9.5f : 2.5f; }), Tier::High);
    const std::uint64_t aLeft = greenIn(leftDark, 60, ABOVE_FLOOR, 0, SIZE / 2);
    const std::uint64_t aRight = greenIn(leftDark, 60, ABOVE_FLOOR, SIZE / 2, SIZE);
    const std::uint64_t bLeft = greenIn(rightDark, 60, ABOVE_FLOOR, 0, SIZE / 2);
    const std::uint64_t bRight = greenIn(rightDark, 60, ABOVE_FLOOR, SIZE / 2, SIZE);
    CAPTURE(aLeft, aRight, bLeft, bRight);
    // Whichever screen side negative y lands on, it is darker in the first
    // frame than in the second, and the other side the reverse.
    REQUIRE(aLeft + aRight > 0);
    CHECK(std::min(aLeft, bLeft) * 2 < std::max(aLeft, bLeft));
    CHECK(std::min(aRight, bRight) * 2 < std::max(aRight, bRight));
    CHECK((aLeft < bLeft) != (aRight < bRight));
}

TEST_CASE("UTA-0293 INV-11: grass ends at range", "[device][grass]") {
    removeDisplay();
    // A floor reaching 2000 ahead, its grass from 1700 on: past High's
    // GRASS_END of 1536 from the eye.
    uta::ubundle::Bundle far = bundleOf(floorFrom(NEAR_X, 2000));
    addSolidMaterial(far, "grass", GREEN);
    addEvenProbes(far, {0, -200, -100}, {2100, 200, 100}, 0.5f);
    far.grass = grassOver(1700, 1800);
    uta::ubundle::Bundle bare = far;
    bare.grass.reset();
    CHECK(differing(frameOf(far, Tier::High), frameOf(bare, Tier::High)) == 0);
}

TEST_CASE("UTA-0293 INV-12: grass sways", "[device][grass]") {
    removeDisplay();
    const uta::ubundle::Bundle bundle = meadow();
    const auto atZero = frameOf(bundle, Tier::High, 0);
    CHECK(differing(atZero, frameOf(bundle, Tier::High, 0)) == 0);
    const std::size_t moved = differing(atZero, frameOf(bundle, Tier::High, 1));
    INFO("pixels a second of wind moved: " << moved);
    CHECK(moved > 20);
}

TEST_CASE("UTA-0293 INV-13: a changed VEGE re-uploads", "[device][grass]") {
    removeDisplay();
    // One bundle edited in place, so only shapeOf's sample of VEGE can tell
    // the renderer the grass changed: a new bundle is a new address.
    Renderer renderer = requireRenderer(configAt(Tier::High));
    uta::ubundle::Bundle bundle = meadow();
    const auto before = drawOnce(renderer, bundle);
    REQUIRE(differing(before, drawOnce(renderer, bundle)) == 0);
    for (uta::ubundle::GrassClump& clump : bundle.grass->clumps) clump.base[1] += 7;
    CHECK(differing(before, drawOnce(renderer, bundle)) > 20);
}
