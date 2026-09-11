// UTA-0109's container cases: the GEOM section.
//
// docs/specs/UTA-0109-map-geometry.md SS 4.2, INV-1 and INV-2. The builder's
// cases are tests/unit/BakeGeometryTest.cpp; nothing here reaches ubake.
//
// THE GOLDEN BYTES ARE AUTHORED FROM SS 4.2, never produced by `write`, for
// BundleTextureTest.cpp's reason: a transposition present in both the reader
// and the writer round-trips perfectly, so each is graded against the same
// bytes instead.
//
// EACH INV-2 FIXTURE BREAKS ONE RULE. Remove the rule it names and nothing
// else refuses it -- the count-of-four case tiles its four indices exactly,
// and the wrap case's counts are all multiples of three.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "ubundle/Bundle.h"

#include "Bytes.h"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using uta::ErrorCode;
using uta::testing::Bytes;
using uta::ubundle::Bundle;
using uta::ubundle::Geometry;
using uta::ubundle::GeometryBatch;
using uta::ubundle::GeometryVertex;
using uta::ubundle::MaterialRecord;
using uta::ubundle::Origin;
using uta::ubundle::read;
using uta::ubundle::write;

namespace {

std::uint32_t bitsOf(float value) {
    return std::bit_cast<std::uint32_t>(value);
}

/// A signalling NaN: quiet bit clear, non-zero payload. A quiet NaN keeps its
/// bits through a double, so only this one shows a float widened in transit.
const float SIGNALLING_NAN = std::bit_cast<float>(0x7FA01234u);

/// A GEOM payload in SS 4.2's order: vertices, indices, batches. Written field
/// by field from the spec's table, never through the codec.
Bytes geomPayload(const Geometry& geometry) {
    Bytes out;
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
    return out;
}

/// A whole .utab carrying exactly one GEOM section holding `payload`.
std::vector<std::byte> fileWith(const Bytes& payload) {
    Bytes out;
    out.id("UTAB");
    out.u32(5); // formatVersion -- 5 since UTA-0110 SS 4.4
    out.u8(1);  // origin: Authored
    out.u8(0);  // kind: Map
    out.u16(0); // reserved
    out.u32(1); // sectionCount

    out.id("GEOM");
    out.u64(16 + 24); // offset: the table ends here
    out.u64(payload.size());
    out.u8(0); // compression
    out.u8(0);
    out.u8(0);
    out.u8(0);

    out.append(payload);
    return out.data();
}

GeometryVertex vertexAt(float x, float y, float u, float v) {
    GeometryVertex vertex;
    vertex.position = {x, y, 8.0F};
    vertex.normal = {0.0F, 0.0F, 1.0F};
    vertex.u = u;
    vertex.v = v;
    return vertex;
}

/// Two triangles in two batches, every float distinct, one position -0.0 and
/// one coordinate a signalling NaN.
Geometry golden() {
    Geometry geometry;
    geometry.vertices = {vertexAt(-0.0F, 0.0F, 0.0F, 0.0F), vertexAt(64.0F, 0.0F, 0.5F, 0.0F),
                         vertexAt(64.0F, 64.0F, SIGNALLING_NAN, 1.0F),
                         vertexAt(0.0F, 64.0F, 0.0F, 1.0F)};
    geometry.indices = {0, 1, 2, 0, 2, 3};
    geometry.batches = {GeometryBatch{"", 0, 0, 3},
                        GeometryBatch{"dm-fixture.floor", 0x04000000u, 3, 3}};
    return geometry;
}

void sameBits(const Geometry& actual, const Geometry& expected) {
    REQUIRE(actual.vertices.size() == expected.vertices.size());
    for (std::size_t i = 0; i < expected.vertices.size(); ++i) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            CHECK(bitsOf(actual.vertices[i].position[axis])
                  == bitsOf(expected.vertices[i].position[axis]));
            CHECK(bitsOf(actual.vertices[i].normal[axis])
                  == bitsOf(expected.vertices[i].normal[axis]));
        }
        CHECK(bitsOf(actual.vertices[i].u) == bitsOf(expected.vertices[i].u));
        CHECK(bitsOf(actual.vertices[i].v) == bitsOf(expected.vertices[i].v));
    }
    CHECK(actual.indices == expected.indices);
    REQUIRE(actual.batches.size() == expected.batches.size());
    for (std::size_t i = 0; i < expected.batches.size(); ++i) {
        CHECK(actual.batches[i].material == expected.batches[i].material);
        CHECK(actual.batches[i].polyFlags == expected.batches[i].polyFlags);
        CHECK(actual.batches[i].firstIndex == expected.batches[i].firstIndex);
        CHECK(actual.batches[i].indexCount == expected.batches[i].indexCount);
    }
}

