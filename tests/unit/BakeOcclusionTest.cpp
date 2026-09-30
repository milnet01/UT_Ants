// UTA-0164's bake: the occlusion atlas and each vertex's place in it.
//
// docs/specs/UTA-0164-ambient-occlusion.md SS 4.2, SS 4.3 and INV-2 to INV-6.
//
// The level is built by hand as GEOM would hold it: each rectangle is one
// polygon of four vertices, fanned from its first, as ubake::buildGeometry
// step 8 writes them.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "ubake/Occlusion.h"

#include "core/Jobs.h"
#include "ubake/LightProbes.h"
#include "ubake/SurfaceRays.h"
#include "ubundle/Bundle.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <vector>

using uta::JobSystem;
using uta::ubake::bakeOcclusion;
using uta::ubake::directions;
using uta::ubake::OCCLUSION_DISTANCE;
using uta::ubake::SurfaceRays;
using uta::ubake::Vec3;
using uta::ubake::OCCLUSION_TEXEL_SIZE;
using uta::ubundle::Geometry;
using uta::ubundle::GeometryBatch;
using uta::ubundle::GeometryVertex;
using uta::ubundle::Occlusion;

namespace {

using P = std::array<float, 3>;

constexpr std::uint32_t PF_UNLIT = 0x00400000u;

struct Scene {
    Geometry geometry;
    std::vector<std::uint32_t> indicesLit, indicesUnlit;
};

/// The rectangle a, b, c, d -- d is a + (d - a), c is b + (d - a) -- facing `n`.
std::uint32_t addQuad(Scene& scene, P a, P b, P c, P d, P n, bool lit = true) {
    const auto first = static_cast<std::uint32_t>(scene.geometry.vertices.size());
    for (const P& p : {a, b, c, d}) {
        GeometryVertex vertex;
        vertex.position = p;
        vertex.normal = n;
        scene.geometry.vertices.push_back(vertex);
    }
    std::vector<std::uint32_t>& indices = lit ? scene.indicesLit : scene.indicesUnlit;
    for (std::uint32_t k = 1; k + 1 < 4; ++k) indices.insert(indices.end(), {first, first + k, first + k + 1});
    return first;
}

/// The batches, lit before unlit -- the key order GEOM keeps.
Geometry finish(Scene scene) {
    Geometry& g = scene.geometry;
    g.indices = scene.indicesLit;
    g.batches.push_back(GeometryBatch{"", 0, 0, static_cast<std::uint32_t>(scene.indicesLit.size())});
    if (!scene.indicesUnlit.empty()) {
        g.batches.push_back(GeometryBatch{"", PF_UNLIT, static_cast<std::uint32_t>(g.indices.size()),
                                          static_cast<std::uint32_t>(scene.indicesUnlit.size())});
        g.indices.insert(g.indices.end(), scene.indicesUnlit.begin(), scene.indicesUnlit.end());
    }
    return g;
}

/// A 512-unit box room with no ceiling: its floor as 8 by 8 tiles of 64, and
/// four 256-unit walls facing in. Returns each floor tile's first vertex, row
/// by row from the origin.
std::vector<std::uint32_t> boxRoom(Scene& scene) {
    std::vector<std::uint32_t> tiles;
    for (int j = 0; j < 8; ++j)
        for (int i = 0; i < 8; ++i) {
            const float x0 = 64.0f * i, y0 = 64.0f * j, x1 = x0 + 64, y1 = y0 + 64;
            tiles.push_back(addQuad(scene, {x0, y0, 0}, {x1, y0, 0}, {x1, y1, 0}, {x0, y1, 0}, {0, 0, 1}));
        }
    addQuad(scene, {0, 0, 0}, {512, 0, 0}, {512, 0, 256}, {0, 0, 256}, {0, 1, 0});
    addQuad(scene, {0, 512, 0}, {512, 512, 0}, {512, 512, 256}, {0, 512, 256}, {0, -1, 0});
    addQuad(scene, {0, 0, 0}, {0, 512, 0}, {0, 512, 256}, {0, 0, 256}, {1, 0, 0});
    addQuad(scene, {512, 0, 0}, {512, 512, 0}, {512, 512, 256}, {512, 0, 256}, {-1, 0, 0});
    return tiles;
}

/// The stored value at world point `p` of the rectangle whose first vertex is
/// `first`, found through its four vertices' uvs.
int texelAt(const Geometry& g, const Occlusion& o, std::uint32_t first, P p) {
    const auto at = [&](std::uint32_t k) { return g.vertices[first + k].position; };
    const auto sub = [](P a, P b) { return P{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
    const auto dot = [](P a, P b) { return double(a[0]) * b[0] + double(a[1]) * b[1] + double(a[2]) * b[2]; };
    const P e1 = sub(at(1), at(0)), e2 = sub(at(3), at(0)), d = sub(p, at(0));
    const double s = dot(d, e1) / dot(e1, e1), t = dot(d, e2) / dot(e2, e2);
    const auto& u0 = o.uv[first];
    const auto& u1 = o.uv[first + 1];
    const auto& u3 = o.uv[first + 3];
    const double u = u0[0] + s * (u1[0] - u0[0]) + t * (u3[0] - u0[0]);
    const double v = u0[1] + s * (u1[1] - u0[1]) + t * (u3[1] - u0[1]);
    const auto x = static_cast<std::size_t>(std::floor(u * o.width));
    const auto y = static_cast<std::size_t>(std::floor(v * o.height));
    return o.texels[y * o.width + x];
}

/// The tile of a box room holding floor point (x, y).
std::uint32_t tileAt(const std::vector<std::uint32_t>& tiles, float x, float y) {
    return tiles[static_cast<std::size_t>(y / 64) * 8 + static_cast<std::size_t>(x / 64)];
}

} // namespace

TEST_CASE("INV-2: open floor is unoccluded and a wall's foot and a corner are darker", "[ubake][occlusion]") {
    Scene scene;
    const std::vector<std::uint32_t> tiles = boxRoom(scene);
    const Geometry geometry = finish(scene);
    JobSystem jobs(2);
    const auto baked = bakeOcclusion(geometry, jobs);
    REQUIRE(baked.has_value());
    CHECK(baked->texelSize == OCCLUSION_TEXEL_SIZE);

    const int centre = texelAt(geometry, *baked, tileAt(tiles, 264, 264), {264, 264, 0});
    const int foot = texelAt(geometry, *baked, tileAt(tiles, 264, 8), {264, 8, 0});
    const int corner = texelAt(geometry, *baked, tileAt(tiles, 8, 8), {8, 8, 0});
    INFO("centre " << centre << ", foot " << foot << ", corner " << corner);
    CHECK(centre == 255);
    CHECK(foot < centre);
    CHECK(corner < foot);
}

TEST_CASE("UTA-0259: the gathered occluders give each ray the hit the whole tree gives", "[ubake][occlusion]") {
    // A box room with a slab leaning across one corner and a post, so origins
    // near them gather several occluders at different distances and angles.
    Scene scene;
    boxRoom(scene);
    addQuad(scene, {0, 96, 0}, {96, 0, 0}, {96, 0, 128}, {0, 96, 128}, {0.7071f, 0.7071f, 0});
    addQuad(scene, {200, 200, 0}, {216, 200, 0}, {216, 200, 96}, {200, 200, 96}, {0, -1, 0});
    addQuad(scene, {200, 216, 0}, {216, 216, 0}, {216, 216, 96}, {200, 216, 96}, {0, 1, 0});
    addQuad(scene, {180, 180, 40}, {240, 180, 40}, {240, 240, 40}, {180, 240, 40}, {0, 0, -1});
    const Geometry geometry = finish(scene);
    const SurfaceRays rays(geometry);

    struct From {
        Vec3 origin, normal;
    };
    std::vector<From> froms;
    for (double x = 4; x < 512; x += 20.5)
        for (double y = 4; y < 512; y += 20.5) froms.push_back({{x, y, 0.5}, {0, 0, 1}});
    for (double y = 4; y < 512; y += 17.25)
        for (double z = 4; z < 256; z += 17.25) froms.push_back({{0.5, y, z}, {1, 0, 0}});

    std::size_t compared = 0, hits = 0, gathering = 0;
    std::vector<std::uint32_t> near;
    for (const From& from : froms) {
        near.clear();
        rays.gatherInFront(from.origin, from.normal, OCCLUSION_DISTANCE, near);
        if (!near.empty()) ++gathering;
        // anyInFront is gatherInFront without the margin, so it never says yes to nothing gathered.
        if (rays.anyInFront(from.origin, from.normal, OCCLUSION_DISTANCE)) CHECK_FALSE(near.empty());
        for (const Vec3& w : directions()) {
            if (dot(w, from.normal) <= 0) continue;
            const auto whole = rays.first(from.origin, w, OCCLUSION_DISTANCE);
            const std::optional<double> among = rays.nearestAmong(near, from.origin, w, OCCLUSION_DISTANCE);
            ++compared;
            if (whole) ++hits;
            if (whole.has_value() != among.has_value() || (whole && whole->t != *among)) {
                CAPTURE(from.origin.x, from.origin.y, from.origin.z, w.x, w.y, w.z);
                FAIL_CHECK("a ray's hit differs");
            }
        }
    }
    // The comparison is over rays that do hit, and origins that do gather.
    CHECK(compared > 40000);
    CHECK(hits > 2000);
    CHECK(gathering > 100);

    // With no limit, against every occluder in front: the search's order and
    // its box test may prune, and must not change which hit is nearest.
    std::size_t unlimited = 0;
    for (std::size_t k = 0; k < froms.size(); k += 7) {
        const From& from = froms[k];
        near.clear();
        rays.gatherInFront(from.origin, from.normal, 1e9, near);
        for (const Vec3& w : directions()) {
            if (dot(w, from.normal) <= 0) continue;
            const auto whole = rays.first(from.origin, w);
            const std::optional<double> among =
                rays.nearestAmong(near, from.origin, w, std::numeric_limits<double>::infinity());
            ++unlimited;
            if (whole.has_value() != among.has_value() || (whole && whole->t != *among)) {
                CAPTURE(from.origin.x, from.origin.y, from.origin.z, w.x, w.y, w.z);
                FAIL_CHECK("an unlimited ray's hit differs");
            }
        }
    }
    CHECK(unlimited > 5000);

    // A ray past the limit meets nothing, as first() with that limit does.
    near.clear();
    rays.gatherInFront({256, 256, 0.5}, {0, 0, 1}, 512, near);
    REQUIRE_FALSE(near.empty());
    const Vec3 toWall{1, 0, 0.001};
    CHECK(rays.nearestAmong(near, {256, 256, 0.5}, toWall, 300).has_value());
    CHECK_FALSE(rays.nearestAmong(near, {256, 256, 0.5}, toWall, 200).has_value());
}

TEST_CASE("INV-2: a nearer occluder darkens more than a farther one", "[ubake][occlusion]") {
    // A floor under a ceiling of the same size: every upward ray meets it, at
    // a distance the ceiling's height sets.
    const auto underCeiling = [](float height) {
        Scene scene;
        const std::uint32_t floor =
            addQuad(scene, {0, 0, 0}, {512, 0, 0}, {512, 512, 0}, {0, 512, 0}, {0, 0, 1});
        addQuad(scene, {0, 0, height}, {512, 0, height}, {512, 512, height}, {0, 512, height}, {0, 0, -1});
        const Geometry geometry = finish(scene);
        JobSystem jobs(2);
        const auto baked = bakeOcclusion(geometry, jobs);
        REQUIRE(baked.has_value());
        return texelAt(geometry, *baked, floor, {264, 264, 0});
    };
    const int low = underCeiling(8), high = underCeiling(32), clear = underCeiling(128);
    INFO("under 8: " << low << ", under 32: " << high << ", under 128: " << clear);
    CHECK(low < high);
    CHECK(high < clear);
    CHECK(clear == 255);
}

TEST_CASE("INV-3: coplanar polygons of one cut agree along their edge", "[ubake][occlusion]") {
    // One wall along y = 0. Polygon A starts 5 units off it and B on it, so a
    // grid anchored at each chart would put their texel rows 5 units apart.
    Scene scene;
    addQuad(scene, {-1024, 0, 0}, {1536, 0, 0}, {1536, 0, 256}, {-1024, 0, 256}, {0, 1, 0});
    const std::uint32_t a = addQuad(scene, {0, 5, 0}, {256, 5, 0}, {256, 512, 0}, {0, 512, 0}, {0, 0, 1});
    const std::uint32_t b = addQuad(scene, {256, 0, 0}, {512, 0, 0}, {512, 512, 0}, {256, 512, 0}, {0, 0, 1});
    const Geometry geometry = finish(scene);
    JobSystem jobs(2);
    const auto baked = bakeOcclusion(geometry, jobs);
    REQUIRE(baked.has_value());

    for (const float y : {8.0f, 24.0f, 40.0f}) {
        CAPTURE(y);
        const int left = texelAt(geometry, *baked, a, {248, y, 0});
        const int right = texelAt(geometry, *baked, b, {264, y, 0});
        CHECK(left < 255);
        CHECK(std::abs(left - right) <= 1);
    }
}

TEST_CASE("INV-4: charts do not overlap and every uv sits inside its own", "[ubake][occlusion]") {
    Scene scene;
    boxRoom(scene);
    const std::uint32_t sky = addQuad(scene, {0, 0, 300}, {64, 0, 300}, {64, 64, 300}, {0, 64, 300}, {0, 0, -1},
                                      false);
    const Geometry geometry = finish(scene);
    JobSystem jobs(2);
    const auto baked = bakeOcclusion(geometry, jobs);
    REQUIRE(baked.has_value());
    const Occlusion& o = *baked;
    CHECK(o.height % 4 == 0);

    // Each lit chart's rectangle, from its vertices: SS 4.2 grows the texels
    // they cover by one a side.
    struct Rect {
        long x0, y0, x1, y1; // inclusive
    };
    std::vector<Rect> rects;
    for (std::uint32_t first = 0; first < sky; first += 4) {
        double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
        for (std::uint32_t k = first; k < first + 4; ++k) {
            const double x = double(o.uv[k][0]) * o.width, y = double(o.uv[k][1]) * o.height;
            minX = std::min(minX, x), maxX = std::max(maxX, x);
            minY = std::min(minY, y), maxY = std::max(maxY, y);
            CHECK(x >= 1);
            CHECK(y >= 1);
            CHECK(x <= o.width - 1);
            CHECK(y <= o.height - 1);
        }
        rects.push_back({std::lround(std::floor(minX + 1e-3)) - 1, std::lround(std::floor(minY + 1e-3)) - 1,
                         std::lround(std::floor(maxX - 1e-3)) + 1, std::lround(std::floor(maxY - 1e-3)) + 1});
    }
    const Rect white{0, 0, 3, 3};
    const auto overlap = [](const Rect& p, const Rect& q) {
        return p.x0 <= q.x1 && q.x0 <= p.x1 && p.y0 <= q.y1 && q.y0 <= p.y1;
    };
    for (std::size_t i = 0; i < rects.size(); ++i) {
        CAPTURE(i);
        CHECK(rects[i].x0 >= 0);
        CHECK(rects[i].y0 >= 0);
        CHECK(rects[i].x1 < long(o.width));
        CHECK(rects[i].y1 < long(o.height));
        CHECK_FALSE(overlap(rects[i], white));
        for (std::size_t j = i + 1; j < rects.size(); ++j) {
            CAPTURE(j);
            CHECK_FALSE(overlap(rects[i], rects[j]));
        }
    }

    // An unlit chart reads the white block's centre, and the block is white.
    for (std::uint32_t k = sky; k < sky + 4; ++k) {
        CHECK(o.uv[k][0] * o.width == 2.0f);
        CHECK(o.uv[k][1] * o.height == 2.0f);
    }
    for (std::uint32_t y = 0; y < 4; ++y)
        for (std::uint32_t x = 0; x < 4; ++x) CHECK(int(o.texels[y * o.width + x]) == 255);
}

TEST_CASE("INV-5: a level too large for the atlas coarsens its texel", "[ubake][occlusion]") {
    // A strip 512 units long and 1 wide: 5120 texels at 0.1, past the limit.
    Scene scene;
    addQuad(scene, {0, 0, 0}, {512, 0, 0}, {512, 1, 0}, {0, 1, 0}, {0, 0, 1});
    const Geometry geometry = finish(scene);
    JobSystem jobs(2);
    const auto baked = bakeOcclusion(geometry, jobs, 0.1f);
    REQUIRE(baked.has_value());
    CHECK(baked->texelSize >= 0.2f);
    CHECK(baked->width <= uta::ubundle::OCCLUSION_ATLAS_LIMIT);
    CHECK(baked->height <= uta::ubundle::OCCLUSION_ATLAS_LIMIT);
}

TEST_CASE("UTA-0218: a texel size that cannot coarsen is refused", "[ubake][occlusion]") {
    // Coarsening doubles the texel until the level fits; zero, a negative or
    // NaN never grows past the ceiling, so the loop would never end.
    Scene scene;
    addQuad(scene, {0, 0, 0}, {512, 0, 0}, {512, 1, 0}, {0, 1, 0}, {0, 0, 1});
    const Geometry geometry = finish(scene);
    JobSystem jobs(1);
    for (const float size : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(),
                             std::numeric_limits<float>::infinity()}) {
        INFO(size);
        CHECK_FALSE(bakeOcclusion(geometry, jobs, size).has_value());
    }
}

TEST_CASE("INV-6: the atlas is the same at 1 2 and 4 workers", "[ubake][occlusion]") {
    Scene scene;
    boxRoom(scene);
    const Geometry geometry = finish(scene);
    JobSystem one(1), two(2), four(4);
    const auto a = bakeOcclusion(geometry, one);
    const auto b = bakeOcclusion(geometry, two);
    const auto c = bakeOcclusion(geometry, four);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(c.has_value());
    CHECK(a->texels == b->texels);
    CHECK(a->texels == c->texels);
    CHECK(a->uv == b->uv);
    CHECK(a->uv == c->uv);
}
