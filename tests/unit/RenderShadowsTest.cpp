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

/// UTA-0162: a strip leader of radius byte 20 (R = 525) at `from`, its segment
/// running to `to`.
Light stripLeader(std::array<float, 3> from, std::array<float, 3> to) {
    Light light = pointLight(from, 20);
    light.strip = uta::ubundle::STRIP_LEADER;
    light.stripFrom = from;
    light.stripTo = to;
    return light;
}

} // namespace

TEST_CASE("UTA-0162 INV-9: a strip's shadow is cast from its midpoint and reaches R plus half its length",
          "[render]") {
    SECTION("tile size") {
        // R is 525 either way. As a strip its segment is 3800 long, so its
        // reach is 525 + 1900 and it asks for a bigger tile than the same light
        // as a point. UTA-0166 sizes a tile from reach, so reach is what this
        // reads; before it, the pair was told apart by which one the camera
        // could see.
        const Light strip = stripLeader({-3900, 0, 0}, {-100, 0, 0});
        Light point = strip;
        point.strip = uta::ubundle::STRIP_NONE;
        point.stripFrom = {};
        point.stripTo = {};
        CHECK(urender::shadowTileSize(strip) > urender::shadowTileSize(point));
        CHECK(urender::shadowTileSize(point) >= urender::SMALLEST_SHADOW_TILE);
    }
    SECTION("face projection") {
        // Midpoint (100, 200, 300), reach 625. A point 600 along +X from the
        // midpoint is inside it; from the location it is 700, beyond R.
        const Light strip = stripLeader({0, 200, 300}, {200, 200, 300});
        const auto m = urender::shadowViewProj(strip, 0);
        const std::array<double, 3> p{700, 200, 300};
        std::array<double, 4> clip{};
        for (int row = 0; row < 4; ++row)
            clip[row] = m[0 * 4 + row] * p[0] + m[1 * 4 + row] * p[1] + m[2 * 4 + row] * p[2] + m[3 * 4 + row];
        REQUIRE(clip[3] > 0);
        const double depth = clip[2] / clip[3];
        CHECK(depth > 0);
        CHECK(depth < 1);
    }
    SECTION("a mover near its far end redraws it") {
        ShadowPlanner planner;
        // Midpoint (900, 0, 0), reach 1025. A box at 1350 is 950 from the
        // location, beyond R, and 450 from the midpoint.
        const std::vector lights = {stripLeader({400, 0, 0}, {1400, 0, 0}), pointLight({4000, 3000, 0}, 12)};
        (void)planner.plan(lights, {});
        const std::vector<std::array<std::array<float, 3>, 2>> moved = {{{{1340, -10, -10}}, {{1360, 10, 10}}}};
        const auto plan = planner.plan(lights, moved);
        REQUIRE(plan.draws.size() == 6u);
        for (const auto& draw : plan.draws) CHECK(draw.face < 6u);
    }
}

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

TEST_CASE("UTA-0166: a light's tile size follows its reach and never the camera", "[render]") {
    const Light small = pointLight({600, 0, 0}, 20);   // R = 525
    const Light large = pointLight({600, 0, 0}, 200);  // R = 5025
    CHECK(urender::shadowTileSize(large) > urender::shadowTileSize(small));
    CHECK(urender::shadowTileSize(small) >= urender::SMALLEST_SHADOW_TILE);
    CHECK(urender::shadowTileSize(large) <= urender::LARGEST_SHADOW_TILE);

    // The same light wherever it sits relative to a camera -- in front, far
    // off, behind. Sized from its size on screen these differed, and a light
    // that lost its tile scattered no fog, so shafts and haze switched off as
    // the camera turned.
    for (const std::array<float, 3> where : {std::array<float, 3>{600, 0, 0}, {12000, 0, 0}, {-4000, 0, 0}})
        CHECK(urender::shadowTileSize(pointLight(where, 20)) == urender::shadowTileSize(small));

    // A light that shadows nothing still asks for nothing.
    Light dark = small;
    dark.effect = 12;
    dark.cone = 0;
    CHECK(urender::shadowTileSize(dark) == 0u);
}

TEST_CASE("UTA-0166: a map's worth of lights all keep a tile and are placed once", "[render]") {
    // DM-Deck16][ carries 141 shadowing lights, the most of the three maps
    // UTA-0015 fitted against; its radius bytes run from 2 to 255. Spread that
    // many over a grid and every one must hold a tile, or the fog it scatters
    // comes and goes with whichever lights won this frame.
    std::vector<Light> lights;
    for (int i = 0; i < 141; ++i) {
        const auto f = static_cast<float>(i);
        lights.push_back(pointLight({f * 130, f * 70, f * 30}, static_cast<std::uint8_t>(2 + i % 254)));
    }
    ShadowPlanner planner;
    const auto first = planner.plan(lights, {});
    CHECK(first.unshadowed == 0u);
    CHECK(first.draws.size() == lights.size() * 6);

    // Nothing moved, so the second plan places and draws nothing again.
    const auto second = planner.plan(lights, {});
    CHECK(second.unshadowed == 0u);
    CHECK(second.draws.empty());
    CHECK(second.faces.size() == first.faces.size());
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

    const auto first = planner.plan(lights, {});
    CHECK(first.draws.size() == 12u); // six faces each
    CHECK(first.unshadowed == 0u);
    CHECK(first.firstFace[0] >= 0);
    CHECK(first.faceCount[1] == 6u);

    const auto second = planner.plan(lights, {});
    CHECK(second.draws.empty());
    CHECK(second.faces.size() == 12u);
}

