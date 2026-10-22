// UTA-0350: the bake's painted sky -- INV-4, which surfaces become sky and
// when the recipe is refused, and INV-5, the sun and daylight it lets in.
//
// docs/specs/UTA-0350-painted-skies.md SS 4.2.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "BakeFixture.h"

#include "core/Jobs.h"
#include "ubake/Bake.h"
#include "ubundle/Bundle.h"
#include "ubundle/Sky.h"
#include "upkg/Package.h"
#include "urecipe/Recipe.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace uta::test::bake;
using uta::JobSystem;
using uta::ubake::BakeResult;
using uta::urecipe::Recipe;
namespace detail = uta::ubake::detail;
namespace ubundle = uta::ubundle;

namespace {

using Square = std::array<std::array<float, 3>, 4>;

constexpr std::uint32_t PF_FAKE_BACKDROP = 0x80;

/// The sky picture's material id: a map texture in no group.
constexpr std::string_view SKY = "dm-fixture.sky";

/// A level square at `z`, facing up or down.
Square level(float x0, float x1, float y0, float y1, float z, bool up) {
    if (up) return {{{x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}}};
    return {{{x0, y0, z}, {x0, y1, z}, {x1, y1, z}, {x1, y0, z}}};
}

/// BakeSunTest.cpp's two floors, with the hole closed: floor 0, x and y in
/// [-60, 60], lies under a roof at z = 200 that only wears the sky's picture;
/// floor 1, x in [196, 316], under a plain roof. Neither roof is a backdrop.
/// Probe (0, 0, 128) stands under the painted roof. With `skyZone`, the level
/// also holds a SkyZoneInfo, so it has a sky of its own.
std::vector<std::byte> room(Fixture& fixture, bool skyZone = false) {
    MapBuilder& map = fixture.map;
    const std::int32_t wall = map.addTexture(TextureSpec{"Wall", "", picture(3), false});
    const std::int32_t sky = map.addTexture(TextureSpec{"Sky", "", picture(4), false});
    if (skyZone) map.addActorOfClass("Engine", "SkyZoneInfo", {vectorProperty("Location", 0.0F, 0.0F, 1000.0F)});
    map.addSurface(wall).shapeSurface(level(-60, 60, -60, 60, 20, true), {0, 0, 1});
    map.addSurface(wall).shapeSurface(level(196, 316, -60, 60, 20, true), {0, 0, 1});
    map.addSurface(sky).shapeSurface(level(-60, 60, -60, 60, 200, false), {0, 0, -1});
    map.addSurface(wall).shapeSurface(level(196, 316, -60, 60, 200, false), {0, 0, -1});
    return asByteVector(map.build());
}

/// A recipe opening `textures` as sky, with the sun overhead where `sun`.
Recipe skyRecipe(std::vector<std::string> textures, bool sun) {
    Recipe recipe;
    recipe.map = std::string(MAP_NAME);
    recipe.mapDigest = std::array<std::byte, 32>{};
    recipe.sky = uta::urecipe::Sky{.textures = std::move(textures)};
    if (sun) recipe.sun = uta::urecipe::Sun{.yaw = 0, .pitch = 16384, .hue = 0, .saturation = 255, .brightness = 255};
    return recipe;
}

uta::Result<BakeResult> bake(const Fixture& fixture, const std::vector<std::byte>& bytes, const Recipe* recipe) {
    MemoryPackages packages = memoryPackagesFor(fixture);
    JobSystem jobs(2);
    const detail::CuratedLookup nothingCurated = [](std::uint64_t) -> const uta::umat::CuratedOverride* {
        return nullptr;
    };
    const detail::FlameLookup noFlame = [](std::uint64_t) { return false; };
    const auto opened = uta::upkg::Package::open(bytes);
    REQUIRE(opened.has_value());
    return detail::bake(*opened, MAP_NAME, packages.resolver(), jobs, nothingCurated,
                        uta::umat::TEXTURE_BUDGET_BYTES, nullptr, noFlame, recipe);
}

BakeResult bakedOk(const Recipe* recipe) {
    Fixture fixture;
    const std::vector<std::byte> bytes = room(fixture);
    auto result = bake(fixture, bytes, recipe);
    if (!result.has_value()) FAIL("the bake was refused: " << result.error().message());
    return std::move(*result);
}

/// Each GEOM batch's material, and whether it is sky.
std::vector<std::pair<std::string, bool>> skyBatches(const ubundle::Bundle& bundle) {
    std::vector<std::pair<std::string, bool>> out;
    for (const ubundle::GeometryBatch& batch : bundle.geometry->batches)
        out.emplace_back(batch.material, (batch.polyFlags & PF_FAKE_BACKDROP) != 0);
    return out;
}

/// The GEOM vertex nearest `p`'s SMSK chart.
std::uint32_t chartAt(const ubundle::Bundle& bundle, const std::array<float, 3>& p) {
    const auto& vertices = bundle.geometry->vertices;
    std::size_t best = 0;
    const auto distance = [&](std::size_t k) {
        double sum = 0;
        for (std::size_t axis = 0; axis < 3; ++axis)
            sum += (vertices[k].position[axis] - p[axis]) * (vertices[k].position[axis] - p[axis]);
        return sum;
    };
    for (std::size_t v = 1; v < vertices.size(); ++v)
        if (distance(v) < distance(best)) best = v;
    return bundle.shadowMask->vertexChart[best];
}

/// Whether chart `chart` holds a pair for light `light`.
bool chartHasLight(const ubundle::ShadowMask& mask, std::uint32_t chart, std::uint32_t light) {
    const ubundle::MaskChart& c = mask.charts[chart];
    for (std::uint32_t k = c.firstPair; k < c.firstPair + c.pairCount; ++k)
        if (mask.pairs[k].light == light) return true;
    return false;
}

/// The probe at `cell`'s index; the lattice skips sky, so it differs by bake.
std::size_t probeIndex(const ubundle::Bundle& bundle, const std::array<std::int32_t, 3>& cell) {
    const auto& probes = bundle.lightProbes->probes;
    for (std::size_t p = 0; p < probes.size(); ++p)
        if (probes[p].cell == cell) return p;
    FAIL("no probe at the cell");
    return 0;
}

float sunSeenAt(const ubundle::Bundle& bundle, const std::array<std::int32_t, 3>& cell) {
    return bundle.lightProbes->sunSeen.at(probeIndex(bundle, cell));
}

/// The summed cube of the probe at `cell`.
double probeSum(const ubundle::Bundle& bundle, const std::array<std::int32_t, 3>& cell) {
    double sum = 0;
    for (const auto& face : bundle.lightProbes->probes[probeIndex(bundle, cell)].cube)
        for (const float channel : face) sum += channel;
    return sum;
}

} // namespace

