// UTA-0111's container cases: the COLL section.
//
// docs/specs/UTA-0111-level-collision.md SS 4.2, INV-1 and INV-2. The
// builder's cases are tests/unit/BakeCollisionTest.cpp; nothing here reaches
// ubake.
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
using uta::ubundle::Collision;
using uta::ubundle::CollisionHull;
using uta::ubundle::CollisionNode;
using uta::ubundle::CollisionTree;
using uta::ubundle::HullPlane;
using uta::ubundle::MoverCollision;
using uta::ubundle::MoverShape;
using uta::ubundle::Origin;
using uta::ubundle::read;
using uta::ubundle::write;

namespace {

/// A bool byte as the structure says, rather than one a case substitutes.
constexpr std::uint8_t AS_GIVEN = 0xFF;

std::uint32_t bitsOf(float value) {
    return std::bit_cast<std::uint32_t>(value);
}

std::uint8_t boolByte(bool value, std::uint8_t substitute) {
    return substitute == AS_GIVEN ? static_cast<std::uint8_t>(value ? 1 : 0) : substitute;
}

/// A CollisionTree in SS 4.2's order: nodes, points, outline, hulls, outside.
/// `outsideByte` and `flippedByte` replace the tree's `outside` and its first
/// hull plane's `flipped`.
void putTree(Bytes& out, const CollisionTree& tree, std::uint8_t outsideByte = AS_GIVEN,
             std::uint8_t flippedByte = AS_GIVEN) {
    out.u32(static_cast<std::uint32_t>(tree.nodes.size()));
    for (const CollisionNode& node : tree.nodes) {
        for (const float part : node.normal) out.f32(part);
        out.f32(node.distance);
        out.i32(node.back);
        out.i32(node.front);
        out.i32(node.coplanar);
        out.i32(node.hull);
        out.u8(node.nodeFlags);
        out.u32(node.polyFlags);
        out.u32(node.firstOutline);
        out.u32(node.outlineCount);
    }
    out.u32(static_cast<std::uint32_t>(tree.points.size()));
    for (const auto& point : tree.points)
        for (const float part : point) out.f32(part);
    out.u32(static_cast<std::uint32_t>(tree.outline.size()));
    for (const std::uint32_t index : tree.outline) out.u32(index);
    out.u32(static_cast<std::uint32_t>(tree.hulls.size()));
    bool first = true;
    for (const CollisionHull& hull : tree.hulls) {
        out.u32(static_cast<std::uint32_t>(hull.planes.size()));
        for (const HullPlane& plane : hull.planes) {
            out.u32(plane.node);
            out.u8(boolByte(plane.flipped, first ? flippedByte : AS_GIVEN));
            first = false;
        }
        for (const float part : hull.min) out.f32(part);
        for (const float part : hull.max) out.f32(part);
    }
    out.u8(boolByte(tree.outside, outsideByte));
}

/// A COLL payload in SS 4.2's order: the level's tree, then the movers'.
Bytes collPayload(const Collision& collision, std::uint8_t outsideByte = AS_GIVEN,
                  std::uint8_t flippedByte = AS_GIVEN) {
    Bytes out;
    putTree(out, collision.level, outsideByte, flippedByte);
    out.u32(static_cast<std::uint32_t>(collision.movers.size()));
    for (const MoverCollision& mover : collision.movers) {
        out.u32(mover.exportIndex);
        putTree(out, mover.tree);
    }
    return out;
}

/// A MOVR payload holding no shape.
Bytes emptyMovr() {
    Bytes out;
    out.u32(0);
    return out;
}

/// A whole .utab holding `sections` in the order given.
std::vector<std::byte> fileWith(const std::vector<std::pair<std::string_view, Bytes>>& sections) {
    Bytes out;
    out.id("UTAB");
    out.u32(7); // formatVersion -- 7 since UTA-0111 SS 4.2
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

/// Every field distinct. The level's node 2 names node 1, stored before it, as
/// its front; node 3 is reached by no walk from node 0; node 1 has an outline;
/// the one hull has two planes, one flipped; `outside` is false. Mover 7's
/// tree holds a -0.0 point, and mover 9's one node.
Collision golden() {
    CollisionNode root;
    root.normal = {0.25F, -0.5F, 0.75F};
    root.distance = 16.0F;
    root.front = 2;
    root.hull = 0;
    root.nodeFlags = 0x40;

    CollisionNode face;
    face.normal = {0.0F, 0.0F, 1.0F};
    face.distance = 8.0F;
    face.nodeFlags = 0x04;
    face.polyFlags = 0x00000001u;
    face.firstOutline = 0;
    face.outlineCount = 4;

    CollisionNode split;
    split.normal = {1.0F, 0.0F, 0.0F};
    split.distance = -32.0F;
    split.front = 1;
    split.nodeFlags = 0x80;

    CollisionNode stray;
    stray.normal = {0.0F, 1.0F, 0.0F};
    stray.distance = 4.0F;
    stray.back = 1;
    stray.coplanar = 0;
    stray.nodeFlags = 0x10;
    stray.polyFlags = 0x04000000u;

    CollisionHull hull;
    hull.planes = {HullPlane{2, false}, HullPlane{1, true}};
    hull.min = {-8.0F, 0.5F, 1.0F};
    hull.max = {64.0F, 64.5F, 9.0F};

    Collision collision;
    collision.level.nodes = {root, face, split, stray};
    collision.level.points = {{0.0F, 0.0F, 8.0F}, {64.0F, 0.0F, 8.0F}, {64.0F, 64.0F, 8.0F},
                              {0.0F, 64.0F, 8.0F}};
    collision.level.outline = {0, 1, 2, 3};
    collision.level.hulls = {hull};
    collision.level.outside = false;

    MoverCollision door;
    door.exportIndex = 7;
    door.tree.points = {{-0.0F, 1.0F, 2.0F}};
    door.tree.outside = true;

    CollisionNode plate;
    plate.normal = {0.0F, 0.0F, -1.0F};
    plate.distance = 2.0F;
    MoverCollision lift;
    lift.exportIndex = 9;
    lift.tree.nodes = {plate};
    lift.tree.outside = true;

    collision.movers = {door, lift};
    return collision;
}

void sameTree(const CollisionTree& actual, const CollisionTree& expected) {
    REQUIRE(actual.nodes.size() == expected.nodes.size());
    for (std::size_t n = 0; n < expected.nodes.size(); ++n) {
        const CollisionNode& a = actual.nodes[n];
        const CollisionNode& e = expected.nodes[n];
        for (std::size_t axis = 0; axis < 3; ++axis)
            CHECK(bitsOf(a.normal[axis]) == bitsOf(e.normal[axis]));
        CHECK(bitsOf(a.distance) == bitsOf(e.distance));
        CHECK(a.back == e.back);
        CHECK(a.front == e.front);
        CHECK(a.coplanar == e.coplanar);
        CHECK(a.hull == e.hull);
        CHECK(a.nodeFlags == e.nodeFlags);
        CHECK(a.polyFlags == e.polyFlags);
        CHECK(a.firstOutline == e.firstOutline);
        CHECK(a.outlineCount == e.outlineCount);
    }
    REQUIRE(actual.points.size() == expected.points.size());
    for (std::size_t p = 0; p < expected.points.size(); ++p)
        for (std::size_t axis = 0; axis < 3; ++axis)
            CHECK(bitsOf(actual.points[p][axis]) == bitsOf(expected.points[p][axis]));
    CHECK(actual.outline == expected.outline);
    REQUIRE(actual.hulls.size() == expected.hulls.size());
    for (std::size_t h = 0; h < expected.hulls.size(); ++h) {
        const CollisionHull& a = actual.hulls[h];
        const CollisionHull& e = expected.hulls[h];
        REQUIRE(a.planes.size() == e.planes.size());
        for (std::size_t p = 0; p < e.planes.size(); ++p) {
            CHECK(a.planes[p].node == e.planes[p].node);
            CHECK(a.planes[p].flipped == e.planes[p].flipped);
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            CHECK(bitsOf(a.min[axis]) == bitsOf(e.min[axis]));
            CHECK(bitsOf(a.max[axis]) == bitsOf(e.max[axis]));
        }
    }
    CHECK(actual.outside == expected.outside);
}

void sameCollision(const Collision& actual, const Collision& expected) {
    sameTree(actual.level, expected.level);
    REQUIRE(actual.movers.size() == expected.movers.size());
    for (std::size_t m = 0; m < expected.movers.size(); ++m) {
        CHECK(actual.movers[m].exportIndex == expected.movers[m].exportIndex);
        sameTree(actual.movers[m].tree, expected.movers[m].tree);
    }
}

/// `read` refuses it with MalformedData, and `write` with InvalidArgument --
/// the same rule, blamed on the file or on the caller.
void refusedBothWays(const Collision& collision, std::string_view says) {
    const auto result = read(fileWith({{"COLL", collPayload(collision)}}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::MalformedData);
    CHECK(result.error().message().find(says) != std::string_view::npos);

    Bundle bundle;
    bundle.collision = collision;
    const auto written = write(bundle);
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code() == ErrorCode::InvalidArgument);
    CHECK(written.error().message().find(says) != std::string_view::npos);
}

void readRefuses(const std::vector<std::byte>& bytes, std::string_view says) {
    const auto result = read(bytes);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::MalformedData);
    CHECK(result.error().message().find(says) != std::string_view::npos);
}

} // namespace

