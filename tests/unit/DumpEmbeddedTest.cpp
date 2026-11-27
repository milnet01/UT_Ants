// UTA-0522: ut-dump's embedded reading, against collision trees built from
// solid blocks -- tools/ut-dump/Embedded.h.

#include "BlockFixture.h"

#include "ut-dump/Embedded.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using uta::dump::EmbeddedReading;
using uta::dump::readEmbedded;
using uta::test::blocks::box;
using uta::test::blocks::solidBlocks;
using uta::uworld::Vec3;

namespace {

// A floor 2048 thick with its top at z = 0, 1024 across, and a wall standing
// on it whose face is the plane x = 30.
const uta::ubundle::CollisionTree& level() {
    static const uta::ubundle::CollisionTree tree =
        solidBlocks({box({-512, -512, -2048}, {512, 512, 0}), box({30, -512, 0}, {64, 512, 200})});
    return tree;
}

// A Skaarj-shaped body, smaller: radius 10, half-height 20.
EmbeddedReading at(double x, double y, double z) {
    return readEmbedded(level(), Vec3{x, y, z}, 10, 20);
}

} // namespace

TEST_CASE("UTA-0522: a body on the floor is free, and one sunk past the engine's slack is embedded", "[dump]") {
    const EmbeddedReading standing = at(0, 0, 20);
    CHECK_FALSE(standing.centreSolid);
    REQUIRE(standing.floorGap.has_value());
    CHECK(std::abs(*standing.floorGap - 20) < 1e-9);
    CHECK_FALSE(standing.embedded);
    CHECK(standing.depth == 0.0);

    // SINK_SLACK is 3.5: a gap of 17 stands, a gap of 16 is embedded, as the
    // engine read UT_MonsterHunt's monsters. MHStuckMonsters' own 4 would
    // let 16 stand.
    CHECK_FALSE(at(0, 0, 17).embedded);
    const EmbeddedReading sunk = at(0, 0, 16);
    CHECK(sunk.embedded);
    // Its bottom is 4 under the floor; the tested cylinder is 1 smaller, so
    // the first push that frees it is 4.
    CHECK(sunk.depth == 4.0);
    CHECK(at(0, 0, 10).depth == 10.0);
}

TEST_CASE("UTA-0522: a body clipping a wall has a depth but is not embedded", "[dump]") {
    // Its edge reaches x = 35, 5 into the wall; the tested edge, 4.
    const EmbeddedReading clipping = at(25, 0, 40);
    CHECK_FALSE(clipping.embedded);
    CHECK(clipping.depth == 5.0);
}

TEST_CASE("UTA-0522: a centre in solid has no gap, and one too deep has no depth", "[dump]") {
    const EmbeddedReading buried = at(0, 0, -50);
    CHECK(buried.centreSolid);
    CHECK(buried.floorGap == 0.0);
    CHECK(buried.embedded);
    // Up 70 frees it; past 32 the push goes by 4, so 72.
    CHECK(buried.depth == 72.0);

    // Over 1000 under the floor and 1000 above its bottom: no push up to
    // MAX_PUSH frees it.
    const EmbeddedReading deep = at(0, 0, -1000);
    CHECK(deep.embedded);
    CHECK_FALSE(deep.depth.has_value());
}

TEST_CASE("UTA-0522: a body with nothing under it has no gap and is not embedded", "[dump]") {
    const EmbeddedReading overVoid = at(600, 0, 20);
    CHECK_FALSE(overVoid.floorGap.has_value());
    CHECK_FALSE(overVoid.embedded);
    CHECK(overVoid.depth == 0.0);
}