/// Three vertices, `indices`, and `batches`: the shape every INV-2 case takes.
Geometry shaped(std::vector<std::uint32_t> indices, std::vector<GeometryBatch> batches) {
    Geometry geometry;
    geometry.vertices = {vertexAt(0, 0, 0, 0), vertexAt(1, 0, 0, 0), vertexAt(0, 1, 0, 0)};
    geometry.indices = std::move(indices);
    geometry.batches = std::move(batches);
    return geometry;
}

/// `read` refuses it with MalformedData naming `says`, and `write` refuses it
/// with InvalidArgument -- the same rule, blamed on the file or on the caller.
void refusedBothWays(const Geometry& geometry, std::string_view says) {
    const auto result = read(fileWith(geomPayload(geometry)));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::MalformedData);
    CHECK(result.error().message().find(says) != std::string_view::npos);

    Bundle bundle;
    bundle.geometry = geometry;
    const auto written = write(bundle);
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code() == ErrorCode::InvalidArgument);
    CHECK(written.error().message().find(says) != std::string_view::npos);
}

} // namespace

TEST_CASE("the GEOM golden bytes decode to the geometry they encode", "[ubundle][geom]") {
    // INV-1, the reader's half.
    const auto result = read(fileWith(geomPayload(golden())));
    REQUIRE(result.has_value());
    CHECK(result->header.formatVersion == 5);
    REQUIRE(result->geometry.has_value());
    sameBits(*result->geometry, golden());
}

TEST_CASE("write emits the GEOM golden bytes", "[ubundle][geom]") {
    // INV-1, the writer's half.
    Bundle bundle;
    bundle.header.origin = Origin::Authored;
    bundle.geometry = golden();
    const auto written = write(bundle);
    REQUIRE(written.has_value());
    CHECK(*written == fileWith(geomPayload(golden())));
}

TEST_CASE("GEOM round-trips through write and read with every float's bits", "[ubundle][geom]") {
    // INV-1. An empty geometry is legal and distinct from an absent one
    // (UTA-0008 SS 4.4).
    for (const Geometry& geometry : {golden(), Geometry{}}) {
        Bundle bundle;
        bundle.geometry = geometry;
        const auto written = write(bundle);
        REQUIRE(written.has_value());
        const auto back = read(*written);
        REQUIRE(back.has_value());
        REQUIRE(back->geometry.has_value());
        sameBits(*back->geometry, geometry);
    }
}

TEST_CASE("write emits GEOM after MATS", "[ubundle][geom]") {
    // INV-1: appended, so UTA-0008 SS 4.10's order clause is extended.
    Bundle bundle;
    bundle.materials = std::vector<MaterialRecord>{};
    bundle.geometry.emplace();
    const auto written = write(bundle);
    REQUIRE(written.has_value());
    REQUIRE(written->size() > 16 + 2 * 24);
    const auto idAt = [&](std::size_t offset) {
        std::string id;
        for (std::size_t i = 0; i < 4; ++i) id += static_cast<char>((*written)[offset + i]);
        return id;
    };
    CHECK(idAt(16) == "MATS");
    CHECK(idAt(16 + 24) == "GEOM");
}

TEST_CASE("a GEOM index past the vertex table is refused", "[ubundle][geom]") {
    refusedBothWays(shaped({0, 1, 3}, {{"a", 0, 0, 3}}), "names vertex 3 of 3");
}