TEST_CASE("SS 4.8: a mover that moved inside a light's radius redraws only that light", "[render]") {
    ShadowPlanner planner;
    const std::vector lights = {pointLight({400, 0, 0}, 20), pointLight({4000, 3000, 0}, 12)};
    (void)planner.plan(lights, {});

    // A box beside the first light, far from the second.
    const std::vector<std::array<std::array<float, 3>, 2>> moved = {{{{380, -10, -10}}, {{420, 10, 10}}}};
    const auto plan = planner.plan(lights, moved);
    REQUIRE(plan.draws.size() == 6u);
    for (const auto& draw : plan.draws) CHECK(draw.face < 6u); // the first light's faces come first
}

namespace {

/// UTA-0166 sizes a tile from the light's reach, so the widest radius byte is
/// what asks for the biggest tile rather than standing close to the camera.
/// Radius 200 reaches 5025 units. These say what that buys without repeating
/// the number, so the arithmetic below follows SHADOW_UNITS_PER_TEXEL.
std::uint32_t widestTile() { return urender::shadowTileSize(pointLight({0, 0, 0}, 200)); }
std::uint32_t atlasSlots() {
    const std::uint32_t perSide = urender::SHADOW_ATLAS_SIZE / widestTile();
    return perSide * perSide;
}

Light spotLight(std::array<float, 3> location, std::uint8_t radius) {
    Light light = pointLight(location, radius);
    light.effect = 12;
    light.cone = 60;
    return light;
}

} // namespace

TEST_CASE("SS 6: lights beyond what the atlas holds are counted and the largest are kept", "[render]") {
    ShadowPlanner planner;
    // Enough spotlights at the widest radius to fill the atlas exactly, a
    // spotlight taking one tile. A small spotlight LISTED FIRST takes a corner
    // of one of those tiles if it is admitted first -- so admitting by size keeps
    // every large one and leaves the small one out, and admitting in list order
    // does the opposite. Point lights could not show this: six faces apiece
    // leave a remainder spare.
    std::vector<Light> lights = {spotLight({20000, 0, 0}, 2)};
    for (std::uint32_t i = 0; i < atlasSlots(); ++i)
        lights.push_back(spotLight({10.0f + static_cast<float>(i), 0, 0}, 200));

    const auto plan = planner.plan(lights, {});
    CHECK(plan.unshadowed == 1u);
    CHECK(plan.faceCount[0] == 0u);
    for (std::size_t i = 1; i < lights.size(); ++i) {
        CAPTURE(i);
        CHECK(plan.faceCount[i] == 1u);
    }
}

TEST_CASE("SS 6: a point light the atlas cannot give all six faces gets none", "[render]") {
    ShadowPlanner planner;
    // Point lights at the widest radius, three more than the atlas has room
    // for: six faces apiece, so the last three are turned away whole.
    const std::uint32_t fit = atlasSlots() / 6;
    std::vector<Light> lights;
    for (std::uint32_t i = 0; i < fit + 3; ++i)
        lights.push_back(pointLight({10.0f + static_cast<float>(i), 0, 0}, 200));
    const auto plan = planner.plan(lights, {});
    CHECK(plan.unshadowed == 3u);
    for (std::size_t i = 0; i < lights.size(); ++i) {
        CAPTURE(i);
        CHECK((plan.faceCount[i] == 0u || plan.faceCount[i] == 6u));
    }
}

TEST_CASE("SS 6: a point light turned away gives back the faces it had taken", "[render]") {
    ShadowPlanner planner;
    // Point lights at the widest radius, one more than the atlas holds, and a
    // spotlight. The last point takes what is left and is turned away partway
    // through its six faces; what it took goes back, so the spotlight, admitted
    // after it, still gets one.
    const std::uint32_t fit = atlasSlots() / 6;
    std::vector<Light> lights;
    for (std::uint32_t i = 0; i <= fit; ++i)
        lights.push_back(pointLight({10.0f + static_cast<float>(i), 0, 0}, 200));
    lights.push_back(spotLight({20, 0, 0}, 200));

    const auto plan = planner.plan(lights, {});
    REQUIRE(atlasSlots() % 6 != 0); // else nothing is left for the spotlight
    CHECK(plan.faceCount[fit] == 0u);
    CHECK(plan.faceCount[fit + 1] == 1u);
    CHECK(plan.unshadowed == 1u);
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
