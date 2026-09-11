// UTA-0109's builder cases: buildGeometry over a Model built in memory.
//
// docs/specs/UTA-0109-map-geometry.md SS 4.3, INV-3 to INV-9. The container's
// cases are tests/unit/BundleGeometryTest.cpp, and the bake's, INV-10, are in
// tests/unit/BakeTest.cpp.
//
// NO PACKAGE IS BUILT. buildGeometry takes a upkg::Model and a lookup, so the
// Model is assembled field by field and the lookup is a stub that records what
// it was asked. That is what lets each case name exactly the index or flag it
// is about.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "ubake/Geometry.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using uta::ErrorCode;
using uta::ubake::buildGeometry;
using uta::ubake::MaterialLookup;
using uta::ubake::PF_INVISIBLE;
using uta::ubake::PF_MASKED;
using uta::ubake::SurfaceMaterial;
using uta::ubundle::Geometry;
using uta::upkg::ObjectReference;
using uta::upkg::Vector3;

namespace {

// UT 4.32's EPolyFlags values for the flags this item carries and never reads
// -- SS 4.3's source.
constexpr std::uint32_t PF_FAKE_BACKDROP = 0x00000080u;
constexpr std::uint32_t PF_TWO_SIDED = 0x00000100u;
constexpr std::uint32_t PF_PORTAL = 0x04000000u;

Vector3 at(float x, float y, float z) {
    return Vector3{x, y, z};
}

/// A unit square in the plane z = `z`, wound so its fan points along +z.
std::vector<Vector3> square(float z) {
    return {at(0, 0, z), at(64, 0, z), at(64, 64, z), at(0, 64, z)};
}

/// A Model assembled field by field. Vectors 0, 1 and 2 are +z, +x and +y --
/// every surface's normal, TextureU and TextureV unless a case changes them.
struct Scene {
    uta::upkg::Model model;

    Scene() { model.vectors = {at(0, 0, 1), at(1, 0, 0), at(0, 1, 0)}; }

    /// A surface wearing `texture` under `flags`, and a node drawing `points`
    /// on it. Its base is the first point. Returns the node's index.
    std::size_t add(const std::vector<Vector3>& points, std::uint32_t flags = 0,
                    std::int32_t texture = 1) {
        uta::upkg::BspSurf surf;
        surf.texture = ObjectReference{texture};
        surf.polyFlags = flags;
        surf.pBase = static_cast<std::int32_t>(model.points.size());
        surf.vNormal = 0;
        surf.vTextureU = 1;
        surf.vTextureV = 2;

        uta::upkg::BspNode node;
        node.iSurf = static_cast<std::int32_t>(model.surfs.size());
        node.iVertPool = static_cast<std::int32_t>(model.verts.size());
        node.numVertices = static_cast<std::uint8_t>(points.size());
        for (const Vector3& point : points) {
            model.verts.push_back(uta::upkg::Vert{static_cast<std::int32_t>(model.points.size()), 0});
            model.points.push_back(point);
        }
        model.surfs.push_back(surf);
        model.nodes.push_back(node);
        return model.nodes.size() - 1;
    }

    uta::upkg::BspSurf& surfOf(std::size_t node) {
        return model.surfs[static_cast<std::size_t>(model.nodes[node].iSurf)];
    }
};

/// A lookup that makes "t<raw>" or "t<raw>#masked" for every texture it is
/// asked for, `uSize` by `vSize`, and records each question.
struct Stub {
    double uSize = 64;
    double vSize = 64;
    bool onlyMasked = false;
    std::vector<std::pair<std::int32_t, bool>> asked;
    std::map<std::pair<std::int32_t, bool>, SurfaceMaterial> made;

    MaterialLookup lookup() {
        return [this](ObjectReference texture, bool masked) -> const SurfaceMaterial* {
            asked.emplace_back(texture.raw(), masked);
            if (onlyMasked && !masked) return nullptr;
            const std::string id = "t" + std::to_string(texture.raw()) + (masked ? "#masked" : "");
            return &made.try_emplace({texture.raw(), masked}, SurfaceMaterial{id, uSize, vSize})
                        .first->second;
        };
    }
};

Geometry built(const Scene& scene, Stub& stub) {
    auto geometry = buildGeometry(scene.model, stub.lookup());
    REQUIRE(geometry.has_value());
    return std::move(*geometry);
}

bool samePosition(const uta::ubundle::GeometryVertex& vertex, const Vector3& point) {
    return vertex.position[0] == point.x && vertex.position[1] == point.y
           && vertex.position[2] == point.z;
}

void refused(const Scene& scene, std::string_view says) {
    Stub stub;
    const auto geometry = buildGeometry(scene.model, stub.lookup());
    REQUIRE_FALSE(geometry.has_value());
    CHECK(geometry.error().code() == ErrorCode::MalformedData);
    CHECK(geometry.error().message().find("node 0") != std::string_view::npos);
    CHECK(geometry.error().message().find(says) != std::string_view::npos);
}

} // namespace

