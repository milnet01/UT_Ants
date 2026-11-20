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

namespace {

/// Presses jump for one tick from `s`, then lets go until the body stops
/// rising; returns that state and how far it rose from the press.
std::pair<MoveState, double> jumpToTop(const BodyWorld& world, const MoveRules& rules, MoveState s) {
    MoveCommand jump;
    jump.jump = true;
    const double from = s.location.z;
    s = tick(world, BODY, rules, s, jump, DT);
    for (int k = 0; k < 300 && s.velocity.z > 0; ++k) s = tick(world, BODY, rules, s, {}, DT);
    return {s, s.location.z - from};
}

} // namespace

TEST_CASE("UTA-0017: a jump in the air rises exactly as high as the first, once per fall") {
    const auto world = levelOf({});
    MoveRules rules = RULES;
    rules.airJumps = 1;

    const auto [top, first] = jumpToTop(world, rules, standingAt(0, 0));
    REQUIRE(top.physics == Physics::Falling);
    CHECK(first > 50);
    const auto [higher, second] = jumpToTop(world, rules, top);
    CHECK(std::abs(second - first) < 1e-6);
    CHECK(higher.location.z > top.location.z + 50);

    // A third press does nothing: the velocity stays gravity's.
    const auto [still, third] = jumpToTop(world, rules, higher);
    CHECK(third < 1e-6);
    CHECK(still.airJumpsUsed == 1);

    // Landing gives the air jump back.
    MoveState s = still;
    for (int k = 0; k < 600 && s.physics == Physics::Falling; ++k) s = tick(world, BODY, rules, s, {}, DT);
    REQUIRE(s.physics == Physics::Walking);
    CHECK(s.airJumpsUsed == 0);
}

