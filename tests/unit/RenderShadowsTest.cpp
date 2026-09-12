// Shadow-map planning -- docs/specs/UTA-0014-vulkan-draw-path.md SS 4.8 and
// SS 6. SS 4.12 grades atlas tile allocation in the device-free tier.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "urender/Shadows.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vector>

using Catch::Matchers::WithinAbs;
using uta::ubundle::Light;
using uta::urender::AtlasTile;
using uta::urender::Camera;
using uta::urender::ShadowAtlas;
using uta::urender::ShadowPlanner;
namespace urender = uta::urender;

namespace {

bool overlap(const AtlasTile& a, const AtlasTile& b) {
    return a.x < b.x + b.size && b.x < a.x + a.size && a.y < b.y + b.size && b.y < a.y + a.size;
}

Light pointLight(std::array<float, 3> location, std::uint8_t radius) {
    Light light;
    light.type = 1;
    light.brightness = 255;
    light.saturation = 255;
    light.radius = radius;
    light.location = location;
    return light;
}

} // namespace

TEST_CASE("SS 4.8: atlas tiles lie inside the atlas and never overlap", "[render]") {
    ShadowAtlas atlas;
    std::vector<AtlasTile> tiles;
    // A mix of sizes, so halving is exercised as well as whole tiles.
    for (const std::uint32_t size : {1024u, 64u, 256u, 1024u, 512u, 64u, 128u, 256u, 1024u, 64u}) {
        const auto tile = atlas.allocate(size);
        REQUIRE(tile.has_value());
        CHECK(tile->size == size);
        CHECK(tile->x + tile->size <= urender::SHADOW_ATLAS_SIZE);
        CHECK(tile->y + tile->size <= urender::SHADOW_ATLAS_SIZE);
        CHECK(tile->x % size == 0u);
        CHECK(tile->y % size == 0u);
        tiles.push_back(*tile);
    }
    for (std::size_t a = 0; a < tiles.size(); ++a)
        for (std::size_t b = a + 1; b < tiles.size(); ++b) {
            CAPTURE(a, b);
            CHECK_FALSE(overlap(tiles[a], tiles[b]));
        }
}

TEST_CASE("SS 4.8: a full atlas refuses and a cleared one gives again", "[render]") {
    ShadowAtlas atlas;
    const std::uint32_t whole = (urender::SHADOW_ATLAS_SIZE / urender::LARGEST_SHADOW_TILE)
                                * (urender::SHADOW_ATLAS_SIZE / urender::LARGEST_SHADOW_TILE);
    for (std::uint32_t i = 0; i < whole; ++i) REQUIRE(atlas.allocate(urender::LARGEST_SHADOW_TILE).has_value());
    CHECK_FALSE(atlas.allocate(urender::SMALLEST_SHADOW_TILE).has_value());
    CHECK_FALSE(atlas.allocate(100).has_value()); // not a power of two
    atlas.clear();
    CHECK(atlas.allocate(urender::SMALLEST_SHADOW_TILE).has_value());
}

TEST_CASE("SS 4.8: a point light takes six tiles and a spotlight one", "[render]") {
    Light point = pointLight({0, 0, 0}, 20);
    CHECK(urender::shadowFacesOf(point) == 6u);
    Light spot = point;
    spot.effect = 12;
    spot.cone = 64;
    CHECK(urender::shadowFacesOf(spot) == 1u);
    spot.cone = 0; // lights nothing, so casts nothing
    CHECK(urender::shadowFacesOf(spot) == 0u);
}

TEST_CASE("a light's tile size follows its size on screen and is zero out of view", "[render]") {
    const Camera camera; // at the origin looking along +X
    const Light nearLight = pointLight({600, 0, 0}, 20);
    const Light farLight = pointLight({12000, 0, 0}, 20);
    const Light behind = pointLight({-4000, 0, 0}, 20);
    const std::uint32_t nearSize = urender::shadowTileSize(camera, 1280, 720, nearLight);
    const std::uint32_t farSize = urender::shadowTileSize(camera, 1280, 720, farLight);
    CHECK(nearSize > farSize);
    CHECK(farSize >= urender::SMALLEST_SHADOW_TILE);
    CHECK(urender::shadowTileSize(camera, 1280, 720, behind) == 0u);
}

TEST_CASE("a point light's face projects the point along its axis to the tile's centre", "[render]") {
    const Light light = pointLight({100, 200, 300}, 20); // radius 525
    const std::array<std::array<double, 3>, 6> along = {{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1},
                                                         {0, 0, -1}}};
    for (std::uint32_t face = 0; face < 6; ++face) {
        CAPTURE(face);
        const auto m = urender::shadowViewProj(light, face);
        const std::array<double, 3> p{100 + along[face][0] * 200, 200 + along[face][1] * 200,
                                      300 + along[face][2] * 200};
        std::array<double, 4> clip{};
        for (int row = 0; row < 4; ++row)
            clip[row] = m[0 * 4 + row] * p[0] + m[1 * 4 + row] * p[1] + m[2 * 4 + row] * p[2] + m[3 * 4 + row];
        REQUIRE(clip[3] > 0);
        CHECK_THAT(clip[0] / clip[3], WithinAbs(0, 1e-4));
        CHECK_THAT(clip[1] / clip[3], WithinAbs(0, 1e-4));
        const double depth = clip[2] / clip[3];
        CHECK(depth > 0);
        CHECK(depth < 1);
    }
}