TEST_CASE("UTA-0350 INV-4: the named texture's surfaces become sky and no others", "[ubake][paintedsky]") {
    const Recipe recipe = skyRecipe({std::string(SKY)}, false);
    const BakeResult plain = bakedOk(nullptr);
    const BakeResult painted = bakedOk(&recipe);

    const auto before = skyBatches(plain.bundle);
    const auto after = skyBatches(painted.bundle);
    REQUIRE(std::ranges::count(after, std::pair<std::string, bool>{std::string(SKY), true}) == 1);
    for (const auto& [material, sky] : after) {
        CAPTURE(material);
        CHECK(sky == (material == SKY));
    }
    CHECK(std::ranges::none_of(before, [](const auto& batch) { return batch.second; }));

    REQUIRE(painted.bundle.paintedSky.has_value());
    CHECK_FALSE(plain.bundle.paintedSky.has_value());
    // The recipe's defaults, linear: #3a78c8 overhead, and 20 per cent cloud.
    CHECK(painted.bundle.paintedSky->top[2] > painted.bundle.paintedSky->top[0]);
    CHECK(painted.bundle.paintedSky->top[2] < 1.0f);
    // UTA-0383: stored as a picture is, dimmed by the screen's exposure --
    // #c8 is 0.578 linear.
    CHECK(painted.bundle.paintedSky->top[2] == Catch::Approx(0.578 / ubundle::PAINTED_SKY_EXPOSURE).epsilon(0.002));
    CHECK(painted.bundle.paintedSky->clouds == 0.2f);
    CHECK(painted.bundle.paintedSky->hazeStart == 0.1f);

    // UTA-0391: the recipe's haze, as a share.
    Recipe hazed = recipe;
    hazed.sky->haze = 40;
    CHECK(bakedOk(&hazed).bundle.paintedSky->hazeStart == 0.4f);
}