TEST_CASE("UTA-0017: the original's rules make no jump in the air") {
    const auto world = levelOf({});
    const auto [top, first] = jumpToTop(world, RULES, standingAt(0, 0));
    const auto [after, second] = jumpToTop(world, RULES, top);
    CHECK(second < 1e-6);
    CHECK(after.airJumpsUsed == 0);
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

TEST_CASE("UTA-0022: every physical platforming assist is off by default") {
    CHECK(RULES.coyoteTime == 0);
    CHECK(RULES.mantleHeight == 0);
    CHECK(RULES.extraStepHeight == 0);
}

namespace {

/// Runs off a shelf 100 high ending at x = 100; the state on the first
/// falling tick.
MoveState walkedOff(const BodyWorld& world, const MoveRules& rules) {
    MoveState s = standingAt(0, 0, 100);
    s.velocity = {rules.groundSpeed, 0, 0};
    for (int k = 0; k < 100 && s.physics == Physics::Walking; ++k) s = tick(world, BODY, rules, s, forward(), DT);
    REQUIRE(s.physics == Physics::Falling);
    return s;
}

/// `s` after `ticks` falling ticks pushing forward, then a jump pressed.
MoveState jumpAfter(const BodyWorld& world, const MoveRules& rules, MoveState s, int ticks) {
    for (int k = 0; k < ticks; ++k) s = tick(world, BODY, rules, s, forward(), DT);
    MoveCommand jump = forward();
    jump.jump = true;
    return tick(world, BODY, rules, s, jump, DT);
}

} // namespace

TEST_CASE("UTA-0022: coyote time lets a jump start just off a ledge, using no air jump") {
    const auto world = levelOf({box({-500, -200, 0}, {100, 200, 100})});
    MoveRules rules = RULES;
    rules.coyoteTime = COYOTE_TIME_ON;
    rules.airJumps = 1;
    const int window = static_cast<int>(COYOTE_TIME_ON / DT); // falling ticks still inside it

    const MoveState late = jumpAfter(world, rules, walkedOff(world, rules), window - 1);
    CHECK(late.velocity.z > 0.9 * rules.jumpZ);
    CHECK(late.airJumpsUsed == 0);
    CHECK(late.coyoteLeft == 0);

    // Past the window, the press is the air jump.
    const MoveState after = jumpAfter(world, rules, walkedOff(world, rules), window + 2);
    CHECK(after.airJumpsUsed == 1);

    // Off, as the original: no jump at all.
    const MoveState off = jumpAfter(world, RULES, walkedOff(world, RULES), 1);
    CHECK(off.velocity.z < 0);
}

TEST_CASE("UTA-0022: a falling body holding jump against a ledge in reach climbs onto it") {
    MoveRules rules = RULES;
    rules.mantleHeight = MANTLE_HEIGHT_ON;
    // Jumps from against a wall, holding forward throughout, and jump too
    // where `holdJump` says so.
    const auto run = [&](const BodyWorld& world, const MoveRules& r, bool holdJump = true) {
        MoveState s = standingAt(200 - BODY.radius - 1, 0);
        bool mantled = false;
        for (int k = 0; k < 300; ++k) {
            MoveCommand c = forward();
            c.jump = k == 0 || holdJump;
            s = tick(world, BODY, r, s, c, DT);
            if (s.physics == Physics::Mantling) {
                mantled = true;
                CHECK(push(s, {}).physics == Physics::Falling); // knocked off mid-climb
            }
        }
        return std::pair{s, mantled};
    };

    const auto wall = levelOf({box({200, -200, 0}, {1900, 200, 100})}); // longer than 300 ticks' walk
    const auto [up, mantled] = run(wall, rules);
    CHECK(mantled);
    CHECK(up.physics == Physics::Walking);
    CHECK(up.location.x > 200 + BODY.radius);
    CHECK(std::abs(up.location.z - (100 + STANDING)) < 0.01);

    // Off, as the original; or with jump let go after the take-off: the
    // body stays below.
    for (const auto& [r, holdJump] : {std::pair{RULES, true}, std::pair{rules, false}}) {
        const auto [below, any] = run(wall, r, holdJump);
        CHECK_FALSE(any);
        CHECK(below.location.x < 200);
    }

    // A ledge beyond a jump's height plus mantleHeight is out of reach.
    const auto high = levelOf({box({200, -200, 0}, {1500, 200, 140})});
    CHECK_FALSE(run(high, rules).second);

    // No room for the body on top: a ceiling 50 above the ledge.
    const auto low = levelOf({box({200, -200, 0}, {1500, 200, 100}), box({200, -200, 150}, {1500, 200, 300})});
    CHECK_FALSE(run(low, rules).second);
}

TEST_CASE("UTA-0022: extraStepHeight raises the step walked up, not the step walked down") {
    MoveRules rules = RULES;
    rules.extraStepHeight = EXTRA_STEP_HEIGHT_ON;
    const double most = RULES.maxStepHeight + EXTRA_STEP_HEIGHT_ON;
    for (const double height : {most, most + 1}) {
        const auto world = levelOf({box({200, -200, 0}, {1500, 200, height})});
        MoveState s = standingAt(0, 0);
        for (int k = 0; k < 200; ++k) s = tick(world, BODY, rules, s, forward(), DT);
        INFO("a step " << height << " high");
        CHECK(s.physics == Physics::Walking);
        CHECK((s.location.x > 200 + BODY.radius) == (height == most));
    }

    // Walking off a step higher than maxStepHeight still falls.
    const auto world = levelOf({box({-500, -200, 0}, {100, 200, RULES.maxStepHeight + 1})});
    MoveState s = standingAt(0, 0, RULES.maxStepHeight + 1);
    bool fell = false;
    for (int k = 0; k < 100; ++k) {
        s = tick(world, BODY, rules, s, forward(), DT);
        fell = fell || s.physics == Physics::Falling;
    }
    CHECK(fell);
}

TEST_CASE("UTA-0021 INV-1: projectLanding predicts the real landing exactly") {
    // A shelf 100 high ending at x = 100; the body jumps off it running.
    const auto world = levelOf({box({-500, -500, 0}, {100, 500, 100})});
    MoveRules rules = RULES;
    rules.airJumps = 1; // so a kept jump bit would jump again and lengthen the arc
    MoveCommand held = forward();
    held.strafe = 0.5;
    held.yaw = 2048;
    MoveCommand takeOff = held;
    takeOff.jump = true;

    MoveState s = standingAt(60, 0, 100);
    s.velocity = {RULES.groundSpeed, 0, 0};
    s = tick(world, BODY, rules, s, takeOff, DT);
    REQUIRE(s.physics == Physics::Falling);
    s = tick(world, BODY, rules, s, held, DT); // the jump key is let go
    REQUIRE(!s.jumpHeld);

    // Projected with a fresh jump press in the command: only clearing the bit
    // keeps the air jump out, as the real run below never makes one.
    MoveCommand pressed = held;
    pressed.jump = true;
    const auto landing = projectLanding(world, BODY, rules, s, pressed, DT);

    MoveState real = s;
    int ticks = 0;
    while (real.physics == Physics::Falling && ticks < 1000) {
        real = tick(world, BODY, rules, real, held, DT);
        ++ticks;
    }
    REQUIRE(real.physics == Physics::Walking);
    CHECK(real.location.z < 100); // it cleared the shelf and landed on the floor
    REQUIRE(landing.has_value());
    CHECK(landing->ticks == ticks);
    CHECK(landing->location.x == real.location.x);
    CHECK(landing->location.y == real.location.y);
    CHECK(landing->location.z == real.location.z);
}

TEST_CASE("UTA-0021 INV-2: no landing for a walking body, or past the horizon") {
    const auto world = levelOf({});
    CHECK_FALSE(projectLanding(world, BODY, RULES, standingAt(0, 0), {}, DT).has_value());

    MoveState high = standingAt(0, 0, 500);
    high.physics = Physics::Falling;
    CHECK_FALSE(projectLanding(world, BODY, RULES, high, {}, DT, 10).has_value());
    // The same fall lands inside the default horizon, so the 10 is what refused it.
    const auto landing = projectLanding(world, BODY, RULES, high, {}, DT);
    REQUIRE(landing.has_value());
    CHECK(std::abs(landing->location.z - STANDING) < 1);
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
    // Moved 2026-11-20 with UTA-0479: a body stopped by a surface it has
    // crept right up to now steps back to SKIN and slides on.
    CHECK(hash == 4271611235578567045ULL);
}
