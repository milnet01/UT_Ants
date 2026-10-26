// UTA-0326's bake: the shadow mask's pairs, texels and packing.
//
// docs/specs/UTA-0326-baked-shadow-mask.md SS 4.3, SS 4.4 and INV-2 to INV-7.
//
// The level is built by hand as GEOM would hold it: each rectangle is one
// polygon of four vertices, fanned from its first, as BakeOcclusionTest's are.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "ubake/ShadowMask.h"

#include "BakeFixture.h"
#include "core/Jobs.h"
#include "support/UnrealPackageBuilder.h"
#include "ubake/Bake.h"
#include "ubundle/Bundle.h"
#include "umat/Library.h"
#include "umat/Material.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

using uta::JobSystem;
using uta::ubake::bakeShadowMask;
using uta::ubake::Cutout;
using uta::ubake::Cutouts;
using uta::ubundle::Bundle;
using uta::ubundle::Geometry;
using uta::ubundle::GeometryBatch;
using uta::ubundle::GeometryVertex;
using uta::ubundle::Light;
using uta::ubundle::MASK_ALL_LIT;
using uta::ubundle::MASK_NO_CHART;
using uta::ubundle::MaskPair;
using uta::ubundle::ShadowMask;

namespace {

using P = std::array<float, 3>;

constexpr std::uint32_t PF_MASKED = 0x00000002u;
constexpr std::uint32_t PF_TRANSLUCENT = 0x00000004u;
constexpr std::uint32_t PF_NOT_SOLID = 0x00000008u;
constexpr std::uint32_t PF_MODULATED = 0x00000040u;
constexpr std::uint32_t PF_FAKE_BACKDROP = 0x00000080u;
constexpr std::uint32_t PF_INVISIBLE = 0x00000001u;
constexpr std::uint32_t PF_PORTAL = 0x04000000u;
constexpr std::uint32_t PF_SPECIAL_LIT = 0x00100000u;

/// Triangles gathered by batch -- material, then flags -- the key order GEOM keeps.
struct Scene {
    Geometry geometry;
    std::map<std::pair<std::string, std::uint32_t>, std::vector<std::uint32_t>> batches;
};

/// The rectangle a, b, c, d -- d is a + (d - a), c is b + (d - a) -- facing `n`,
/// u running a to b and v a to d over one repeat.
std::uint32_t addQuad(Scene& scene, P a, P b, P c, P d, P n, std::uint32_t flags = 0, std::string material = "") {
    const auto first = static_cast<std::uint32_t>(scene.geometry.vertices.size());
    const float us[4] = {0, 1, 1, 0}, vs[4] = {0, 0, 1, 1};
    int k = 0;
    for (const P& p : {a, b, c, d}) {
        GeometryVertex vertex;
        vertex.position = p;
        vertex.normal = n;
        vertex.u = us[k];
        vertex.v = vs[k];
        ++k;
        scene.geometry.vertices.push_back(vertex);
    }
    std::vector<std::uint32_t>& indices = scene.batches[{material, flags}];
    for (std::uint32_t j = 1; j + 1 < 4; ++j) indices.insert(indices.end(), {first, first + j, first + j + 1});
    return first;
}

Geometry finish(Scene scene) {
    Geometry& g = scene.geometry;
    for (const auto& [key, indices] : scene.batches) {
        g.batches.push_back(GeometryBatch{key.first, key.second, static_cast<std::uint32_t>(g.indices.size()),
                                          static_cast<std::uint32_t>(indices.size())});
        g.indices.insert(g.indices.end(), indices.begin(), indices.end());
    }
    return g;
}

/// A box of `low` to `high`, its five faces but the bottom facing out. Returns
/// each face's first vertex: -x, +x, -y, +y, top.
std::array<std::uint32_t, 5> addBox(Scene& scene, P low, P high) {
    const float x0 = low[0], y0 = low[1], z0 = low[2], x1 = high[0], y1 = high[1], z1 = high[2];
    return {addQuad(scene, {x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, {-1, 0, 0}),
            addQuad(scene, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}, {1, 0, 0}),
            addQuad(scene, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, {0, -1, 0}),
            addQuad(scene, {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}, {0, 1, 0}),
            addQuad(scene, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, {0, 0, 1})};
}

/// A steady point light reaching 525 units.
Light pointLight(P at, std::uint32_t exportIndex = 0) {
    Light light;
    light.exportIndex = exportIndex;
    light.location = at;
    light.type = 1;
    light.brightness = 200;
    light.radius = 20;
    return light;
}

Bundle bundleOf(Geometry geometry, std::vector<Light> lights) {
    Bundle bundle;
    bundle.geometry = std::move(geometry);
    bundle.lights = std::move(lights);
    return bundle;
}

/// The pair of `light` on the chart holding vertex `first`, or none.
const MaskPair* pairOf(const ShadowMask& mask, std::uint32_t first, std::uint32_t light) {
    const std::uint32_t chart = mask.vertexChart[first];
    if (chart == MASK_NO_CHART) return nullptr;
    const auto& record = mask.charts[chart];
    for (std::uint32_t k = record.firstPair; k < record.firstPair + record.pairCount; ++k)
        if (mask.pairs[k].light == light) return &mask.pairs[k];
    return nullptr;
}

/// The stored value of `light` at world point `p` of the rectangle whose first
/// vertex is `first`: 255 for an all-lit pair, -1 for no pair.
int valueAt(const Geometry& g, const ShadowMask& mask, std::uint32_t first, std::uint32_t light, P p) {
    const MaskPair* pair = pairOf(mask, first, light);
    if (pair == nullptr) return -1;
    if (pair->x == MASK_ALL_LIT) return 255;
    const auto at = [&](std::uint32_t k) { return g.vertices[first + k].position; };
    const auto sub = [](P a, P b) { return P{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
    const auto dot = [](P a, P b) { return double(a[0]) * b[0] + double(a[1]) * b[1] + double(a[2]) * b[2]; };
    const P e1 = sub(at(1), at(0)), e2 = sub(at(3), at(0)), d = sub(p, at(0));
    const double s = dot(d, e1) / dot(e1, e1), t = dot(d, e2) / dot(e2, e2);
    const auto& t0 = mask.vertexTexel[first];
    const auto& t1 = mask.vertexTexel[first + 1];
    const auto& t3 = mask.vertexTexel[first + 3];
    const double x = t0[0] + s * (t1[0] - t0[0]) + t * (t3[0] - t0[0]);
    const double y = t0[1] + s * (t1[1] - t0[1]) + t * (t3[1] - t0[1]);
    const auto column = static_cast<std::size_t>(std::floor(x)), row = static_cast<std::size_t>(std::floor(y));
    return mask.texels[(pair->y + row) * mask.width + pair->x + column];
}

/// INV-2's room: a 512-unit floor and four 256-unit walls facing in, a light
/// overhead, and a pillar east of it.
struct PillarRoom {
    Geometry geometry;
    std::uint32_t floor = 0, westWall = 0;
    std::array<std::uint32_t, 5> pillar{};
};

PillarRoom pillarRoom() {
    Scene scene;
    PillarRoom room;
    room.floor = addQuad(scene, {0, 0, 0}, {512, 0, 0}, {512, 512, 0}, {0, 512, 0}, {0, 0, 1});
    addQuad(scene, {0, 0, 0}, {512, 0, 0}, {512, 0, 256}, {0, 0, 256}, {0, 1, 0});
    addQuad(scene, {0, 512, 0}, {512, 512, 0}, {512, 512, 256}, {0, 512, 256}, {0, -1, 0});
    room.westWall = addQuad(scene, {0, 0, 0}, {0, 512, 0}, {0, 512, 256}, {0, 0, 256}, {1, 0, 0});
    addQuad(scene, {512, 0, 0}, {512, 512, 0}, {512, 512, 256}, {512, 0, 256}, {-1, 0, 0});
    room.pillar = addBox(scene, {320, 236, 0}, {360, 276, 150});
    room.geometry = finish(std::move(scene));
    return room;
}

const P LIGHT_AT{256, 256, 200};

} // namespace

TEST_CASE("INV-2: a floor texel in the pillar's shadow stores 0 and one in the open 255", "[ubake][shadowmask]") {
    const PillarRoom room = pillarRoom();
    JobSystem jobs(2);
    const auto mask = bakeShadowMask(bundleOf(room.geometry, {pointLight(LIGHT_AT)}), jobs);
    REQUIRE(mask.has_value());
    // East of the pillar, the segment to the light passes through it at z 73.
    CHECK(valueAt(room.geometry, *mask, room.floor, 0, {420, 256, 0}) == 0);
    CHECK(valueAt(room.geometry, *mask, room.floor, 0, {100, 100, 0}) == 255);

    // Across the shadow's edge, some texel sees part of the light.
    const MaskPair* floor = pairOf(*mask, room.floor, 0);
    REQUIRE(floor != nullptr);
    REQUIRE(floor->x != MASK_ALL_LIT);
    const auto& chart = mask->charts[mask->vertexChart[room.floor]];
    bool between = false;
    for (std::uint32_t j = 0; j < chart.height; ++j)
        for (std::uint32_t i = 0; i < chart.width; ++i) {
            const int value = mask->texels[(floor->y + j) * mask->width + floor->x + i];
            between = between || (value > 0 && value < 255);
        }
    CHECK(between);
}

TEST_CASE("INV-2: a wall the pillar cannot shadow has an all-lit pair", "[ubake][shadowmask]") {
    const PillarRoom room = pillarRoom();
    JobSystem jobs(2);
    const auto mask = bakeShadowMask(bundleOf(room.geometry, {pointLight(LIGHT_AT)}), jobs);
    REQUIRE(mask.has_value());
    const MaskPair* wall = pairOf(*mask, room.westWall, 0);
    REQUIRE(wall != nullptr);
    CHECK(wall->x == MASK_ALL_LIT);
    CHECK(wall->y == MASK_ALL_LIT);
}

TEST_CASE("UTA-0343: a texel inside a polygon whose edge steps back is baked where it lies", "[ubake][shadowmask]") {
    // MH-()mG-DeathcourseV2's wall c_wal19d runs one corner 0.25 units past
    // the next along its bottom edge, then back. An inside test that reads the
    // outline as convex put every point outside, and moved each onto its
    // nearest edge: those nearest the corner were baked from behind a post the
    // wall's own middle sees past, a black wedge with nothing to cast it.
    Scene scene;
    const auto wall = static_cast<std::uint32_t>(scene.geometry.vertices.size());
    for (const P& p : {P{0, 0, 0}, P{256.25f, 0, 0}, P{256, 0, 0}, P{256, 0, 232}, P{0, 0, 232}}) {
        GeometryVertex vertex;
        vertex.position = p;
        vertex.normal = {0, 1, 0};
        scene.geometry.vertices.push_back(vertex);
    }
    std::vector<std::uint32_t>& indices = scene.batches[{"", 0}];
    for (std::uint32_t j = 1; j + 1 < 5; ++j) indices.insert(indices.end(), {wall, wall + j, wall + j + 1});
    // A post just off the wall, between the light and the corner edge only.
    addBox(scene, {240, 4, 0}, {250, 12, 232});
    const Geometry geometry = finish(std::move(scene));

    JobSystem jobs(2);
    const auto mask = bakeShadowMask(bundleOf(geometry, {pointLight({128, 200, 116})}), jobs);
    REQUIRE(mask.has_value());
    const MaskPair* pair = pairOf(*mask, wall, 0);
    REQUIRE(pair != nullptr);
    if (pair->x == MASK_ALL_LIT) return; // every texel lit: nothing was moved behind the post
    // The wall's texel is affine in x and z: along x from its first corner to
    // its second, along z from its first to its last.
    const auto texelOf = [&](float x, float z, int axis) {
        const auto& t0 = mask->vertexTexel[wall];
        const auto& t1 = mask->vertexTexel[wall + 1];
        const auto& t4 = mask->vertexTexel[wall + 4];
        return t0[axis] + x / 256.25 * (t1[axis] - t0[axis]) + z / 232.0 * (t4[axis] - t0[axis]);
    };
    // 56 units from the corner edge and 116 from the floor and the top, so
    // the corner edge is its nearest; its own ray to the light passes the post.
    const auto column = static_cast<std::size_t>(std::floor(texelOf(200, 116, 0)));
    const auto row = static_cast<std::size_t>(std::floor(texelOf(200, 116, 1)));
    CHECK(int(mask->texels[(pair->y + row) * mask->width + pair->x + column]) == 255);
}

TEST_CASE("INV-2: the pillar's face turned from the light has no pair for it", "[ubake][shadowmask]") {
    const PillarRoom room = pillarRoom();
    JobSystem jobs(2);
    const auto mask = bakeShadowMask(bundleOf(room.geometry, {pointLight(LIGHT_AT)}), jobs);
    REQUIRE(mask.has_value());
    // +x faces away from a light at x 256; -x and the top face it.
    CHECK(pairOf(*mask, room.pillar[1], 0) == nullptr);
    CHECK(mask->charts[mask->vertexChart[room.pillar[1]]].pairCount == 0);
    CHECK(pairOf(*mask, room.pillar[0], 0) != nullptr);
    CHECK(pairOf(*mask, room.pillar[4], 0) != nullptr);
}

namespace {

constexpr std::uint32_t PF_TWO_SIDED = 0x00000100u;
const P BELOW{256, 256, 20};

/// A 112-unit solid sheet at z 100 facing up under `flags` and `material`, and
/// optionally a 32-unit solid square at z 60 under its middle. Returns the
/// geometry and the sheet's first vertex.
std::pair<Geometry, std::uint32_t> sheetScene(std::uint32_t flags, const std::string& material, bool blocker) {
    Scene scene;
    const std::uint32_t sheet =
        addQuad(scene, {200, 200, 100}, {312, 200, 100}, {312, 312, 100}, {200, 312, 100}, {0, 0, 1}, flags, material);
    if (blocker) addQuad(scene, {240, 240, 60}, {272, 240, 60}, {272, 272, 60}, {240, 272, 60}, {0, 0, -1});
    return {finish(std::move(scene)), sheet};
}

} // namespace

TEST_CASE("SS 4.3: a two-sided sheet lit only from behind is lit from that side", "[ubake][shadowmask]") {
    // scene.frag turns a two-sided surface's normal to the viewer, so a light
    // behind it lights the side it is on. DM-Crane's sky clouds are lit so.
    JobSystem jobs(2);
    const auto [geometry, sheet] = sheetScene(PF_TWO_SIDED, "", false);
    const auto mask = bakeShadowMask(bundleOf(geometry, {pointLight(BELOW)}), jobs);
    REQUIRE(mask.has_value());
    // Solid, so a ray lifted to the far side would meet the sheet itself.
    CHECK(valueAt(geometry, *mask, sheet, 0, {256, 256, 100}) == 255);
}

TEST_CASE("SS 4.3: what lies behind a two-sided sheet still shadows it", "[ubake][shadowmask]") {
    JobSystem jobs(2);
    const auto [geometry, sheet] = sheetScene(PF_TWO_SIDED, "", true);
    const auto mask = bakeShadowMask(bundleOf(geometry, {pointLight(BELOW)}), jobs);
    REQUIRE(mask.has_value());
    CHECK(valueAt(geometry, *mask, sheet, 0, {256, 256, 100}) == 0);
    // From this corner the segment to the light passes the square at x 233.
    CHECK(valueAt(geometry, *mask, sheet, 0, {210, 210, 100}) == 255);
}

TEST_CASE("SS 4.3: a strip shadows a texel by the share of its segment it sees", "[ubake][shadowmask]") {
    // UTA-0381: a block over one end of a strip hides that end, and only that
    // end, from the floor under it. Lit from the nearest point alone the floor
    // went black there, and a straight edge cast a curved shadow.
    Scene scene;
    const std::uint32_t floor = addQuad(scene, {0, 0, 0}, {512, 0, 0}, {512, 512, 0}, {0, 512, 0}, {0, 0, 1});
    addBox(scene, {90, 246, 100}, {110, 266, 110});
    const Geometry geometry = finish(std::move(scene));
    Light strip = pointLight({100, 256, 200});
    strip.effect = 17; // LE_Cylinder, as a real row's lights are
    strip.strip = uta::ubundle::STRIP_LEADER;
    strip.stripFrom = {100, 256, 200};
    strip.stripTo = {400, 256, 200};
    JobSystem jobs(2);
    const auto mask = bakeShadowMask(bundleOf(geometry, {strip}), jobs);
    REQUIRE(mask.has_value());
    const int under = valueAt(geometry, *mask, floor, 0, {100, 256, 0});
    CHECK(under > 0);
    CHECK(under < 255);
}

TEST_CASE("SS 4.3: a strip's points out of reach of a texel do not shadow it", "[ubake][shadowmask]") {
    // The strip reaches 125 units. A block hides its west end from the floor
    // at its east end, beyond that end's reach, so the floor there stays lit.
    Scene scene;
    const std::uint32_t floor = addQuad(scene, {0, 0, 0}, {512, 0, 0}, {512, 512, 0}, {0, 512, 0}, {0, 0, 1});
    addBox(scene, {150, 246, 35}, {170, 266, 45});
    const Geometry geometry = finish(std::move(scene));
    Light strip = pointLight({100, 256, 50});
    strip.radius = 4;
    strip.effect = 17;
    strip.strip = uta::ubundle::STRIP_LEADER;
    strip.stripFrom = {100, 256, 50};
    strip.stripTo = {400, 256, 50};
    JobSystem jobs(2);
    const auto mask = bakeShadowMask(bundleOf(geometry, {strip}), jobs);
    REQUIRE(mask.has_value());
    CHECK(valueAt(geometry, *mask, floor, 0, {400, 256, 0}) == 255);
}

TEST_CASE("SS 4.3: a two-sided liquid lit only from behind has no pair", "[ubake][shadowmask]") {
    // UTA-0215: scene.frag lights a liquid's sheet as its front from both sides.
    JobSystem jobs(2);
    const auto [geometry, sheet] = sheetScene(PF_TWO_SIDED, "water", false);
    Bundle bundle = bundleOf(geometry, {pointLight(BELOW)});
    uta::ubundle::MaterialRecord water;
    water.id = "water";
    water.liquid.emplace();
    bundle.materials = std::vector<uta::ubundle::MaterialRecord>{water};
    const auto mask = bakeShadowMask(bundle, jobs);
    REQUIRE(mask.has_value());
    CHECK(pairOf(*mask, sheet, 0) == nullptr);
}

namespace {

/// INV-3's scene: a floor, a light overhead, and a 112-unit pane between them
/// at z 100 wearing `material` under `flags`. Returns the floor's first vertex.
std::pair<Geometry, std::uint32_t> paneScene(std::uint32_t flags, const std::string& material = "pane") {
    Scene scene;
    const std::uint32_t floor = addQuad(scene, {0, 0, 0}, {512, 0, 0}, {512, 512, 0}, {0, 512, 0}, {0, 0, 1});
    addQuad(scene, {200, 200, 100}, {312, 200, 100}, {312, 312, 100}, {200, 312, 100}, {0, 0, -1}, flags, material);
    return {finish(std::move(scene)), floor};
}

} // namespace

TEST_CASE("INV-3: a translucent or modulated or non-solid or backdrop pane casts nothing", "[ubake][shadowmask]") {
    JobSystem jobs(2);
    {
        const auto [geometry, floor] = paneScene(0);
        const auto mask = bakeShadowMask(bundleOf(geometry, {pointLight(LIGHT_AT)}), jobs);
        REQUIRE(mask.has_value());
        REQUIRE(valueAt(geometry, *mask, floor, 0, {256, 256, 0}) == 0); // a solid pane does
    }
    for (const std::uint32_t flags : {PF_TRANSLUCENT, PF_MODULATED, PF_NOT_SOLID, PF_FAKE_BACKDROP}) {
        CAPTURE(flags);
        const auto [geometry, floor] = paneScene(flags);
        const auto mask = bakeShadowMask(bundleOf(geometry, {pointLight(LIGHT_AT)}), jobs);
        REQUIRE(mask.has_value());
        const MaskPair* pair = pairOf(*mask, floor, 0);
        REQUIRE(pair != nullptr);
        CHECK(pair->x == MASK_ALL_LIT);
    }
}

TEST_CASE("UTA-0352: a zone portal the level draws is charted and lit; an invisible one is not",
          "[ubake][shadowmask]") {
    // A water sheet is often a zone portal too (flags 0x0400010c on
    // DM-[JAPAN]-AerialAct). The renderer draws and lights it (UTA-0188), so
    // with no chart it lit through the cluster path's shadow-map reads.
    Scene scene;
    addQuad(scene, {0, 0, 0}, {512, 0, 0}, {512, 512, 0}, {0, 512, 0}, {0, 0, 1});
    const std::uint32_t water = addQuad(scene, {0, 0, 100}, {200, 0, 100}, {200, 200, 100}, {0, 200, 100}, {0, 0, 1},
                                        PF_PORTAL | PF_TWO_SIDED | PF_NOT_SOLID | PF_TRANSLUCENT, "water");
    const std::uint32_t hidden = addQuad(scene, {300, 300, 100}, {500, 300, 100}, {500, 500, 100}, {300, 500, 100},
                                         {0, 0, 1}, PF_PORTAL | PF_INVISIBLE, "portal");
    JobSystem jobs(2);
    const auto mask = bakeShadowMask(bundleOf(finish(std::move(scene)), {pointLight(LIGHT_AT)}), jobs);
    REQUIRE(mask.has_value());
    REQUIRE(mask->vertexChart[water] != MASK_NO_CHART);
    CHECK(pairOf(*mask, water, 0) != nullptr);
    CHECK(mask->vertexChart[hidden] == MASK_NO_CHART);
}

TEST_CASE("INV-3: a masked pane casts only its solid half's shadow", "[ubake][shadowmask]") {
    // Two texels: u below 0.5 is a hole, above it solid.
    Cutouts cutouts;
    cutouts["pane"] = Cutout{2, 1, {0, 1}};
    const auto [geometry, floor] = paneScene(PF_MASKED);
    JobSystem jobs(2);
    const auto mask = bakeShadowMask(bundleOf(geometry, {pointLight(LIGHT_AT)}), jobs, cutouts);
    REQUIRE(mask.has_value());
    // (230, 256) meets the pane at u 0.38; (282, 256) at u 0.62.
    CHECK(valueAt(geometry, *mask, floor, 0, {230, 256, 0}) == 255);
    CHECK(valueAt(geometry, *mask, floor, 0, {282, 256, 0}) == 0);

    // With no cutout to read, the masked pane is solid throughout.
    const auto solid = bakeShadowMask(bundleOf(geometry, {pointLight(LIGHT_AT)}), jobs);
    REQUIRE(solid.has_value());
    CHECK(valueAt(geometry, *solid, floor, 0, {230, 256, 0}) == 0);
}

TEST_CASE("INV-4: a light the renderer does not draw directly has no pair", "[ubake][shadowmask]") {
    std::vector<Light> lights;
    lights.push_back(pointLight(LIGHT_AT, 0)); // the control
    Light backdrop = pointLight(LIGHT_AT, 1);
    backdrop.type = 6;
    lights.push_back(backdrop);
    Light special = pointLight(LIGHT_AT, 2);
    special.specialLit = true;
    lights.push_back(special);
    Light dark = pointLight(LIGHT_AT, 3);
    dark.brightness = 0;
    dark.volumeRadius = 40;
    lights.push_back(dark);
    Light spot = pointLight(LIGHT_AT, 4);
    spot.effect = 12;
    spot.cone = 0;
    lights.push_back(spot);
    Light absorbed = pointLight(LIGHT_AT, 5);
    absorbed.strip = uta::ubundle::STRIP_ABSORBED;
    lights.push_back(absorbed);
    // UTA-0438 SS 4.1: a special-lit light lights directly, but only a
    // PF_SpecialLit chart, and this room has none.
    for (const Light& light : lights)
        CHECK(uta::ubundle::litDirectly(light) == (light.exportIndex == 0 || light.exportIndex == 2));

    const PillarRoom room = pillarRoom();
    JobSystem jobs(2);
    const auto mask = bakeShadowMask(bundleOf(room.geometry, lights), jobs);
    REQUIRE(mask.has_value());
    REQUIRE_FALSE(mask->pairs.empty());
    for (const MaskPair& pair : mask->pairs) CHECK(pair.light == 0);
}

TEST_CASE("UTA-0438 INV-1: a special-lit chart pairs only special-lit lights", "[ubake][shadowmask]") {
    // Two walls facing one light of each class: the flagged wall takes only
    // the special light, the plain wall only the ordinary one.
    Scene scene;
    const std::uint32_t plain = addQuad(scene, {0, 0, 0}, {256, 0, 0}, {256, 0, 256}, {0, 0, 256}, {0, 1, 0});
    const std::uint32_t flagged = addQuad(scene, {0, 300, 0}, {256, 300, 0}, {256, 300, 256}, {0, 300, 256},
                                          {0, -1, 0}, PF_SPECIAL_LIT);
    const Geometry geometry = finish(std::move(scene));
    Light special = pointLight({128, 150, 128}, 1);
    special.specialLit = true;
    JobSystem jobs(2);
    const auto mask = bakeShadowMask(bundleOf(geometry, {pointLight({128, 150, 128}, 0), special}), jobs);
    REQUIRE(mask.has_value());
    CHECK(pairOf(*mask, plain, 0) != nullptr);
    CHECK(pairOf(*mask, plain, 1) == nullptr);
    CHECK(pairOf(*mask, flagged, 0) == nullptr);
    CHECK(pairOf(*mask, flagged, 1) != nullptr);
}

namespace {

/// INV-5's mover: a 20-unit cube centred on its location.
uta::ubundle::MoverShape cubeMover(std::uint32_t exportIndex, P at) {
    uta::ubundle::MoverShape mover;
    mover.exportIndex = exportIndex;
    mover.location = at;
    Scene scene;
    addBox(scene, {-10, -10, -10}, {10, 10, 10});
    mover.geometry = finish(std::move(scene));
    return mover;
}

/// The floor's pair of light 0 in a floor-only level holding `bundle`'s movers.
std::uint8_t floorMark(Bundle bundle) {
    Scene scene;
    const std::uint32_t floor = addQuad(scene, {0, 0, 0}, {512, 0, 0}, {512, 512, 0}, {0, 512, 0}, {0, 0, 1});
    bundle.geometry = finish(std::move(scene));
    bundle.lights = std::vector<Light>{pointLight(LIGHT_AT)};
    JobSystem jobs(2);
    const auto mask = bakeShadowMask(bundle, jobs);
    REQUIRE(mask.has_value());
    const MaskPair* pair = pairOf(*mask, floor, 0);
    REQUIRE(pair != nullptr);
    return pair->moverReach;
}

} // namespace

TEST_CASE("INV-5: a mover between the light and the floor marks the floor's pair", "[ubake][shadowmask]") {
    Bundle between;
    between.movers = std::vector{cubeMover(7, {256, 256, 100})};
    CHECK(floorMark(between) == 1);

    Bundle beyond;
    beyond.movers = std::vector{cubeMover(7, {256, 256, 400})};
    CHECK(floorMark(beyond) == 0);
    CHECK(floorMark(Bundle{}) == 0);
}

TEST_CASE("INV-5: a mover beyond the light whose key carries it between marks the pair", "[ubake][shadowmask]") {
    Bundle keyed;
    keyed.movers = std::vector{cubeMover(7, {256, 256, 400})};
    uta::ubundle::Placements placements;
    uta::ubundle::ActorPlacement actor;
    actor.exportIndex = 7;
    uta::ubundle::PropertyRecord key;
    key.name = "KeyPos";
    key.arrayIndex = 1;
    key.kind = uta::ubundle::ValueKind::Vector;
    key.value = std::array<float, 3>{0, 0, -300};
    actor.properties.push_back(key);
    placements.actors.push_back(actor);
    keyed.placements = placements;
    CHECK(floorMark(keyed) == 1);

    // Another actor's key moves nothing.
    keyed.placements->actors[0].exportIndex = 8;
    CHECK(floorMark(keyed) == 0);
}

TEST_CASE("INV-5: a rolled mover is placed by UTA-0119 SS 4.5's roll", "[ubake][shadowmask]") {
    // A box 150 out along the mover's own Y, the mover 50 above the light.
    // R's second row is (0, c, s), so a quarter roll carries +Y to -Z: the
    // box lands at z 100, between the light and the floor. The other way
    // round it lands at z 400, beyond the light.
    uta::ubundle::MoverShape mover;
    mover.exportIndex = 7;
    mover.location = {256, 256, 250};
    Scene scene;
    addBox(scene, {-10, 140, -10}, {10, 160, 10});
    mover.geometry = finish(std::move(scene));

    Bundle rolled;
    mover.rotation = {0, 0, 16384};
    rolled.movers = std::vector{mover};
    CHECK(floorMark(rolled) == 1);

    Bundle rolledBack;
    mover.rotation = {0, 0, -16384};
    rolledBack.movers = std::vector{mover};
    CHECK(floorMark(rolledBack) == 0);
}

TEST_CASE("INV-6: placed rectangles do not overlap and hold every lit vertex", "[ubake][shadowmask]") {
    const PillarRoom room = pillarRoom();
    std::vector<Light> lights{pointLight(LIGHT_AT, 0), pointLight({100, 400, 120}, 1)};
    JobSystem jobs(2);
    const auto mask = bakeShadowMask(bundleOf(room.geometry, lights), jobs);
    REQUIRE(mask.has_value());

    struct Rect {
        std::uint32_t x, y, w, h;
    };
    std::vector<Rect> rects;
    for (const auto& chart : mask->charts)
        for (std::uint32_t k = chart.firstPair; k < chart.firstPair + chart.pairCount; ++k)
            if (mask->pairs[k].x != MASK_ALL_LIT)
                rects.push_back({mask->pairs[k].x, mask->pairs[k].y, chart.width, chart.height});
    REQUIRE(rects.size() >= 2);
    for (std::size_t a = 0; a < rects.size(); ++a) {
        CHECK(rects[a].x + rects[a].w <= mask->width);
        CHECK(rects[a].y + rects[a].h <= mask->height);
        for (std::size_t b = a + 1; b < rects.size(); ++b) {
            const bool apart = rects[a].x + rects[a].w <= rects[b].x || rects[b].x + rects[b].w <= rects[a].x
                               || rects[a].y + rects[a].h <= rects[b].y || rects[b].y + rects[b].h <= rects[a].y;
            CHECK(apart);
        }
    }
    for (std::size_t v = 0; v < mask->vertexChart.size(); ++v) {
        if (mask->vertexChart[v] == MASK_NO_CHART) continue;
        const auto& chart = mask->charts[mask->vertexChart[v]];
        CAPTURE(v);
        CHECK(mask->vertexTexel[v][0] >= 1);
        CHECK(mask->vertexTexel[v][0] <= chart.width - 1.0f);
        CHECK(mask->vertexTexel[v][1] >= 1);
        CHECK(mask->vertexTexel[v][1] <= chart.height - 1.0f);
    }
}

TEST_CASE("INV-6: pairs past the limit squared bake coarser within the limit", "[ubake][shadowmask]") {
    // Two 256-unit floors, each shadowed by a post, at 1/12 unit a texel:
    // 3074 texels a side each, 18.9 million in all, past 4096 squared. The
    // limit is 4096, not the bundle's, so the fixture stays small (UTA-0322).
    constexpr std::uint32_t limit = 4096;
    Scene scene;
    addQuad(scene, {0, 0, 0}, {256, 0, 0}, {256, 256, 0}, {0, 256, 0}, {0, 0, 1});
    addQuad(scene, {300, 0, 0}, {556, 0, 0}, {556, 256, 0}, {300, 256, 0}, {0, 0, 1});
    addBox(scene, {120, 120, 40}, {136, 136, 60});
    addBox(scene, {420, 120, 40}, {436, 136, 60});
    const Geometry geometry = finish(std::move(scene));
    JobSystem jobs(4);
    const float start = 1.0f / 12;
    const auto mask = bakeShadowMask(bundleOf(geometry, {pointLight({278, 128, 200})}), jobs, start, {}, limit);
    REQUIRE(mask.has_value());
    CHECK(mask->texelSize >= 2 * start);
    CHECK(mask->width <= limit);
    CHECK(mask->height <= limit);
}

TEST_CASE("INV-7: the mask is the same at 1 and 2 and 4 workers", "[ubake][shadowmask]") {
    const PillarRoom room = pillarRoom();
    std::vector<Light> lights{pointLight(LIGHT_AT, 0), pointLight({100, 400, 120}, 1), pointLight({450, 60, 60}, 2)};
    const auto encoded = [&](std::size_t workers) {
        JobSystem jobs(workers);
        Bundle bundle = bundleOf(room.geometry, lights);
        auto mask = bakeShadowMask(bundle, jobs);
        REQUIRE(mask.has_value());
        bundle.shadowMask = std::move(*mask);
        auto bytes = uta::ubundle::write(bundle);
        REQUIRE(bytes.has_value());
        return *bytes;
    };
    const auto one = encoded(1);
    CHECK(encoded(2) == one);
    CHECK(encoded(4) == one);
}

namespace {

/// The standard fixture's level and lamp, the lamp at `brightness`.
uta::test::bake::Fixture litLevel(std::uint8_t brightness) {
    using namespace uta::test::bake;
    Fixture fixture;
    MapBuilder& map = fixture.map;
    const std::int32_t wall = map.addTexture(TextureSpec{"Wall", "Base", picture(1), false});
    const std::int32_t floor = map.addTexture(TextureSpec{"Floor", "", picture(2), false});
    map.addSurface(wall).addSurface(wall, MASKED).addSurface(floor);
    map.addActorOfClass("ActorPkg", "Lamp",
                        {vectorProperty("Location", 16.0F, 32.0F, 48.0F), byteProperty("LightBrightness", brightness)});
    return fixture;
}

} // namespace

TEST_CASE("UTA-0326 SS 4.6: a bake writes SMSK only for a litDirectly light", "[ubake][shadowmask]") {
    using namespace uta::test::bake;
    for (const std::uint8_t brightness : {std::uint8_t{0}, std::uint8_t{64}}) {
        CAPTURE(int{brightness});
        const Fixture fixture = litLevel(brightness);
        MemoryPackages packages = memoryPackagesFor(fixture);
        const std::vector<std::uint8_t> bytes = fixture.map.build();
        const auto map = uta::upkg::Package::open(uta::test::asBytes(bytes));
        REQUIRE(map.has_value());
        JobSystem jobs(2);
        const auto result = uta::ubake::detail::bake(*map, MAP_NAME, packages.resolver(), jobs, &uta::umat::curated,
                                                     uta::umat::TEXTURE_BUDGET_BYTES);
        REQUIRE(result.has_value());
        // The lamp is placed either way, and the level has surfaces to light.
        REQUIRE(result->bundle.lights.has_value());
        REQUIRE(result->bundle.lights->size() == 1);
        REQUIRE_FALSE(result->bundle.geometry->vertices.empty());
        CHECK(result->bundle.shadowMask.has_value() == (brightness != 0));
    }
}
