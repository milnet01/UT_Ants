// Locks INV-3, INV-4, INV-5 and INV-7 of docs/specs/UTA-0017-movement-rules.md:
// feet touch the floor, a 25-unit step is climbed and a 26-unit one stops the
// body, a dodge comes only from a double tap and never chains, and a fixed
// script hashes to one constant on every compiler.
//
// Every level here is BlockFixture's: solid blocks in empty space. The floor's
// top is z = 0, so a standing body's centre is halfHeight + SKIN.

#include "BlockFixture.h"

#include "uworld/FixedTick.h"
#include "uworld/Movement.h"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace uta::uworld;
using uta::test::blocks::Block;
using uta::test::blocks::box;
using uta::test::blocks::solidBlocks;

namespace {

const BodySize BODY{};
const MoveRules RULES{};
const double DT = DEFAULT_GAME_SPEED / TICKS_PER_SECOND;
const double STANDING = BODY.halfHeight + SKIN;

/// A floor 4000 across with its top at z = 0, and `blocks` on it.
BodyWorld levelOf(std::vector<Block> blocks) {
    blocks.push_back(box({-2000, -2000, -64}, {2000, 2000, 0}));
    uta::ubundle::Collision collision;
    collision.level = solidBlocks(blocks);
    auto world = BodyWorld::build(collision, {});
    REQUIRE(world.has_value());
    return std::move(*world);
}

MoveState standingAt(double x, double y, double floorTop = 0) {
    MoveState s;
    s.location = {x, y, floorTop + STANDING};
    return s;
}

MoveCommand forward() {
    MoveCommand c;
    c.forward = 1; // yaw 0 faces +X
    return c;
}

} // namespace

TEST_CASE("UTA-0017 INV-3: feet touch the floor, flat, up a step, down a step and after landing") {
    // A block 20 high from x = 200 to 400 across the path.
    const auto world = levelOf({box({200, -200, 0}, {400, 200, 20})});
    MoveState s = standingAt(0, 0);
    bool climbed = false, cameDown = false;
    for (int k = 0; k < 400 && s.location.x < 600; ++k) {
        s = tick(world, BODY, RULES, s, forward(), DT);
        REQUIRE(s.physics == Physics::Walking);
        if (s.location.x < 200 - BODY.radius - 1) {
            CHECK(std::abs(s.location.z - STANDING) < 0.01);
        } else if (s.location.x > 200 + BODY.radius + 1 && s.location.x < 400 - BODY.radius - 1) {
            climbed = true;
            CHECK(std::abs(s.location.z - (20 + STANDING)) < 0.01);
        } else if (s.location.x > 400 + BODY.radius + 1) {
            cameDown = true;
            CHECK(std::abs(s.location.z - STANDING) < 0.01);
        }
    }
    CHECK(climbed);
    CHECK(cameDown);

    // A jump lands back on the floor, touching it.
    MoveCommand jump;
    jump.jump = true;
    s = standingAt(-500, 0);
    s = tick(world, BODY, RULES, s, jump, DT);
    REQUIRE(s.physics == Physics::Falling);
    for (int k = 0; k < 200 && s.physics == Physics::Falling; ++k) s = tick(world, BODY, RULES, s, {}, DT);
    REQUIRE(s.physics == Physics::Walking);
    s = tick(world, BODY, RULES, s, {}, DT);
    CHECK(std::abs(s.location.z - STANDING) < 0.01);
}

TEST_CASE("UTA-0017 INV-4: a 25-unit step is climbed walking; a 26-unit one stops the body") {
    for (const double height : {25.0, 26.0}) {
        const auto world = levelOf({box({200, -200, 0}, {1500, 200, height})});
        MoveState s = standingAt(0, 0);
        for (int k = 0; k < 200; ++k) {
            s = tick(world, BODY, RULES, s, forward(), DT);
            REQUIRE(s.physics == Physics::Walking);
        }
        INFO("a step " << height << " high");
        if (height == 25) {
            CHECK(s.location.x > 200 + BODY.radius);
            CHECK(std::abs(s.location.z - (25 + STANDING)) < 0.01);
        } else {
            CHECK(s.location.x < 200 - BODY.radius);
            CHECK(std::abs(s.location.z - STANDING) < 0.01);
        }
    }
}

namespace {

/// Presses forward on the ticks in `pressed` and lets go between, through a
/// DoubleTap; returns the first dodge it makes, and the tick.
std::pair<DodgeDir, int> tapForward(const std::vector<int>& pressed, int ticks, bool ready = true) {
    DoubleTap tap(RULES.dodgeClickTime);
    for (int k = 0; k < ticks; ++k) {
        DirectionKeys keys;
        for (int p : pressed) keys.forward = keys.forward || (k >= p && k < p + 3); // held three ticks
        const DodgeDir d = tap.update(keys, DT, ready);
        if (d != DodgeDir::None) return {d, k};
    }
    return {DodgeDir::None, -1};
}

int ticksFor(double seconds) { return static_cast<int>(std::lround(seconds / DT)); }

} // namespace

TEST_CASE("UTA-0017 INV-5: a dodge comes only from a double tap inside DodgeClickTime") {
    const int apart02 = ticksFor(0.2), apart03 = ticksFor(0.3);
    CHECK(tapForward({0, apart02}, 60) == std::pair{DodgeDir::Forward, apart02});
    CHECK(tapForward({0, apart03}, 60).first == DodgeDir::None);
    CHECK(tapForward({0}, 60).first == DodgeDir::None);
    // Not ready -- in the air or just landed -- a double tap does nothing.
    CHECK(tapForward({0, apart02}, 60, false).first == DodgeDir::None);

    // Each direction dodges its own way off the view's yaw.
    const auto world = levelOf({});
    MoveCommand c;
    c.dodge = DodgeDir::Left;
    const MoveState s = tick(world, BODY, RULES, standingAt(0, 0), c, DT);
    CHECK(s.physics == Physics::Falling);
    CHECK(s.dodge == DodgeState::Active);
    CHECK(s.velocity.y < -RULES.dodgeSpeedXY + 1); // left of +X is -Y
}

