// Locks INV-2 of docs/specs/UTA-0121-bot-path-seeds.md: trace returns the
// first crossing into solid, and its normal faces the start.
//
// THE TREE IS WRITTEN OUT NODE BY NODE, not built from regions as
// PathFixture.h builds its worlds. There every node is a CSG node, which sets
// `outside` whatever it arrived as, so a trace that dropped `outside` at a
// split would pass. Node 3 here is not a CSG node -- it has no outline -- so a
// segment it splits keeps the `outside` it came in with.

#include "ut-paths/Trace.h"

#include "ubundle/Bundle.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>

using uta::paths::Hit;
using uta::paths::isEmpty;
using uta::paths::trace;
using uta::paths::Vec3;
using uta::ubundle::CollisionNode;
using uta::ubundle::CollisionTree;

namespace {

CollisionNode nodeOn(std::array<float, 3> normal, float distance, std::int32_t front,
                     std::int32_t back, bool csg) {
    CollisionNode node;
    node.normal = normal;
    node.distance = distance;
    node.front = front;
    node.back = back;
    node.outlineCount = csg ? 3 : 0;
    return node;
}

/// Empty above the floor z = 0, except for a slab solid from z = 100 to 120.
CollisionTree floorAndSlab() {
    CollisionTree tree;
    tree.points = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    tree.outline = {0, 1, 2};
    tree.nodes = {
        nodeOn({0, 0, 1}, 0, 1, -1, true),       // 0: the floor; below it, solid
        nodeOn({0, 0, -1}, -100, 3, 2, true),    // 1: in front, below the slab
        nodeOn({0, 0, 1}, 120, -1, -1, true),    // 2: in front, above the slab
        nodeOn({1, 0, 0}, 0, -1, -1, false),     // 3: splits, and changes nothing
    };
    return tree;
}

bool near(double a, double b) {
    return std::abs(a - b) < 1e-9;
}

} // namespace

TEST_CASE("INV-2: trace returns the first crossing into solid and its normal faces the start",
          "[paths][trace]") {
    const CollisionTree tree = floorAndSlab();

    // Down from above the slab: its top, not the floor under it.
    const Hit top = trace(tree, {10, 0, 200}, {10, 0, -10});
    CHECK(near(top.fraction, 80.0 / 210.0));
    CHECK(top.normal == Vec3{0, 0, 1});

    // Down from under the slab: the floor.
    const Hit floor = trace(tree, {10, 0, 50}, {10, 0, -10});
    CHECK(near(floor.fraction, 50.0 / 60.0));
    CHECK(floor.normal == Vec3{0, 0, 1});

    // Up from under the slab: its underside, the normal facing down at the start.
    const Hit underside = trace(tree, {10, 0, 50}, {10, 0, 200});
    CHECK(near(underside.fraction, 50.0 / 150.0));
    CHECK(underside.normal == Vec3{0, 0, -1});

    // Across node 3's plane, wholly under the slab: nothing.
    CHECK(trace(tree, {-50, 0, 50}, {50, 0, 50}).fraction == 1);

    // Starting inside the slab.
    CHECK(trace(tree, {10, 0, 110}, {10, 0, 150}).fraction == 0);
}

TEST_CASE("isEmpty walks UTA-0111's tree from node 0", "[paths][trace]") {
    const CollisionTree tree = floorAndSlab();
    CHECK(isEmpty(tree, {10, 0, 50}));
    CHECK(isEmpty(tree, {-10, 0, 50}));
    CHECK(isEmpty(tree, {10, 0, 150}));
    CHECK_FALSE(isEmpty(tree, {10, 0, 110}));
    CHECK_FALSE(isEmpty(tree, {10, 0, -5}));
}
