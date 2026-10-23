// UTA-0396: a recipe's hidden brush leaves GEOM, and the opening it cuts in
// another surface's plane is covered as that surface --
// docs/specs/UTA-0109-map-geometry.md SS 4.6.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "BakeFixture.h"

#include "core/Jobs.h"
#include "ubake/Bake.h"
#include "ubundle/Bundle.h"
#include "upkg/Package.h"
#include "urecipe/Recipe.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <set>
#include <string>
#include <vector>

using namespace uta::test::bake;
using uta::JobSystem;
using uta::ubake::BakeResult;
using uta::urecipe::HiddenBrush;
using uta::urecipe::Recipe;
namespace detail = uta::ubake::detail;

namespace {

using Square = std::array<std::array<float, 3>, 4>;

/// "Sea0" owns the floor square at z = 0, as its polygon 3. "Pit1" owns two
/// upright squares through it: one across x at y = 32, from z = -32 to 32,
/// whose edges cross the plane, and one across y at x = 32, from z = -32 to
/// 0, whose top corners lie on it. "Wall2" owns a square wholly above it.
std::vector<std::byte> pitMap(Fixture& fixture) {
    MapBuilder& map = fixture.map;
    const std::int32_t stone = map.addTexture(TextureSpec{"Stone", "", picture(3), false});
    const std::int32_t brushClass = map.importClass("Engine", "Brush");
    map.addActor("Sea0", brushClass);
    map.addActor("Pit1", brushClass);
    map.addActor("Wall2", brushClass);
    map.addSurface(stone).ownSurface(0, 3);
    map.addSurface(stone)
        .shapeSurface(Square{{{16, 32, -32}, {48, 32, -32}, {48, 32, 32}, {16, 32, 32}}}, {0, -1, 0})
        .ownSurface(1, 0);
    map.addSurface(stone)
        .shapeSurface(Square{{{32, 16, -32}, {32, 48, -32}, {32, 48, 0}, {32, 16, 0}}}, {1, 0, 0})
        .ownSurface(1, 1);
    map.addSurface(stone)
        .shapeSurface(Square{{{0, 0, 40}, {64, 0, 40}, {64, 0, 60}, {0, 0, 60}}}, {0, -1, 0})
        .ownSurface(2, 0);
    return asByteVector(map.build());
}

uta::Result<BakeResult> baked(std::vector<HiddenBrush> hidden) {
    Fixture fixture;
    const std::vector<std::byte> bytes = pitMap(fixture);
    MemoryPackages packages = memoryPackagesFor(fixture);
    JobSystem jobs(2);
    const detail::CuratedLookup nothingCurated = [](std::uint64_t) -> const uta::umat::CuratedOverride* {
        return nullptr;
    };
    const detail::FlameLookup noFlame = [](std::uint64_t) { return false; };
    Recipe recipe;
    recipe.map = std::string(MAP_NAME);
    recipe.mapDigest = std::array<std::byte, 32>{};
    recipe.hidden = std::move(hidden);
    const auto opened = uta::upkg::Package::open(bytes);
    REQUIRE(opened.has_value());
    return detail::bake(*opened, MAP_NAME, packages.resolver(), jobs, nothingCurated,
                        uta::umat::TEXTURE_BUDGET_BYTES, nullptr, noFlame, &recipe);
}

using Point = std::array<float, 3>;

std::set<Point> pointsOf(const BakeResult& result) {
    std::set<Point> points;
    for (const auto& vertex : result.bundle.geometry->vertices) points.insert(vertex.position);
    return points;
}

} // namespace

TEST_CASE("UTA-0396: a hidden brush leaves GEOM and its opening is covered", "[ubake][hide]") {
    const auto before = baked({});
    REQUIRE(before.has_value());
    const std::set<Point> was = pointsOf(*before);
    REQUIRE(was.contains(Point{16, 32, -32})); // the pit is drawn without the recipe

    const auto after = baked({HiddenBrush{.name = "pit", .brush = "Pit1", .coverBrush = "Sea0", .coverPolygon = 3}});
    if (!after.has_value()) FAIL("the bake was refused: " << after.error().message());
    const std::set<Point> now = pointsOf(*after);
    // The floor and the wall above stay; no corner of the pit does; the cover
    // is the pit's cross-section at z = 0, an edge crossing and two corners.
    const std::set<Point> expected = {
        {0, 0, 0},   {64, 0, 0},  {64, 64, 0}, {0, 64, 0},  // the floor
        {0, 0, 40},  {64, 0, 40}, {64, 0, 60}, {0, 0, 60},  // the wall above
        {16, 32, 0}, {48, 32, 0}, {32, 16, 0}, {32, 48, 0}, // the cover
    };
    CHECK(now == expected);
    // Two triangles each: the floor, the wall and the cover.
    CHECK(after->bundle.geometry->indices.size() == 3 * (2 + 2 + 2));
}

TEST_CASE("UTA-0396: every [hide] the map cannot honour is refused by name", "[ubake][hide]") {
    struct Refusal {
        const char* what;
        HiddenBrush hide;
        const char* says;
    };
    const std::vector<Refusal> refusals = {
        {"a brush not in the map", {"a", "Pit9", "Sea0", 3}, "Pit9 is not in the map"},
        {"a brush with no surface", {"a", "Model0", "Sea0", 3}, "Model0 gives no surface"},
        {"a cover brush not in the map", {"a", "Pit1", "Sea9", 3}, "Sea9 is not in the map"},
        {"a cover polygon the brush lacks", {"a", "Pit1", "Sea0", 4}, "Sea0 has no drawn polygon 4"},
        {"a cover the hidden brush owns", {"a", "Pit1", "Pit1", 0}, "the cover is a surface of the brush"},
        {"a brush that does not reach the plane", {"a", "Wall2", "Sea0", 3}, "Wall2 does not cross"},
    };
    for (const Refusal& refusal : refusals) {
        DYNAMIC_SECTION(refusal.what) {
            const auto result = baked({refusal.hide});
            REQUIRE_FALSE(result.has_value());
            const std::string message(result.error().message());
            INFO("message: " << message);
            CHECK(message.find("recipe [hide a]: ") != std::string::npos);
            CHECK(message.find(refusal.says) != std::string::npos);
        }
    }
}
