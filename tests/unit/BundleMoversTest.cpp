// UTA-0119's container cases: the MOVR section.
//
// docs/specs/UTA-0119-mover-shapes.md SS 4.2, INV-1 and INV-2. The builder's
// cases are tests/unit/BakeMoversTest.cpp; nothing here reaches ubake.
//
// THE PAYLOADS ARE AUTHORED FROM SS 4.2, field by field, never produced by
// `write` -- an encoder and a decoder sharing a wrong width round-trip every
// value, so each is graded against the same authored bytes instead (INV-1).
//
// EACH INV-2 FIXTURE BREAKS ONE RULE of golden(), which every rule accepts.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "ubundle/Bundle.h"

#include "Bytes.h"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

using uta::ErrorCode;
using uta::testing::Bytes;
using uta::ubundle::Bundle;
using uta::ubundle::Geometry;
using uta::ubundle::GeometryBatch;
using uta::ubundle::GeometryVertex;
using uta::ubundle::Light;
using uta::ubundle::MoverShape;
using uta::ubundle::Origin;
using uta::ubundle::read;
using uta::ubundle::write;

namespace {

std::uint32_t bitsOf(float value) {
    return std::bit_cast<std::uint32_t>(value);
}

/// GEOM's encoding (UTA-0109 SS 4.2): vertices, indices, batches.
void putGeometry(Bytes& out, const Geometry& geometry) {
    out.u32(static_cast<std::uint32_t>(geometry.vertices.size()));
    for (const GeometryVertex& vertex : geometry.vertices) {
        for (const float part : vertex.position) out.f32(part);
        for (const float part : vertex.normal) out.f32(part);
        out.f32(vertex.u);
        out.f32(vertex.v);
    }
    out.u32(static_cast<std::uint32_t>(geometry.indices.size()));
    for (const std::uint32_t index : geometry.indices) out.u32(index);
    out.u32(static_cast<std::uint32_t>(geometry.batches.size()));
    for (const GeometryBatch& batch : geometry.batches) {
        out.str(batch.material);
        out.u32(batch.polyFlags);
        out.u32(batch.firstIndex);
        out.u32(batch.indexCount);
    }
}

/// A MOVR payload in SS 4.2's order.
Bytes movrPayload(const std::vector<MoverShape>& shapes) {
    Bytes out;
    out.u32(static_cast<std::uint32_t>(shapes.size()));
    for (const MoverShape& shape : shapes) {
        out.u32(shape.exportIndex);
        for (const float part : shape.location) out.f32(part);
        for (const std::int32_t part : shape.rotation) out.i32(part);
        for (const float part : shape.postScale) out.f32(part);
        putGeometry(out, shape.geometry);
    }
    return out;
}

/// A LITE payload holding no light.
Bytes emptyLite() {
    Bytes out;
    out.u32(0);
    return out;
}

/// A whole .utab holding `sections` in the order given.
std::vector<std::byte> fileWith(const std::vector<std::pair<std::string_view, Bytes>>& sections) {
    Bytes out;
    out.id("UTAB");
    out.u32(6); // formatVersion -- 6 since UTA-0119 SS 4.2
    out.u8(1);  // origin: Authored
    out.u8(0);  // kind: Map
    out.u16(0); // reserved
    out.u32(static_cast<std::uint32_t>(sections.size()));

    std::uint64_t offset = 16 + 24 * sections.size();
    for (const auto& [id, payload] : sections) {
        out.id(id);
        out.u64(offset);
        out.u64(payload.size());
        out.u8(0); // compression
        out.u8(0);
        out.u8(0);
        out.u8(0);
        offset += payload.size();
    }
    for (const auto& section : sections) out.append(section.second);
    return out.data();
}

GeometryVertex corner(float x, float y) {
    GeometryVertex vertex;
    vertex.position = {x, y, -4.0F};
    vertex.normal = {0.0F, 0.0F, -1.0F};
    vertex.u = x / 64.0F;
    vertex.v = y / 64.0F;
    return vertex;
}

/// Two triangles in two batches.
Geometry square() {
    Geometry geometry;
    geometry.vertices = {corner(0, 0), corner(64, 0), corner(64, 64), corner(0, 64)};
    geometry.indices = {0, 1, 2, 0, 2, 3};
    geometry.batches = {GeometryBatch{"", 0, 0, 3}, GeometryBatch{"dm-fixture.door", 0x4u, 3, 3}};
    return geometry;
}

/// Every field distinct between the two shapes, one location -0.0 and one
/// postScale negative.
std::vector<MoverShape> golden() {
    MoverShape door;
    door.exportIndex = 7;
    door.location = {-0.0F, 8.0F, 16.0F};
    door.rotation = {16384, -1, 3};
    door.postScale = {-1.0F, 2.0F, 0.5F};
    door.geometry = square();

    MoverShape lift;
    lift.exportIndex = 9;
    lift.location = {1.0F, 2.0F, 3.0F};
    lift.geometry = square();
    return {door, lift};
}

void sameShapes(const std::vector<MoverShape>& actual, const std::vector<MoverShape>& expected) {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const MoverShape& a = actual[i];
        const MoverShape& e = expected[i];
        CHECK(a.exportIndex == e.exportIndex);
        CHECK(a.rotation == e.rotation);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            CHECK(bitsOf(a.location[axis]) == bitsOf(e.location[axis]));
            CHECK(bitsOf(a.postScale[axis]) == bitsOf(e.postScale[axis]));
        }
        REQUIRE(a.geometry.vertices.size() == e.geometry.vertices.size());
        for (std::size_t v = 0; v < e.geometry.vertices.size(); ++v) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                CHECK(bitsOf(a.geometry.vertices[v].position[axis])
                      == bitsOf(e.geometry.vertices[v].position[axis]));
                CHECK(bitsOf(a.geometry.vertices[v].normal[axis])
                      == bitsOf(e.geometry.vertices[v].normal[axis]));
            }
            CHECK(bitsOf(a.geometry.vertices[v].u) == bitsOf(e.geometry.vertices[v].u));
            CHECK(bitsOf(a.geometry.vertices[v].v) == bitsOf(e.geometry.vertices[v].v));
        }
        CHECK(a.geometry.indices == e.geometry.indices);
        REQUIRE(a.geometry.batches.size() == e.geometry.batches.size());
        for (std::size_t b = 0; b < e.geometry.batches.size(); ++b) {
            CHECK(a.geometry.batches[b].material == e.geometry.batches[b].material);
            CHECK(a.geometry.batches[b].polyFlags == e.geometry.batches[b].polyFlags);
            CHECK(a.geometry.batches[b].firstIndex == e.geometry.batches[b].firstIndex);
            CHECK(a.geometry.batches[b].indexCount == e.geometry.batches[b].indexCount);
        }
    }
}