TEST_CASE("a quad wound along its normal is a two-triangle fan in pool order", "[ubake][geom]") {
    // INV-3.
    Scene scene;
    scene.add(square(0));
    Stub stub;
    const Geometry geometry = built(scene, stub);

    REQUIRE(geometry.vertices.size() == 4);
    for (std::size_t k = 0; k < 4; ++k) CHECK(samePosition(geometry.vertices[k], square(0)[k]));
    CHECK(geometry.indices == std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3});
    REQUIRE(geometry.batches.size() == 1);
    CHECK(geometry.batches[0].material == "t1");
    CHECK(geometry.batches[0].firstIndex == 0);
    CHECK(geometry.batches[0].indexCount == 6);
}

TEST_CASE("a node wound against its normal is reversed and one with no area emits nothing",
          "[ubake][geom]") {
    // INV-4. The triangle's fan points along -z while its surface's normal is
    // +z, so it must come out P0, P2, P1. The three collinear points have no
    // area; the square beside them proves the node after one is still drawn.
    Scene scene;
    const std::vector<Vector3> against = {at(0, 0, 0), at(0, 64, 0), at(64, 0, 0)};
    scene.add(against);
    scene.add({at(0, 0, 8), at(32, 0, 8), at(64, 0, 8)});
    scene.add(square(16));
    Stub stub;
    const Geometry geometry = built(scene, stub);

    REQUIRE(geometry.vertices.size() == 3 + 4);
    CHECK(samePosition(geometry.vertices[0], against[0]));
    CHECK(samePosition(geometry.vertices[1], against[2]));
    CHECK(samePosition(geometry.vertices[2], against[1]));
    for (std::size_t k = 0; k < 4; ++k) CHECK(samePosition(geometry.vertices[3 + k], square(16)[k]));
}

TEST_CASE("fewer than three vertices and PF_Invisible emit nothing while other flags pass through",
          "[ubake][geom]") {
    // INV-5. The last surface carries PF_Portal AND PF_Invisible: invisible wins.
    Scene scene;
    scene.add({at(0, 0, 0), at(64, 0, 0)});
    scene.add(square(8), PF_INVISIBLE);
    scene.add(square(16), PF_PORTAL);
    scene.add(square(24), PF_FAKE_BACKDROP);
    scene.add(square(32), PF_TWO_SIDED);
    scene.add(square(40), PF_PORTAL | PF_INVISIBLE);
    Stub stub;
    const Geometry geometry = built(scene, stub);

    CHECK(geometry.vertices.size() == 3 * 4);
    REQUIRE(geometry.batches.size() == 3);
    CHECK(geometry.batches[0].polyFlags == PF_FAKE_BACKDROP);
    CHECK(geometry.batches[1].polyFlags == PF_TWO_SIDED);
    CHECK(geometry.batches[2].polyFlags == PF_PORTAL);
}

TEST_CASE("u and v are the offset from Base along each axis plus the pan over the size",
          "[ubake][geom]") {
    // INV-6. Base sits off the polygon and off the origin, TextureU is two
    // units long, and both pans are non-zero with opposite signs. The expected
    // values are SS 4.3 step 7's expressions written out.
    Scene scene;
    const std::size_t node = scene.add(square(0));
    auto& surf = scene.surfOf(node);
    surf.pBase = static_cast<std::int32_t>(scene.model.points.size());
    scene.model.points.push_back(at(10, 20, 0));
    surf.vTextureU = static_cast<std::int32_t>(scene.model.vectors.size());
    scene.model.vectors.push_back(at(2, 0, 0));
    surf.panU = 16;
    surf.panV = -8;

    const auto expectU = [](const Vector3& p, double uSize) {
        return static_cast<float>(((p.x - 10.0) * 2.0 + (p.y - 20.0) * 0.0 + p.z * 0.0 + 16.0) / uSize);
    };
    const auto expectV = [](const Vector3& p, double vSize) {
        return static_cast<float>(((p.x - 10.0) * 0.0 + (p.y - 20.0) * 1.0 + p.z * 0.0 - 8.0) / vSize);
    };

    for (const double uSize : {128.0, 256.0}) {
        Stub stub;
        stub.uSize = uSize;
        stub.vSize = 64;
        const Geometry geometry = built(scene, stub);
        REQUIRE(geometry.vertices.size() == 4);
        for (std::size_t k = 0; k < 4; ++k) {
            INFO("uSize " << uSize << " corner " << k);
            CHECK(geometry.vertices[k].u == expectU(square(0)[k], uSize));
            CHECK(geometry.vertices[k].v == expectV(square(0)[k], 64));
        }
    }
}