TEST_CASE("UTA-0017 INV-5: a dodge is refused in the air and during DODGE_Done") {
    const auto world = levelOf({});
    MoveCommand dodge;
    dodge.dodge = DodgeDir::Forward;

    // In the air: a jump, then a dodge asked for on the way up.
    MoveCommand jump;
    jump.jump = true;
    MoveState s = tick(world, BODY, RULES, standingAt(0, 0), jump, DT);
    REQUIRE(s.physics == Physics::Falling);
    s = tick(world, BODY, RULES, s, dodge, DT);
    CHECK(s.dodge == DodgeState::None);
    CHECK(std::abs(s.velocity.x) < 1);

    // Landed from a dodge: the next is refused until dodgeDoneTime has passed.
    s = tick(world, BODY, RULES, standingAt(0, 0), dodge, DT);
    REQUIRE(s.dodge == DodgeState::Active);
    for (int k = 0; k < 200 && s.physics == Physics::Falling; ++k) s = tick(world, BODY, RULES, s, {}, DT);
    REQUIRE(s.physics == Physics::Walking);
    CHECK(s.dodge == DodgeState::Done);
    CHECK(length(s.velocity) < RULES.dodgeSpeedXY * RULES.dodgeLandingKeep + 30);
    s = tick(world, BODY, RULES, s, dodge, DT);
    CHECK(s.physics == Physics::Walking);
    CHECK_FALSE(dodgeReady(s));
    for (int k = 0; k < ticksFor(RULES.dodgeDoneTime) + 1; ++k) s = tick(world, BODY, RULES, s, {}, DT);
    CHECK(dodgeReady(s));
    s = tick(world, BODY, RULES, s, dodge, DT);
    CHECK(s.dodge == DodgeState::Active);
}

TEST_CASE("UTA-0017: a held jump jumps once") {
    const auto world = levelOf({});
    MoveCommand jump;
    jump.jump = true;
    MoveState s = standingAt(0, 0);
    int jumps = 0;
    for (int k = 0; k < 300; ++k) {
        const Physics before = s.physics;
        s = tick(world, BODY, RULES, s, jump, DT);
        if (before == Physics::Walking && s.physics == Physics::Falling) ++jumps;
    }
    CHECK(jumps == 1);
    CHECK(s.physics == Physics::Walking);
}

TEST_CASE("UTA-0017: walking off a ledge keeps its speed into the fall") {
    // A shelf 100 high ending at x = 100.
    const auto world = levelOf({box({-500, -200, 0}, {100, 200, 100})});
    MoveState s = standingAt(0, 0, 100);
    s.velocity = {RULES.groundSpeed, 0, 0};
    for (int k = 0; k < 100 && s.physics == Physics::Walking; ++k) s = tick(world, BODY, RULES, s, forward(), DT);
    REQUIRE(s.physics == Physics::Falling);
    CHECK(s.velocity.x > RULES.groundSpeed - 1);
}

TEST_CASE("UTA-0017 INV-7: a fixed script over a level with a turning mover hashes to one constant") {
    // Walls turned by Pythagorean triples, never by std::sin, and a door
    // turned by placeMover, whose sine is core::sineOf (BodyCollisionTest's
    // INV-8 records why a library sine cannot be in a cross-compiler hash).
    uta::ubundle::Collision collision;
    collision.level = solidBlocks({box({-2000, -2000, -64}, {2000, 2000, 0}),
                                   box({300, -400, 0}, {364, 400, 200}, 0.8, 0.6),
                                   box({-200, 150, 0}, {-100, 250, 25}), box({-60, -300, 0}, {20, -200, 40})});
    collision.movers.push_back({7, solidBlocks({box({-8, -64, 0}, {8, 64, 200})})});
    uta::ubundle::MoverShape shape;
    shape.exportIndex = 7;
    shape.location = {100, 300, 0};
    const std::vector<uta::ubundle::MoverShape> movers{shape};
    auto built = BodyWorld::build(collision, movers);
    REQUIRE(built.has_value());
    auto& world = *built;

    std::uint64_t hash = 14695981039346656037ULL; // FNV-1a
    const auto mix = [&](double v) {
        const auto bits = std::bit_cast<std::uint64_t>(v);
        for (int k = 0; k < 8; ++k) {
            hash ^= (bits >> (8 * k)) & 0xff;
            hash *= 1099511628211ULL;
        }
    };
    MoveState s = standingAt(0, 0);
    for (int k = 0; k < 600; ++k) {
        REQUIRE(world.placeMover(0, {100, 300, 0}, {0, k * 97, 0}).has_value());
        MoveCommand c;
        c.yaw = k * 211;
        c.forward = (k / 40) % 3 == 2 ? 0 : 1;
        c.strafe = (k / 25) % 4 == 1 ? 1 : (k / 25) % 4 == 3 ? -1 : 0;
        c.walk = (k / 90) % 5 == 4;
        c.jump = k % 70 == 30;
        if (k % 110 == 60) c.dodge = DodgeDir::Right;
        s = tick(world, BODY, RULES, s, c, DT);
        mix(s.location.x);
        mix(s.location.y);
        mix(s.location.z);
        mix(s.velocity.x);
        mix(s.velocity.y);
        mix(s.velocity.z);
    }
    INFO("hash " << hash);
    CHECK(hash == 18099172109730268130ULL);
}