/// `read` refuses it with MalformedData, and `write` with InvalidArgument --
/// the same rule, blamed on the file or on the caller.
void refusedBothWays(const std::vector<MoverShape>& shapes, std::string_view says) {
    const auto result = read(fileWith({{"MOVR", movrPayload(shapes)}}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::MalformedData);
    CHECK(result.error().message().find(says) != std::string_view::npos);

    Bundle bundle;
    bundle.movers = shapes;
    const auto written = write(bundle);
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code() == ErrorCode::InvalidArgument);
    CHECK(written.error().message().find(says) != std::string_view::npos);
}

} // namespace

// ------------------------------------------------------------------ INV-1

TEST_CASE("the MOVR golden bytes decode to the shapes they encode", "[ubundle][movr]") {
    const auto result = read(fileWith({{"LITE", emptyLite()}, {"MOVR", movrPayload(golden())}}));
    REQUIRE(result.has_value());
    CHECK(result->header.formatVersion == 6);
    REQUIRE(result->lights.has_value());
    REQUIRE(result->movers.has_value());
    sameShapes(*result->movers, golden());
}

TEST_CASE("write emits MOVR after LITE as the golden bytes", "[ubundle][movr]") {
    Bundle bundle;
    bundle.header.origin = Origin::Authored;
    bundle.lights = std::vector<Light>{};
    bundle.movers = golden();
    const auto written = write(bundle);
    REQUIRE(written.has_value());
    CHECK(*written == fileWith({{"LITE", emptyLite()}, {"MOVR", movrPayload(golden())}}));
}

TEST_CASE("a shape with empty geometry encodes to exactly 52 bytes", "[ubundle][movr]") {
    Bundle bundle;
    bundle.header.origin = Origin::Authored;
    bundle.movers = std::vector<MoverShape>{MoverShape{}};
    const auto written = write(bundle);
    REQUIRE(written.has_value());
    // The header, one descriptor, the payload's own count, and the shape.
    CHECK(written->size() == 16 + 24 + 4 + 52);
    CHECK(*written == fileWith({{"MOVR", movrPayload({MoverShape{}})}}));
}

// ------------------------------------------------------------------ INV-2

TEST_CASE("two shapes of one slot are refused", "[ubundle][movr]") {
    std::vector<MoverShape> shapes = golden();
    shapes[1].exportIndex = shapes[0].exportIndex;
    refusedBothWays(shapes, "shape 1's exportIndex");
}

TEST_CASE("shapes out of order are refused", "[ubundle][movr]") {
    std::vector<MoverShape> shapes = golden();
    std::swap(shapes[0], shapes[1]);
    refusedBothWays(shapes, "shape 1's exportIndex");
}

TEST_CASE("a shape whose geometry has an index past its vertices is refused", "[ubundle][movr]") {
    std::vector<MoverShape> shapes = golden();
    shapes[0].geometry.indices[0] = 99;
    refusedBothWays(shapes, "names vertex 99");
}