TEST_CASE("the surface's own PF_Masked picks the variant and no answer means no material",
          "[ubake][geom]") {
    // INV-7. The stub answers only the masked variant, so the unmasked surface
    // wears nothing; the null-texture surface is never asked at all.
    Scene scene;
    scene.add(square(0), PF_MASKED, 5);
    scene.add(square(8), 0, 5);
    scene.add(square(16), 0, 0);
    Stub stub;
    stub.onlyMasked = true;
    const Geometry geometry = built(scene, stub);

    CHECK(stub.asked == std::vector<std::pair<std::int32_t, bool>>{{5, true}, {5, false}});
    REQUIRE(geometry.batches.size() == 2);
    // The unmasked surface and the null texture share one key -- no material,
    // flags 0 -- so they share the first batch, in node order.
    CHECK(geometry.batches[0].material.empty());
    CHECK(geometry.batches[0].polyFlags == 0);
    CHECK(geometry.batches[0].indexCount == 12);
    CHECK(geometry.batches[1].material == "t5#masked");
    CHECK(geometry.batches[1].polyFlags == PF_MASKED);
    for (std::size_t k = 0; k < 8; ++k) {
        CHECK(geometry.vertices[k].u == 0.0F);
        CHECK(geometry.vertices[k].v == 0.0F);
    }
    CHECK(geometry.vertices[8 + 1].u != 0.0F);
}

TEST_CASE("batches ascend by material then flags and keep node order inside each",
          "[ubake][geom]") {
    // INV-8. Materials descend in node order; texture 5 appears under two sets
    // of flags; texture 7 appears twice under one key, at z = 32 then z = 40.
    Scene scene;
    scene.add(square(0), 0, 3);
    scene.add(square(8), 0, 2);
    scene.add(square(16), PF_TWO_SIDED, 5);
    scene.add(square(24), PF_FAKE_BACKDROP, 5);
    scene.add(square(32), 0, 7);
    scene.add(square(40), 0, 7);
    Stub stub;
    const Geometry geometry = built(scene, stub);

    REQUIRE(geometry.batches.size() == 5);
    CHECK(geometry.batches[0].material == "t2");
    CHECK(geometry.batches[1].material == "t3");
    CHECK(geometry.batches[2].material == "t5");
    CHECK(geometry.batches[2].polyFlags == PF_FAKE_BACKDROP);
    CHECK(geometry.batches[3].material == "t5");
    CHECK(geometry.batches[3].polyFlags == PF_TWO_SIDED);
    CHECK(geometry.batches[4].material == "t7");
    CHECK(geometry.batches[4].indexCount == 12);

    const std::uint32_t first = geometry.indices[geometry.batches[4].firstIndex];
    CHECK(geometry.vertices[first].position[2] == 32.0F);
    CHECK(geometry.vertices[first + 4].position[2] == 40.0F);
}

TEST_CASE("a drawn node's index past its table is refused naming the node", "[ubake][geom]") {
    // INV-9, one case per index, each one past its table's end.
    SECTION("iSurf") {
        Scene scene;
        scene.add(square(0));
        scene.model.nodes[0].iSurf = 1;
        refused(scene, "iSurf 1");
    }
    SECTION("the vertex pool") {
        Scene scene;
        scene.add(square(0));
        scene.model.nodes[0].iVertPool = 1;
        refused(scene, "vertex pool 1");
    }
    SECTION("pVertex") {
        Scene scene;
        scene.add(square(0));
        scene.model.verts[2].pVertex = 4;
        refused(scene, "pVertex 4");
    }
    SECTION("pBase") {
        Scene scene;
        scene.add(square(0));
        scene.surfOf(0).pBase = 4;
        refused(scene, "pBase 4");
    }
    SECTION("vNormal") {
        Scene scene;
        scene.add(square(0));
        scene.surfOf(0).vNormal = 3;
        refused(scene, "vNormal 3");
    }
    SECTION("vTextureU") {
        Scene scene;
        scene.add(square(0));
        scene.surfOf(0).vTextureU = 3;
        refused(scene, "vTextureU 3");
    }
    SECTION("vTextureV") {
        Scene scene;
        scene.add(square(0));
        scene.surfOf(0).vTextureV = 3;
        refused(scene, "vTextureV 3");
    }
}

TEST_CASE("a skipped node is not checked past what its skip needed", "[ubake][geom]") {
    // INV-9. A two-vertex node is checked for nothing, so its out-of-range
    // iSurf and vertex pool pass. An invisible node is checked for iSurf only,
    // so its out-of-range vertex pool passes.
    Scene scene;
    scene.add({at(0, 0, 0), at(64, 0, 0)});
    scene.model.nodes[0].iSurf = 99;
    scene.model.nodes[0].iVertPool = 99;
    scene.add(square(8), PF_INVISIBLE);
    scene.model.nodes[1].iVertPool = 99;
    Stub stub;
    const Geometry geometry = built(scene, stub);
    CHECK(geometry.vertices.empty());
    CHECK(geometry.batches.empty());
}
