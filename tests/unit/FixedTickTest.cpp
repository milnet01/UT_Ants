// Locks INV-1 of docs/specs/UTA-0017-movement-rules.md: movement is the same
// at every frame rate. A fixed script of commands, run through FixedTick with
// frames of 1/30, 1/60 and 1/144 s and an uneven mix, gives bit-identical
// states tick for tick. Also FixedTick's own contract (SS 4.7).

#include "BlockFixture.h"

#include "uworld/FixedTick.h"
#include "uworld/Movement.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

using namespace uta::uworld;
using uta::test::blocks::box;
using uta::test::blocks::solidBlocks;

namespace {

/// The script: tick k's command, the same whatever the frame rate.
MoveCommand commandAt(int k) {
    MoveCommand c;
    c.yaw = k * 150;
    c.forward = k % 120 < 90 ? 1 : 0;
    c.strafe = k % 50 < 20 ? 1 : 0;
    c.jump = k % 100 == 40;
    return c;
}

/// Each tick's state while frames of `frames` (cycled) pass, until `ticks`.
std::vector<MoveState> run(const BodyWorld& world, const std::vector<double>& frames, int ticks) {
    FixedTick clock;
    MoveState s;
    s.location = {0, 0, BodySize{}.halfHeight + SKIN};
    std::vector<MoveState> states;
    for (std::size_t f = 0; static_cast<int>(states.size()) < ticks; ++f) {
        const int due = clock.advance(frames[f % frames.size()]);
        for (int t = 0; t < due; ++t) {
            s = tick(world, BodySize{}, MoveRules{}, s, commandAt(static_cast<int>(states.size())), clock.dt());
            states.push_back(s);
        }
    }
    states.resize(static_cast<std::size_t>(ticks));
    return states;
}

} // namespace

TEST_CASE("UTA-0017 INV-1: the same script gives the same states at every frame rate") {
    uta::ubundle::Collision collision;
    collision.level = solidBlocks({box({-3000, -3000, -64}, {3000, 3000, 0}), box({400, -100, 0}, {500, 900, 20})});
    auto world = BodyWorld::build(collision, {});
    REQUIRE(world.has_value());

    constexpr int TICKS = 400;
    const auto reference = run(*world, {1.0 / 30}, TICKS);
    for (const auto& frames : std::vector<std::vector<double>>{{1.0 / 60}, {1.0 / 144}, {0.004, 0.031, 0.0167, 0.009, 0.05}}) {
        const auto states = run(*world, frames, TICKS);
        for (int k = 0; k < TICKS; ++k) {
            INFO("tick " << k << " with frames of " << frames.front() << " s first");
            const auto& a = reference[static_cast<std::size_t>(k)];
            const auto& b = states[static_cast<std::size_t>(k)];
            REQUIRE(a.location == b.location);
            REQUIRE(a.velocity == b.velocity);
            REQUIRE(a.physics == b.physics);
        }
    }
}

TEST_CASE("UTA-0017: FixedTick runs 85 ticks a wall second, each gameSpeed / 85 game seconds") {
    FixedTick clock;
    CHECK(clock.dt() == DEFAULT_GAME_SPEED / TICKS_PER_SECOND);
    CHECK(FixedTick(1.0).dt() == 1.0 / TICKS_PER_SECOND);
    int ticks = 0;
    for (int f = 0; f < 600; ++f) ticks += clock.advance(1.0 / 60); // ten seconds
    CHECK(std::abs(ticks - 10 * TICKS_PER_SECOND) <= 1);
    CHECK(clock.blend() >= 0);
    CHECK(clock.blend() < 1);
}

TEST_CASE("UTA-0017: FixedTick drops what a stall owes past eight ticks, and ignores bad time") {
    FixedTick clock;
    CHECK(clock.advance(5.0) == MAX_TICKS_PER_ADVANCE);
    CHECK(clock.advance(0.0) == 0);
    CHECK(clock.advance(-1.0) == 0);
    CHECK(clock.advance(std::numeric_limits<double>::quiet_NaN()) == 0);
    CHECK(clock.advance(std::numeric_limits<double>::infinity()) == 0);
    // Half a tick, then half again, makes one.
    FixedTick halves;
    CHECK(halves.advance(0.5 / TICKS_PER_SECOND) == 0);
    CHECK(halves.blend() == 0.5);
    CHECK(halves.advance(0.5 / TICKS_PER_SECOND) == 1);
}