TEST_CASE("UTA-0350 INV-4: a sky texture no surface wears is reported unused", "[ubake][paintedsky]") {
    const Recipe recipe = skyRecipe({std::string(SKY), "dm-fixture.unworn"}, false);
    const BakeResult painted = bakedOk(&recipe);
    CHECK(painted.recipeUnused == std::vector<std::string>{"dm-fixture.unworn"});
}

TEST_CASE("UTA-0350 INV-4: a sky on a map with a sky zone is refused", "[ubake][paintedsky]") {
    const Recipe recipe = skyRecipe({std::string(SKY)}, false);
    Fixture fixture;
    const std::vector<std::byte> bytes = room(fixture, true);
    // The sky zone is what refuses: without the recipe the same map bakes.
    REQUIRE(bake(fixture, bytes, nullptr).has_value());
    const auto refused = bake(fixture, bytes, &recipe);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code() == uta::ErrorCode::InvalidArgument);
    CHECK(refused.error().message().find("[sky]") != std::string::npos);
}

TEST_CASE("UTA-0350 INV-5: the painted roof lets the sun and daylight in", "[ubake][paintedsky]") {
    const Recipe sunOnly = [] {
        Recipe recipe = skyRecipe({}, true);
        recipe.sky.reset();
        return recipe;
    }();
    const Recipe sunAndSky = skyRecipe({std::string(SKY)}, true);
    const Recipe skyOnly = skyRecipe({std::string(SKY)}, false);
    const BakeResult shut = bakedOk(&sunOnly);
    const BakeResult open = bakedOk(&sunAndSky);
    const BakeResult daylit = bakedOk(&skyOnly);
    const BakeResult dark = bakedOk(nullptr);

    // SMSK: the floor under the painted roof has a sun pair only once it is sky.
    const auto sunOf = [](const ubundle::Bundle& bundle) { return static_cast<std::uint32_t>(bundle.lights->size()); };
    CHECK(chartHasLight(*open.bundle.shadowMask, chartAt(open.bundle, {0, 0, 20}), sunOf(open.bundle)));
    CHECK_FALSE(chartHasLight(*shut.bundle.shadowMask, chartAt(shut.bundle, {0, 0, 20}), sunOf(shut.bundle)));
    // The roofed floor sees no sun either way.
    CHECK_FALSE(chartHasLight(*open.bundle.shadowMask, chartAt(open.bundle, {256, 0, 20}), sunOf(open.bundle)));

    // LPRB: the probe under it sees the sun, and holds daylight with no sun at all.
    const std::array<std::int32_t, 3> under{0, 0, 1};
    CHECK(sunSeenAt(open.bundle, under) > 0.0f);
    CHECK(sunSeenAt(shut.bundle, under) == 0.0f);
    CHECK(probeSum(daylit.bundle, under) > probeSum(dark.bundle, under) + 0.1);
    CHECK(probeSum(open.bundle, under) > probeSum(shut.bundle, under));
}

TEST_CASE("UTA-0387 INV-2: a probe's sky share counts only rays that meet the sky", "[ubake][paintedsky]") {
    const Recipe recipe = skyRecipe({std::string(SKY)}, false);
    const BakeResult none = bakedOk(nullptr);
    const BakeResult open = bakedOk(&recipe);

    // No backdrop batch, no sky: the list stays empty.
    CHECK(none.bundle.lightProbes->skySeen.empty());
    const auto& seen = open.bundle.lightProbes->skySeen;
    REQUIRE(seen.size() == open.bundle.lightProbes->probes.size());
    // Under the painted roof, some sky. Below floor 0, whose back hides the
    // roof, none: the floor is met first, and the sideways rays meet nothing.
    CHECK(seen[probeIndex(open.bundle, {0, 0, 1})] > 0.1f);
    CHECK(seen[probeIndex(open.bundle, {0, 0, 0})] == 0.0f);
}