// ------------------------------------------------------------------ INV-1

TEST_CASE("the COLL golden bytes decode to the collision they encode", "[ubundle][coll]") {
    const auto result = read(fileWith({{"MOVR", emptyMovr()}, {"COLL", collPayload(golden())}}));
    REQUIRE(result.has_value());
    CHECK(result->header.formatVersion == 7);
    REQUIRE(result->movers.has_value());
    REQUIRE(result->collision.has_value());
    sameCollision(*result->collision, golden());
}

TEST_CASE("write emits COLL after MOVR as the golden bytes", "[ubundle][coll]") {
    Bundle bundle;
    bundle.header.origin = Origin::Authored;
    bundle.movers = std::vector<MoverShape>{};
    bundle.collision = golden();
    const auto written = write(bundle);
    REQUIRE(written.has_value());
    CHECK(*written == fileWith({{"MOVR", emptyMovr()}, {"COLL", collPayload(golden())}}));
}

TEST_CASE("an empty collision encodes to exactly 21 bytes", "[ubundle][coll]") {
    Bundle bundle;
    bundle.header.origin = Origin::Authored;
    bundle.collision = Collision{};
    const auto written = write(bundle);
    REQUIRE(written.has_value());
    // The header, one descriptor, and the payload.
    CHECK(written->size() == 16 + 24 + 21);
    CHECK(*written == fileWith({{"COLL", collPayload(Collision{})}}));
}