TEST_CASE("SS 4.8: a still camera over still lights draws each tile once", "[render]") {
    ShadowPlanner planner;
    const std::vector lights = {pointLight({400, 0, 0}, 20), pointLight({600, 100, 0}, 12)};
    const Camera camera;

    const auto first = planner.plan(lights, camera, 1280, 720, {});
    CHECK(first.draws.size() == 12u); // six faces each
    CHECK(first.unshadowed == 0u);
    CHECK(first.firstFace[0] >= 0);
    CHECK(first.faceCount[1] == 6u);

    const auto second = planner.plan(lights, camera, 1280, 720, {});
    CHECK(second.draws.empty());
    CHECK(second.faces.size() == 12u);
}

TEST_CASE("SS 4.8: a mover that moved inside a light's radius redraws only that light", "[render]") {
    ShadowPlanner planner;
    const std::vector lights = {pointLight({400, 0, 0}, 20), pointLight({4000, 3000, 0}, 12)};
    const Camera camera;
    (void)planner.plan(lights, camera, 1280, 720, {});

    // A box beside the first light, far from the second.
    const std::vector<std::array<std::array<float, 3>, 2>> moved = {{{{380, -10, -10}}, {{420, 10, 10}}}};
    const auto plan = planner.plan(lights, camera, 1280, 720, moved);
    REQUIRE(plan.draws.size() == 6u);
    for (const auto& draw : plan.draws) CHECK(draw.face < 6u); // the first light's faces come first
}

TEST_CASE("SS 6: lights beyond what the atlas holds are counted and the largest are kept", "[render]") {
    ShadowPlanner planner;
    const Camera camera;
    // Sixteen spotlights each wanting the largest tile fill the atlas exactly,
    // a spotlight taking one tile. A small spotlight LISTED FIRST takes a corner
    // of one of those tiles if it is admitted first -- so admitting by size keeps
    // all sixteen large ones and leaves the small one out, and admitting in list
    // order does the opposite. Point lights could not show this: six faces
    // apiece always leave four tiles spare.
    const auto spot = [](std::array<float, 3> location, std::uint8_t radius) {
        Light light = pointLight(location, radius);
        light.effect = 12;
        light.cone = 60;
        return light;
    };
    std::vector<Light> lights = {spot({20000, 0, 0}, 2)};
    for (int i = 0; i < 16; ++i) lights.push_back(spot({10.0f + i, 0, 0}, 200));

    const auto plan = planner.plan(lights, camera, 1280, 720, {});
    CHECK(plan.unshadowed == 1u);
    CHECK(plan.faceCount[0] == 0u);
    for (std::size_t i = 1; i < lights.size(); ++i) {
        CAPTURE(i);
        CHECK(plan.faceCount[i] == 1u);
    }
}

TEST_CASE("SS 6: a point light the atlas cannot give all six faces gets none", "[render]") {
    ShadowPlanner planner;
    const Camera camera;
    // Five point lights wanting the largest tile: two fit in sixteen tiles.
    std::vector<Light> lights;
    for (int i = 0; i < 5; ++i) lights.push_back(pointLight({10.0f + i, 0, 0}, 200));
    const auto plan = planner.plan(lights, camera, 1280, 720, {});
    CHECK(plan.unshadowed == 3u);
    for (std::size_t i = 0; i < lights.size(); ++i) {
        CAPTURE(i);
        CHECK((plan.faceCount[i] == 0u || plan.faceCount[i] == 6u));
    }
}

TEST_CASE("SS 4.8: filling the atlas with the smallest tiles gives every cell to exactly one tile", "[render]") {
    // Every halving is exercised down to the last quarter, so a split that hands
    // out a quarter it also keeps shows up as a cell given twice.
    ShadowAtlas atlas;
    const std::uint32_t side = urender::SHADOW_ATLAS_SIZE / urender::SMALLEST_SHADOW_TILE;
    std::vector<bool> taken(side * side, false);
    std::uint32_t count = 0;
    std::uint32_t givenTwice = 0;
    while (const auto tile = atlas.allocate(urender::SMALLEST_SHADOW_TILE)) {
        const std::uint32_t cell = (tile->y / urender::SMALLEST_SHADOW_TILE) * side + tile->x / urender::SMALLEST_SHADOW_TILE;
        REQUIRE(cell < taken.size());
        if (taken[cell]) ++givenTwice;
        taken[cell] = true;
        REQUIRE(++count <= side * side);
    }
    CHECK(givenTwice == 0u);
    CHECK(count == side * side);
}
