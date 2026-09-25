// UTA-0085: what a reach spec's flags allow, and whether a pawn may take an
// edge -- src/unav/Reach.h.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "unav/Reach.h"

#include <catch2/catch_test_macros.hpp>

using namespace uta::unav;

namespace {

NavEdge edge(std::int32_t flags, std::int32_t radius = 17, std::int32_t height = 39) {
    NavEdge e;
    e.reachFlags = flags;
    e.collisionRadius = radius;
    e.collisionHeight = height;
    return e;
}

} // namespace

TEST_CASE("UTA-0085: the flags carry UT99's EReachSpecFlags values", "[unav][reach]") {
    CHECK(R_WALK == 1);
    CHECK(R_FLY == 2);
    CHECK(R_SWIM == 4);
    CHECK(R_JUMP == 8);
    CHECK(R_DOOR == 16);
    CHECK(R_SPECIAL == 32);
    CHECK(R_PLAYERONLY == 64);
    CHECK(unknownReachBits(127) == 0);
    CHECK(unknownReachBits(128 | 1) == 128);
}

TEST_CASE("UTA-0085: a walking bot takes walks and jumps and not flights", "[unav][reach]") {
    const PawnMoves bot;
    CHECK(mayTraverse(edge(R_WALK), bot));
    CHECK(mayTraverse(edge(R_WALK | R_JUMP), bot));
    CHECK(mayTraverse(edge(R_WALK | R_SWIM | R_DOOR | R_SPECIAL), bot));
    CHECK_FALSE(mayTraverse(edge(R_FLY), bot));
    CHECK_FALSE(mayTraverse(edge(R_WALK | R_PLAYERONLY), bot));
    CHECK_FALSE(mayTraverse(edge(R_WALK | 128), bot)); // an unknown bit is never satisfied
}

TEST_CASE("UTA-0085: a player may take a player-only edge and a flyer a flight", "[unav][reach]") {
    PawnMoves player;
    player.player = true;
    CHECK(mayTraverse(edge(R_WALK | R_PLAYERONLY), player));
    PawnMoves flyer;
    flyer.fly = true;
    CHECK(mayTraverse(edge(R_FLY), flyer));
    PawnMoves noDoors;
    noDoors.openDoors = false;
    CHECK_FALSE(mayTraverse(edge(R_WALK | R_DOOR), noDoors));
}

TEST_CASE("UTA-0085: an edge built for a smaller body is not taken", "[unav][reach]") {
    const PawnMoves bot;
    CHECK(mayTraverse(edge(R_WALK, 17, 39), bot));
    CHECK(mayTraverse(edge(R_WALK, 40, 80), bot));
    CHECK_FALSE(mayTraverse(edge(R_WALK, 16, 39), bot));
    CHECK_FALSE(mayTraverse(edge(R_WALK, 17, 38), bot));
}