// ------------------------------------------------------------------ INV-2

TEST_CASE("a back link naming no node is refused", "[ubundle][coll]") {
    Collision collision = golden();
    collision.level.nodes[0].back = 4;
    refusedBothWays(collision, "level: node 0's back names node 4");
}

TEST_CASE("a coplanar link naming no node is refused", "[ubundle][coll]") {
    Collision collision = golden();
    collision.level.nodes[2].coplanar = -2;
    refusedBothWays(collision, "level: node 2's coplanar names node -2");
}

TEST_CASE("a mover tree's front link naming no node is refused", "[ubundle][coll]") {
    Collision collision = golden();
    collision.movers[1].tree.nodes[0].front = 1;
    refusedBothWays(collision, "mover tree 1: node 0's front names node 1");
}

TEST_CASE("a link back to node 0 is refused as reaching it twice", "[ubundle][coll]") {
    Collision collision = golden();
    collision.level.nodes[1].back = 0;
    refusedBothWays(collision, "level: node 0 is reached twice");
}

TEST_CASE("a node two parents name is refused as reached twice", "[ubundle][coll]") {
    Collision collision = golden();
    collision.level.nodes[0].back = 1;
    refusedBothWays(collision, "level: node 1 is reached twice");
}

TEST_CASE("a hull naming no hull is refused", "[ubundle][coll]") {
    Collision collision = golden();
    collision.level.nodes[0].hull = 1;
    refusedBothWays(collision, "level: node 0's hull names hull 1");
}

TEST_CASE("an outline of two points is refused", "[ubundle][coll]") {
    Collision collision = golden();
    collision.level.nodes[1].outlineCount = 2;
    refusedBothWays(collision, "level: node 1's outlineCount is 2");
}

TEST_CASE("an outline reaching past the outline table is refused", "[ubundle][coll]") {
    Collision collision = golden();
    collision.level.nodes[1].firstOutline = 1;
    refusedBothWays(collision, "level: node 1's outline runs past");
}

TEST_CASE("an outline whose end wraps a u32 is refused", "[ubundle][coll]") {
    Collision collision = golden();
    // 4294967294 + 4 wraps to 2, inside a table of four.
    collision.level.nodes[1].firstOutline = 4294967294u;
    refusedBothWays(collision, "level: node 1's outline runs past");
}

TEST_CASE("an outline entry naming no point is refused", "[ubundle][coll]") {
    Collision collision = golden();
    collision.level.outline[2] = 4;
    refusedBothWays(collision, "level: outline entry 2 names point 4");
}

TEST_CASE("a hull plane naming no node is refused", "[ubundle][coll]") {
    Collision collision = golden();
    collision.level.hulls[0].planes[1].node = 4;
    refusedBothWays(collision, "level: hull 0's plane 1 names node 4");
}

TEST_CASE("two trees of one slot are refused", "[ubundle][coll]") {
    Collision collision = golden();
    collision.movers[1].exportIndex = collision.movers[0].exportIndex;
    refusedBothWays(collision, "mover tree 1's exportIndex");
}

TEST_CASE("trees out of order are refused", "[ubundle][coll]") {
    Collision collision = golden();
    std::swap(collision.movers[0], collision.movers[1]);
    refusedBothWays(collision, "mover tree 1's exportIndex");
}

TEST_CASE("read refuses a bool byte of 2 in outside", "[ubundle][coll]") {
    readRefuses(fileWith({{"COLL", collPayload(golden(), 2)}}), "bool byte 2");
}

TEST_CASE("read refuses a bool byte of 2 in flipped", "[ubundle][coll]") {
    readRefuses(fileWith({{"COLL", collPayload(golden(), AS_GIVEN, 2)}}), "bool byte 2");
}