TEST_CASE("GEOM batches out of key order are refused", "[ubundle][geom]") {
    // Two of one material whose flags descend.
    refusedBothWays(shaped({0, 1, 2, 0, 1, 2}, {{"a", 2, 0, 3}, {"a", 1, 3, 3}}),
                    "does not sort strictly after");
}

TEST_CASE("two GEOM batches of one key are refused", "[ubundle][geom]") {
    // Strictly ascending rules out a repeat; a `<=` check would admit it.
    refusedBothWays(shaped({0, 1, 2, 0, 1, 2}, {{"a", 1, 0, 3}, {"a", 1, 3, 3}}),
                    "does not sort strictly after");
}

TEST_CASE("a GEOM batch of no triangles is refused", "[ubundle][geom]") {
    // Zero indices and no index table: the batch tiles, and only its count is wrong.
    refusedBothWays(shaped({}, {{"a", 0, 0, 0}}), "not a whole number of triangles");
}

TEST_CASE("a GEOM batch of four indices is refused", "[ubundle][geom]") {
    // It tiles its four indices exactly; only the multiple of three is wrong.
    refusedBothWays(shaped({0, 1, 2, 0}, {{"a", 0, 0, 4}}), "not a whole number of triangles");
}

TEST_CASE("a first GEOM batch not starting at 0 is refused", "[ubundle][geom]") {
    // Its count still sums to the table's size, so only the start is wrong.
    refusedBothWays(shaped({0, 1, 2}, {{"a", 0, 3, 3}}), "does not start where");
}

TEST_CASE("a gap between two GEOM batches is refused", "[ubundle][geom]") {
    // The counts sum to the table's size, so only the second start is wrong.
    refusedBothWays(shaped({0, 1, 2, 0, 1, 2}, {{"a", 0, 0, 3}, {"b", 0, 6, 3}}),
                    "does not start where");
}

TEST_CASE("GEOM indices past the last batch are refused", "[ubundle][geom]") {
    refusedBothWays(shaped({0, 1, 2, 0, 1, 2}, {{"a", 0, 0, 3}}), "do not end at the end");
}

TEST_CASE("GEOM indices with no batch are refused", "[ubundle][geom]") {
    refusedBothWays(shaped({0, 1, 2}, {}), "do not end at the end");
}

TEST_CASE("GEOM batches that tile only when summed in 32 bits are refused", "[ubundle][geom]") {
    // {3, 4294967295} ends at 2 in 32 bits, exactly where {2, 3} begins, and
    // {2, 3} ends at the table's five indices. 4294967295 is a multiple of
    // three. Summed in 64 bits the second batch ends at 4294967298.
    refusedBothWays(shaped({0, 1, 2, 0, 1},
                           {{"a", 0, 0, 3}, {"b", 0, 3, 4294967295u}, {"c", 0, 2, 3}}),
                    "does not start where");
}

TEST_CASE("a GEOM count the section cannot hold is refused before an element is read",
          "[ubundle][geom]") {
    // SS 4.2's minimums: 32 bytes a vertex, 16 a batch. Each payload declares
    // two elements and holds one plus less than a second, so the count is
    // refused up front. A smaller minimum admits the count and fails later on
    // a short read -- a different refusal, which is what these tell apart.
    Bytes vertices;
    vertices.u32(2);
    for (int part = 0; part < 8; ++part) vertices.f32(0.0F);
    vertices.u32(0); // indices
    vertices.u32(0); // batches
    const auto tooManyVertices = read(fileWith(vertices));
    REQUIRE_FALSE(tooManyVertices.has_value());
    CHECK(tooManyVertices.error().message().find("exceeds the bytes remaining")
          != std::string_view::npos);

    Bytes batches;
    batches.u32(0); // vertices
    batches.u32(0); // indices
    batches.u32(2);
    batches.str("");
    batches.u32(0);
    batches.u32(0);
    batches.u32(0);
    const auto tooManyBatches = read(fileWith(batches));
    REQUIRE_FALSE(tooManyBatches.has_value());
    CHECK(tooManyBatches.error().message().find("exceeds the bytes remaining")
          != std::string_view::npos);
}
