// Locks docs/specs/UTA-0019-weapon-set.md INV-4 (behaviour comes from the
// file), INV-5 (the tick is deterministic), INV-7 (ammo is never spent below
// zero), INV-8 (a combo needs a hit) and INV-9 (a body is hit where its
// cylinder is), on BlockFixture's hand-built levels. And
// docs/specs/UTA-0020-weapon-wheel.md INV-1, INV-2, INV-4, INV-5 and INV-6:
// the open wheel's time scale, and what the wheel lists and picks.

#include "BlockFixture.h"

#include "ugame/Fire.h"
#include "ugame/Game.h"
#include "uworld/FixedTick.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <set>
#include <tuple>
#include <numbers>
#include <utility>
#include <vector>

using namespace uta::ugame;
using uta::test::blocks::box;
using uta::test::blocks::solidBlocks;
using uta::uworld::BodyWorld;
using uta::uworld::MoveCommand;
using uta::uworld::MoveRules;

namespace {

const double DT = uta::uworld::DEFAULT_GAME_SPEED / uta::uworld::TICKS_PER_SECOND;
const double STANDING = uta::uworld::BodySize{}.halfHeight + uta::uworld::SKIN;
const MoveRules RULES{};

/// A floor 6000 across, its top at z = 0, and a wall at x = 2000.
BodyWorld arena() {
    uta::ubundle::Collision collision;
    collision.level = solidBlocks({box({-3000, -3000, -64}, {3000, 3000, 0}), box({2000, -3000, 0}, {2064, 3000, 600})});
    auto world = BodyWorld::build(collision, {});
    REQUIRE(world.has_value());
    return std::move(*world);
}

const WeaponSet& stock() {
    static const WeaponSet set = load(shippedDirectory() / "stock.weapons").value();
    return set;
}

int weaponNamed(const WeaponSet& set, std::string_view name) {
    const auto at = std::ranges::find(set.weapons, name, &Weapon::name);
    REQUIRE(at != set.weapons.end());
    return static_cast<int>(at - set.weapons.begin());
}

const FireMode& primaryOf(const WeaponSet& set, std::string_view weapon) {
    return set.weapons[static_cast<std::size_t>(weaponNamed(set, weapon))].primary;
}

/// Where a level shot along +X from a body at the origin starts: its eye
/// plus the mode's offset, the right-hand axis being +Y.
Vec3 levelStart(const FireMode& mode) {
    return {mode.offset[0], mode.offset[1], STANDING + EYE_HEIGHT + mode.offset[2]};
}

/// `shot` as it was launched this tick: a shot flies in the tick it is
/// fired, so its one move is backed out. For a shot that struck nothing.
Shot launchedThisTick(const WeaponSet& set, const Shot& shot) {
    Shot was = shot;
    const Projectile& kind = set.projectiles[shot.kind];
    const Vec3 moving = shot.velocity - Vec3{0, 0, RULES.gravityZ * kind.gravityAfter} * DT;
    was.location = shot.location - moving * DT;
    was.velocity = moving - (Vec3{0, 0, RULES.gravityZ * kind.gravity} + shot.acceleration) * DT;
    return was;
}

/// Damage as it lands: whole, scaled by the set's damage-scale, whole again.
double landed(const WeaponSet& set, double damage) { return std::trunc(std::trunc(damage) * set.damageScale); }

/// A shooter at the origin facing +X with `weapon` in hand and full ammo,
/// and a dummy `distance` ahead.
Game duel(const WeaponSet& set, std::string_view weapon, double distance) {
    Game game;
    game.bodies.push_back(bodyAt(set, {0, 0, STANDING}, 0));
    giveAll(set, game.bodies[0]);
    game.bodies[0].weapon = weaponNamed(set, weapon);
    game.bodies.push_back(bodyAt(set, {distance, 0, STANDING}, 32768));
    game.bodies[1].health = 1e9; // never dies, as SS 4.7's target
    return game;
}

/// `ticks` ticks of the shooter holding fire (or alt), the dummy idle.
void hold(const BodyWorld& world, const WeaponSet& set, Game& game, int ticks, bool alt = false) {
    std::vector<MoveCommand> commands(game.bodies.size());
    commands[0].fire = !alt;
    commands[0].altFire = alt;
    commands[1].yaw = 32768;
    for (int k = 0; k < ticks; ++k) tick(world, set, RULES, game, commands, DT);
}

/// How many events of `kind` `ticks` ticks of held fire make.
int eventsWhileHeld(const BodyWorld& world, const WeaponSet& set, Game& game, int ticks, Event::Kind kind) {
    std::vector<MoveCommand> commands(game.bodies.size());
    commands[0].fire = true;
    commands[1].yaw = 32768;
    int count = 0;
    for (int k = 0; k < ticks; ++k) {
        tick(world, set, RULES, game, commands, DT);
        count += static_cast<int>(std::ranges::count(game.events, kind, &Event::kind));
    }
    return count;
}

} // namespace

TEST_CASE("INV-9: a segment hits an upright cylinder where it is, not a sphere or a box", "[ugame][game]") {
    const Vec3 centre{100, 0, 0};
    const double radius = 17, half = 39;
    // Passing beside the axis, level, at mid height.
    CHECK(entersCylinder({0, radius - 0.01, 0}, {200, radius - 0.01, 0}, centre, radius, half) <= 1);
    CHECK(entersCylinder({0, radius + 0.01, 0}, {200, radius + 0.01, 0}, centre, radius, half) > 1);
    // Through the axis, just above and just below; and just inside the top.
    CHECK(entersCylinder({0, 0, half + 0.01}, {200, 0, half + 0.01}, centre, radius, half) > 1);
    CHECK(entersCylinder({0, 0, -half - 0.01}, {200, 0, -half - 0.01}, centre, radius, half) > 1);
    CHECK(entersCylinder({0, 0, half - 0.01}, {200, 0, half - 0.01}, centre, radius, half) <= 1);
    // A sphere of the radius would miss here: the cylinder's corner is hit.
    CHECK(entersCylinder({100 + radius - 0.5, 0, half + 10}, {100 + radius - 0.5, 0, half - 10}, centre, radius,
                         half) <= 1);
    // Sloping down over the head, above the axis: the top is a miss.
    CHECK(entersCylinder({0, 0, half + 50}, {200, 0, half + 10}, centre, radius, half) > 1);
    // A box of the radius would hit here, at its corner: the cylinder is missed.
    CHECK(entersCylinder({0, radius - 1, 0}, {200, radius - 1, 0}, {100, 0, 0}, radius - 2, half) > 1);
    // Entry is where it enters, and a start inside enters at once.
    CHECK(entersCylinder({0, 0, 0}, {200, 0, 0}, centre, radius, half) == (100.0 - radius) / 200.0);
    CHECK(entersCylinder({100, 0, 0}, {200, 0, 0}, centre, radius, half) == 0);
}

TEST_CASE("INV-9: an instant hit hurts a body only where its cylinder is", "[ugame][game]") {
    const BodyWorld world = arena();
    for (const double offset : {16.99, 17.01}) {
        Game game = duel(stock(), "shock-rifle", 600);
        // The shot passes `offset` from the axis.
        game.bodies[1].move.location.y = levelStart(primaryOf(stock(), "shock-rifle")).y + offset;
        hold(world, stock(), game, 1);
        CAPTURE(offset);
        CHECK((game.bodies[1].health < 1e9) == (offset < 17));
    }
}

TEST_CASE("INV-8: an instant hit sets off a combo only by entering the ball", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    const auto ball = static_cast<std::uint32_t>(
        std::ranges::find(set.projectiles, "shock-ball", &Projectile::name) - set.projectiles.begin());
    const double radius = set.projectiles[ball].radius;
    for (const double offset : {radius - 0.01, radius + 0.01}) {
        Game game = duel(set, "shock-rifle", 1900);
        game.bodies[1].move.location.y = 500; // out of the line of fire
        // A still ball 800 ahead, `offset` beside the shot's line.
        const Vec3 start = levelStart(primaryOf(set, "shock-rifle"));
        game.shots.push_back(Shot{.kind = ball, .owner = 0, .location = {800, start.y + offset, start.z},
                                  .velocity = {}, .lifetime = set.projectiles[ball].lifetime});
        hold(world, set, game, 1);
        const bool combo = std::ranges::count(game.events, Event::Kind::Combo, &Event::kind) == 1;
        CAPTURE(offset);
        CHECK(combo == (offset < radius));
        CHECK(game.shots.empty() == combo);
    }
}

TEST_CASE("INV-7: a mode with too little ammo fires nothing and changes nothing", "[ugame][game]") {
    const BodyWorld world = arena();
    for (const std::string_view weapon : {"shock-rifle", "rocket-launcher", "pulse-gun", "flak-cannon"}) {
        CAPTURE(weapon);
        Game held = duel(stock(), weapon, 600);
        held.bodies[0].weapons[static_cast<std::size_t>(held.bodies[0].weapon)].ammo = 0;
        Game idle = held;
        hold(world, stock(), held, 40);
        std::vector<MoveCommand> nothing(2);
        nothing[1].yaw = 32768;
        for (int k = 0; k < 40; ++k) tick(world, stock(), RULES, idle, nothing, DT);
        CHECK(stateHash(held) == stateHash(idle));
        CHECK(held.shots.empty());
        CHECK(held.bodies[1].health == 1e9);
    }
}

TEST_CASE("INV-7: a charged count is cut to the ammo there is", "[ugame][game]") {
    const BodyWorld world = arena();
    Game game = duel(stock(), "rocket-launcher", 1900);
    game.bodies[0].weapons[static_cast<std::size_t>(game.bodies[0].weapon)].ammo = 2;
    hold(world, stock(), game, 600); // a full charge would be six
    CHECK(game.bodies[0].weapons[static_cast<std::size_t>(game.bodies[0].weapon)].ammo == 0);

    // A continuous count charge pays for the rest of its rounds as it fires.
    WeaponSet continuous = stock();
    continuous.weapons[static_cast<std::size_t>(weaponNamed(continuous, "rocket-launcher"))].primary.charge->step = 0;
    Game held = duel(continuous, "rocket-launcher", 1900);
    held.bodies[0].weapons[static_cast<std::size_t>(held.bodies[0].weapon)].ammo = 2;
    for (int k = 0; k < 600 && held.shots.empty(); ++k) hold(world, continuous, held, 1);
    CHECK(held.shots.size() == 2);
    CHECK(held.bodies[0].weapons[static_cast<std::size_t>(held.bodies[0].weapon)].ammo == 0);
}

/// The ticks, counted from 1, at which the shooter's ammo fell while holding
/// fire (or alt) for `ticks`.
std::vector<int> spendTicks(const BodyWorld& world, Game& game, int ticks, bool alt) {
    const auto ammo = [&game] { return game.bodies[0].weapons[static_cast<std::size_t>(game.bodies[0].weapon)].ammo; };
    std::vector<int> spends;
    for (int k = 1; k <= ticks; ++k) {
        const std::uint32_t before = ammo();
        hold(world, stock(), game, 1, alt);
        if (ammo() < before) spends.push_back(k);
    }
    return spends;
}

TEST_CASE("SS 4.9: the launcher spends a round a loading step and fires itself at six", "[ugame][game]") {
    const BodyWorld world = arena();
    Game game = duel(stock(), "rocket-launcher", 1900);
    const Charge& charge = *primaryOf(stock(), "rocket-launcher").charge;
    REQUIRE(charge.step == 0.9);
    REQUIRE(charge.stepLast == 0.833);
    // The press loads the first, and its tick counts. The rest load once
    // the hold passes 0.9, 1.8, 2.7, 3.6 and 4.433 s: a step's overshoot
    // counts toward the next (209, not 210), and the sixth takes 0.833
    // (tick 343; 0.9 would be 348).
    CHECK(spendTicks(world, game, 342, false) == std::vector{1, 70, 140, 209, 279});
    CHECK(game.shots.empty());
    CHECK(spendTicks(world, game, 1, false) == std::vector{1});
    CHECK(game.shots.size() == 6);
}

TEST_CASE("SS 4.9: the bio rifle's charge steps from nothing each time, the press's tick counted", "[ugame][game]") {
    const BodyWorld world = arena();
    Game game = duel(stock(), "bio-rifle", 1900);
    REQUIRE(stock().weapons[static_cast<std::size_t>(weaponNamed(stock(), "bio-rifle"))].alt->charge->step == 0.5);
    // 0.5 s is 38.6 ticks: 39 with the press's own, then 39 more each time.
    // A hold carried over a step would make the fourth 116.
    CHECK(spendTicks(world, game, 120, true) == std::vector{1, 39, 78, 117});
}

TEST_CASE("SS 4.9: a charged hammer holds at full until released", "[ugame][game]") {
    const BodyWorld world = arena();
    Game game = duel(stock(), "impact-hammer", 1900);
    std::vector<MoveCommand> commands(2);
    commands[0].fire = true;
    commands[0].yaw = 32768; // level, away from the dummy and the wall: nothing in reach
    commands[1].yaw = 32768;
    const double full = primaryOf(stock(), "impact-hammer").charge->time;
    int swings = 0;
    for (int k = 0; k < static_cast<int>(2 * full / DT); ++k) {
        tick(world, stock(), RULES, game, commands, DT);
        swings += static_cast<int>(std::ranges::count(game.events, Event::Kind::Melee, &Event::kind));
    }
    CHECK(swings == 0);
    commands[0].fire = false;
    tick(world, stock(), RULES, game, commands, DT);
    CHECK(std::ranges::count(game.events, Event::Kind::Melee, &Event::kind) == 1);
}

TEST_CASE("SS 4.9: the hammer hurts no body past its reach of the firer", "[ugame][game]") {
    const BodyWorld world = arena();
    const FireMode& mode = primaryOf(stock(), "impact-hammer");
    REQUIRE(mode.reach < mode.range);
    // Each dummy's near side is inside the trace's range; one is within
    // reach of the firer's centre, the other past it.
    for (const auto& [distance, hurt] : {std::pair{mode.reach - 10 + 17, true}, std::pair{mode.reach + 10 + 17, false}}) {
        CAPTURE(distance);
        Game game = duel(stock(), "impact-hammer", distance);
        hold(world, stock(), game, 10);
        std::vector<MoveCommand> release(2);
        release[1].yaw = 32768;
        tick(world, stock(), RULES, game, release, DT);
        CHECK((game.bodies[1].health < 1e9) == hurt);
    }
}

TEST_CASE("SS 4.9: the hammer on a surface costs its firer the same whatever the charge", "[ugame][game]") {
    const BodyWorld world = arena();
    std::vector<double> lost;
    for (const int held : {2, 120}) {
        Game game = duel(stock(), "impact-hammer", 1900);
        game.bodies[0].health = START_HEALTH;
        std::vector<MoveCommand> commands(2);
        commands[0].fire = true;
        commands[0].yaw = 32768; // away from everything: no touch while held
        commands[1].yaw = 32768;
        for (int k = 0; k < held; ++k) tick(world, stock(), RULES, game, commands, DT);
        commands[0].fire = false;
        commands[0].pitch = -16384; // the floor
        tick(world, stock(), RULES, game, commands, DT);
        lost.push_back(START_HEALTH - game.bodies[0].health);
    }
    CHECK(lost[0] == landed(stock(), primaryOf(stock(), "impact-hammer").selfDamage));
    CHECK(lost[1] == lost[0]);
}

TEST_CASE("SS 4.9: three rockets leave around the launcher's circle and fan by its fixed steps",
          "[ugame][game]") {
    const BodyWorld world = arena();
    Game game = duel(stock(), "rocket-launcher", 1900);
    const FireMode& mode = primaryOf(stock(), "rocket-launcher");
    const auto ammo = [&game] { return game.bodies[0].weapons[static_cast<std::size_t>(game.bodies[0].weapon)].ammo; };
    const std::uint32_t full = ammo();
    for (int k = 0; k < 400 && ammo() > full - 3; ++k) hold(world, stock(), game, 1);
    std::vector<MoveCommand> release(2);
    release[1].yaw = 32768;
    tick(world, stock(), RULES, game, release, DT);
    REQUIRE(game.shots.size() == 3);

    // UT_Eightball.FireRockets: rocket i leaves at angle 1.0484 i around a
    // circle of 4, shifted 7.5 right and 7 down, up to 4 back; the first
    // along the aim, the next turned left by 629 and 1258 (600 a radian,
    // truncated), each after the first x 0.9 to 1.1 in speed.
    Vec3 start = levelStart(mode);
    start.z += game.bodies[0].move.location.z - STANDING; // where the body settled
    const std::array<double, 3> sines{0.0, 0.8666260241298891, 0.8648204071720624};
    const std::array<double, 3> cosines{1.0, 0.4989582490557911, -0.5020813313983583};
    const std::array<std::int32_t, 3> yaws{0, -629, -1258};
    const double speed = stock().projectile("rocket")->speed;
    for (std::size_t i = 0; i < 3; ++i) {
        CAPTURE(i);
        const Shot shot = launchedThisTick(stock(), game.shots[i]);
        CHECK(std::abs(shot.location.y - (start.y - (sines[i] * 4 - 7.5))) < 1e-9);
        CHECK(std::abs(shot.location.z - (start.z + (cosines[i] * 4 - 7))) < 1e-9);
        CHECK(shot.location.x <= start.x + 1e-9);
        CHECK(shot.location.x >= start.x - 4 - 1e-9);
        const double v = uta::uworld::length(shot.velocity);
        const Vec3 along = shot.velocity * (1 / v);
        const Vec3 want = aimOf(yaws[i], 0);
        CHECK(uta::uworld::length(along - want) < 1e-9);
        if (i == 0) CHECK(std::abs(v - speed) < 1e-9);
        CHECK(v >= 0.9 * speed - 1e-9);
        CHECK(v <= 1.1 * speed + 1e-9);
    }
}

TEST_CASE("SS 4.9: eight flak chunks each leave from their own place beside the start", "[ugame][game]") {
    const BodyWorld world = arena();
    Game game = duel(stock(), "flak-cannon", 1900);
    const FireMode& mode = primaryOf(stock(), "flak-cannon");
    REQUIRE(mode.rule == uta::ugame::FireRule::FlakSpread);
    hold(world, stock(), game, 1);
    REQUIRE(game.shots.size() == 8);

    // UT_FlakCannon.Fire: Start, -Z, +2Y+Z, -Y, +2Y-Z, Start, +Y-Z, +2Y+Z.
    Vec3 start = levelStart(mode);
    start.z += game.bodies[0].move.location.z - STANDING; // where the body settled
    const std::array<std::array<double, 2>, 8> places{{{0, 0}, {0, -1}, {2, 1}, {-1, 0}, {2, -1}, {0, 0}, {1, -1}, {2, 1}}};
    for (std::size_t i = 0; i < places.size(); ++i) {
        CAPTURE(i);
        const Shot shot = launchedThisTick(stock(), game.shots[i]);
        CHECK(std::abs(shot.location.x - start.x) < 1e-9);
        CHECK(std::abs(shot.location.y - (start.y + places[i][0])) < 1e-9);
        CHECK(std::abs(shot.location.z - (start.z + places[i][1])) < 1e-9);
    }
}

TEST_CASE("INV-4: one mode's damage, refire or speed changes only that", "[ugame][game]") {
    const BodyWorld world = arena();

    SECTION("damage") {
        WeaponSet edited = stock();
        edited.weapons[static_cast<std::size_t>(weaponNamed(edited, "shock-rifle"))].primary.damage += 7;
        Game a = duel(stock(), "shock-rifle", 600), b = duel(edited, "shock-rifle", 600);
        hold(world, stock(), a, 1);
        hold(world, edited, b, 1);
        const double damage = primaryOf(stock(), "shock-rifle").damage;
        CHECK(1e9 - a.bodies[1].health == landed(stock(), damage));
        CHECK(1e9 - b.bodies[1].health == landed(stock(), damage + 7));
        CHECK(a.bodies[1].move.velocity == b.bodies[1].move.velocity);
        // The alt mode, untouched, is untouched.
        Game c = duel(stock(), "shock-rifle", 600), d = duel(edited, "shock-rifle", 600);
        hold(world, stock(), c, 60, true);
        hold(world, edited, d, 60, true);
        CHECK(stateHash(c) == stateHash(d));
    }
    SECTION("refire") {
        WeaponSet edited = stock();
        // The Enforcer: its cadence is its refire alone.
        edited.weapons[static_cast<std::size_t>(weaponNamed(edited, "enforcer"))].primary.refire *= 2;
        Game a = duel(stock(), "enforcer", 600), b = duel(edited, "enforcer", 600);
        const int shotsA = eventsWhileHeld(world, stock(), a, 170, Event::Kind::InstantHit);
        const int shotsB = eventsWhileHeld(world, edited, b, 170, Event::Kind::InstantHit);
        CAPTURE(shotsA, shotsB);
        CHECK(shotsB < shotsA);
        CHECK(shotsB >= shotsA / 2 - 1);
        CHECK(shotsB <= shotsA / 2 + 1);
    }
    SECTION("projectile speed") {
        WeaponSet edited = stock();
        edited.projectiles[0].speed *= 1.5; // bio-gel
        Game a = duel(stock(), "bio-rifle", 1900), b = duel(edited, "bio-rifle", 1900);
        hold(world, stock(), a, 2);
        hold(world, edited, b, 2);
        REQUIRE(a.shots.size() == 1);
        REQUIRE(b.shots.size() == 1);
        CHECK(b.shots[0].velocity.x == a.shots[0].velocity.x * 1.5);
        CHECK(a.bodies[0].weapons == b.bodies[0].weapons);
    }
}

TEST_CASE("S2: the hammer at the floor throws its firer up", "[ugame][game]") {
    const BodyWorld world = arena();
    Game game = duel(stock(), "impact-hammer", 1900);
    std::vector<MoveCommand> commands(2);
    commands[0].fire = true;
    commands[0].pitch = -16384; // straight down
    // The first tick starts the charge; the release goes off. The floor in
    // reach never fires it: only a body does (ImpactHammer.Firing).
    tick(world, stock(), RULES, game, commands, DT);
    commands[0].fire = false;
    tick(world, stock(), RULES, game, commands, DT);
    CHECK(game.bodies[0].move.physics == uta::uworld::Physics::Falling);
    CHECK(game.bodies[0].move.velocity.z > 0);
}

TEST_CASE("S2: a rocket at the firer's feet hurts and throws it", "[ugame][game]") {
    const BodyWorld world = arena();
    Game game = duel(stock(), "rocket-launcher", 1900);
    std::vector<MoveCommand> commands(2);
    commands[0].fire = true;
    commands[0].pitch = -16384;
    tick(world, stock(), RULES, game, commands, DT); // the charge starts
    commands[0].fire = false;
    for (int k = 0; k < 10 && game.bodies[0].health == START_HEALTH; ++k)
        tick(world, stock(), RULES, game, commands, DT);
    CHECK(game.bodies[0].health < START_HEALTH);
    CHECK(game.bodies[0].move.velocity.z > 0);
}

/// The centre of the first explosion `weapon`'s alt fire makes at a dummy
/// 600 ahead, on its line, whose hits count `inset` of its radius in.
Vec3 altBlastAt(const BodyWorld& world, const WeaponSet& set, std::string_view weapon,
                double inset = PAWN_HIT_INSET) {
    Game game = duel(set, weapon, 600);
    game.bodies[1].hitInset = inset;
    const FireMode& alt = *set.weapons[static_cast<std::size_t>(weaponNamed(set, weapon))].alt;
    game.bodies[1].move.location.y = levelStart(alt).y;
    std::vector<MoveCommand> commands(2);
    commands[0].altFire = true;
    commands[1].yaw = 32768;
    for (int k = 0; k < 120; ++k) {
        tick(world, set, RULES, game, commands, DT);
        commands[0].altFire = false;
        for (const Event& event : game.events)
            if (event.kind == Event::Kind::Explosion) return event.from;
    }
    FAIL("no explosion");
    return {};
}

TEST_CASE("SS 4.9: a shock ball striking a body bursts where its move ends, not where it met it", "[ugame][game]") {
    const BodyWorld world = arena();
    const Projectile& ball = *stock().projectile("shock-ball");
    REQUIRE(ball.splashFromMoved);
    const double contact = 600 - uta::uworld::BodySize{}.radius - ball.radius;
    const double step = ball.speed * DT;
    const double start = levelStart(*stock().weapons[static_cast<std::size_t>(weaponNamed(stock(), "shock-rifle"))].alt).x;

    const Vec3 moved = altBlastAt(world, stock(), "shock-rifle");
    CHECK(moved.x > contact);
    CHECK(moved.x <= contact + step);
    const double steps = (moved.x - start) / step;
    CHECK(std::abs(steps - std::round(steps)) < 1e-6);

    WeaponSet met = stock();
    for (Projectile& p : met.projectiles)
        if (p.name == ball.name) p.splashFromMoved = false;
    // Where it met it is moved 0.4 of the body's radius on (Pawn.AdjustHitLocation),
    // less 0.001 of its last move, which the trace meets the body early by.
    const double inset = 0.4 * uta::uworld::BodySize{}.radius;
    CHECK(std::abs(altBlastAt(world, met, "shock-rifle").x - contact - inset + 0.001 * step) < 1e-6);
}

TEST_CASE("UTA-0498: a hit on a player counts further into it than a hit on a dummy", "[ugame][game]") {
    const BodyWorld world = arena();
    // PlayerPawn.AdjustHitLocation's 0.5 of the radius against Pawn's 0.4.
    REQUIRE(Body{}.hitInset == PAWN_HIT_INSET);
    WeaponSet met = stock();
    for (Projectile& p : met.projectiles)
        if (p.name == "shock-ball") p.splashFromMoved = false;
    const double radius = uta::uworld::BodySize{}.radius;
    const double pawn = altBlastAt(world, met, "shock-rifle").x;
    const double player = altBlastAt(world, met, "shock-rifle", PLAYER_HIT_INSET).x;
    CHECK(std::abs(player - pawn - (PLAYER_HIT_INSET - PAWN_HIT_INSET) * radius) < 1e-6);
    CHECK(PAWN_HIT_INSET == 0.4);
    CHECK(PLAYER_HIT_INSET == 0.5);
}

TEST_CASE("SS 4.9: the ripper's alt blade hurts the body it strikes alone; its blast is for the level", "[ugame][game]") {
    const BodyWorld world = arena();
    REQUIRE(stock().projectile("razor-alt")->bodyDirect);
    const auto losses = [&world](const WeaponSet& set) {
        Game game = duel(set, "ripper", 600);
        const FireMode& alt = *set.weapons[static_cast<std::size_t>(weaponNamed(set, "ripper"))].alt;
        game.bodies[1].move.location.y = levelStart(alt).y;
        // A second dummy beside the first, well inside the blast's 180.
        game.bodies.push_back(game.bodies[1]);
        game.bodies[2].move.location.y += 60;
        std::vector<MoveCommand> commands(3);
        commands[0].altFire = true;
        for (int k = 0; k < 60; ++k) {
            tick(world, set, RULES, game, commands, DT);
            commands[0].altFire = false;
        }
        return std::pair{1e9 - game.bodies[1].health, 1e9 - game.bodies[2].health};
    };
    // Struck straight on: the whole damage, as Razor2Alt.ProcessTouch's TakeDamage.
    CHECK(losses(stock()) == std::pair{landed(stock(), stock().projectile("razor-alt")->damage), 0.0});
    WeaponSet splashing = stock();
    for (Projectile& p : splashing.projectiles)
        if (p.name == "razor-alt") p.bodyDirect = false;
    CHECK(losses(splashing).second > 0);
}

TEST_CASE("SS 4.9: a blast's push rises at most its splash-up-max, unnormalised", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    Game game;
    game.bodies.push_back(bodyAt(set, {60, 0, 80}, 0));
    detail::Tick t{world, set, RULES, game, DT, {}, {}, {}};
    detail::splash(t, {0, 0, 0}, 100, 1000, 500, -1, 0.45);
    REQUIRE(t.owed.size() == 1);
    const Vec3 push = t.owed[0].momentum;
    CHECK(push.y == 0);
    CHECK(std::abs(push.z / push.x - 0.45 / 0.6) < 1e-12);
    // Uncapped, the direction is the body's: 0.8 up for 0.6 along.
    t.owed.clear();
    detail::splash(t, {0, 0, 0}, 100, 1000, 500, -1);
    CHECK(std::abs(t.owed[0].momentum.z / t.owed[0].momentum.x - 0.8 / 0.6) < 1e-12);
}

TEST_CASE("SS 4.9: a charged glob's blast widens by its charge, up to splash-radius-max", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    const Projectile& glob = *set.projectile("bio-glob");
    REQUIRE(glob.splashRadiusMax == 250);
    const auto kind = static_cast<std::uint32_t>(&glob - set.projectiles.data());
    // A glob of `scale` flown into the dummy, a bystander 150 beside it:
    // the blast's radius, and what the bystander lost.
    const auto blast = [&](const WeaponSet& with, double scale) {
        Game game = duel(with, "bio-rifle", 600);
        game.bodies.push_back(game.bodies[1]);
        game.bodies[2].move.location.y += 150;
        game.shots.push_back(Shot{.kind = kind, .owner = 0, .location = {540, 0, STANDING}, .velocity = {700, 0, 0},
                                  .lifetime = glob.lifetime, .damageScale = scale});
        double radius = 0;
        const std::vector<MoveCommand> commands(3);
        for (int k = 0; k < 10; ++k) {
            tick(world, with, RULES, game, commands, DT);
            for (const Event& event : game.events)
                if (event.kind == Event::Kind::Explosion) radius = event.radius;
        }
        return std::pair{radius, 1e9 - game.bodies[2].health};
    };
    CHECK(blast(set, 1) == std::pair{75.0, 0.0});
    CHECK(blast(set, 3).first == 225);
    CHECK(blast(set, 3).second > 0);
    CHECK(blast(set, 4.28).first == 250);
    // Without the key a charge leaves the radius alone.
    WeaponSet fixed = set;
    fixed.projectiles[kind].splashRadiusMax = 0;
    CHECK(blast(fixed, 3) == std::pair{75.0, 0.0});
}

TEST_CASE("INV-6: a body moved onto a shot is met along the shot's last move", "[ugame][game]") {
    // Projectile.Touch: the trace from OldLocation to Location, into the
    // body where it now stands, met 0.001 of the trace early and moved in
    // 0.4 of its radius; a last move that starts inside it meets the body's
    // side, towards the shot.
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    const Projectile& rocket = *set.projectile("rocket");
    REQUIRE(!rocket.splashFromMoved);
    const auto kind = static_cast<std::uint32_t>(&rocket - set.projectiles.data());
    const auto blastAt = [&](double previous) {
        Game game = duel(set, "rocket-launcher", 600);
        const double radius = game.bodies[1].size.radius;
        game.shots.push_back(Shot{.kind = kind, .owner = 0, .location = {600 - radius + 7, 0, STANDING},
                                  .velocity = {900, 0, 0}, .previous = {previous, 0, STANDING},
                                  .lifetime = rocket.lifetime, .age = DT});
        tick(world, set, RULES, game, std::vector<MoveCommand>(2), DT);
        for (const Event& event : game.events)
            if (event.kind == Event::Kind::Explosion) return event.from.x - (600 - radius);
        return -1e9;
    };
    const double radius = duel(set, "rocket-launcher", 600).bodies[1].size.radius;
    CHECK(std::abs(blastAt(560) - 0.4 * radius + 0.001 * (600 - radius + 7 - 560)) < 1e-9);
    CHECK(std::abs(blastAt(600 - radius + 3)) < 1e-9);
}

TEST_CASE("INV-6: a shot launched inside a body meets it only heading in", "[ugame][game]") {
    // A flak slug's pieces start inside the body it struck: one heading in,
    // towards the body's axis, meets it where it starts, moved in 0.4 of its
    // radius less 0.001 of its move; one heading out flies on.
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    const Projectile& rocket = *set.projectile("rocket");
    REQUIRE(!rocket.splashFromMoved);
    const auto kind = static_cast<std::uint32_t>(&rocket - set.projectiles.data());
    const double radius = duel(set, "rocket-launcher", 600).bodies[1].size.radius;
    const Vec3 start{600 - radius + 7, 0, STANDING};
    const auto blastAt = [&](double speed) {
        Game game = duel(set, "rocket-launcher", 600);
        game.shots.push_back(Shot{.kind = kind, .owner = 0, .location = start, .velocity = {speed, 0, 0},
                                  .previous = start, .lifetime = rocket.lifetime});
        tick(world, set, RULES, game, std::vector<MoveCommand>(2), DT);
        for (const Event& event : game.events)
            if (event.kind == Event::Kind::Explosion) return event.from.x - start.x;
        return -1e9;
    };
    CHECK(std::abs(blastAt(900) - 0.4 * radius + 0.001 * 900 * DT) < 1e-9);
    CHECK(blastAt(-900) == -1e9);
}

TEST_CASE("SS 4.9: damage lands whole, scaled by hardcore's damage-scale", "[ugame][game]") {
    const BodyWorld world = arena();
    REQUIRE(stock().damageScale == 1.5);
    WeaponSet edited = stock();
    edited.weapons[static_cast<std::size_t>(weaponNamed(edited, "shock-rifle"))].primary.damage = 45.7;
    Game game = duel(edited, "shock-rifle", 600);
    game.bodies[1].move.location.y = levelStart(primaryOf(edited, "shock-rifle")).y;
    hold(world, edited, game, 1);
    // 45.7 is 45 whole; hardcore's 67.5 is 67 whole. The original's own run: 45 took 67.
    CHECK(1e9 - game.bodies[1].health == 67);
}

TEST_CASE("SS 4.9: a push's lift is halved on a body already rising past 380", "[ugame][game]") {
    uta::uworld::MoveState rising;
    rising.physics = uta::uworld::Physics::Falling;
    rising.velocity = {0, 0, 381};
    CHECK(uta::uworld::push(rising, {10, 0, 100}).velocity == Vec3{10, 0, 431});
    rising.velocity.z = 380;
    CHECK(uta::uworld::push(rising, {10, 0, 100}).velocity == Vec3{10, 0, 480});
    rising.velocity.z = 381; // a downward push is never halved
    CHECK(uta::uworld::push(rising, {0, 0, -100}).velocity == Vec3{0, 0, 281});
}

TEST_CASE("SS 4.9: a shot starts at the eye plus its mode's offset", "[ugame][game]") {
    const BodyWorld world = arena();
    // A rocket fired level; the original's run put it 16 above the body's centre.
    Game game = duel(stock(), "rocket-launcher", 1900);
    game.bodies[1].move.location.y = 500;
    std::vector<MoveCommand> commands(2);
    commands[0].fire = true;
    tick(world, stock(), RULES, game, commands, DT); // the charge starts
    commands[0].fire = false;
    tick(world, stock(), RULES, game, commands, DT); // released: one rocket
    REQUIRE(game.shots.size() == 1);
    // From where the body stands now: it settles a hair onto the floor.
    const std::array<double, 3>& offset = primaryOf(stock(), "rocket-launcher").offset;
    const Vec3 body = game.bodies[0].move.location;
    const Vec3 at = launchedThisTick(stock(), game.shots[0]).location;
    CHECK(uta::uworld::length(at - (body + Vec3{offset[0], offset[1], EYE_HEIGHT + offset[2]})) < 1e-9);
    CHECK(std::abs(at.z - body.z - 16) < 1e-9);
}

namespace {

/// The kind named `name`, as an index into the set's projectiles.
std::uint32_t kindOf(const WeaponSet& set, std::string_view name) {
    return static_cast<std::uint32_t>(std::ranges::find(set.projectiles, name, &Projectile::name) -
                                      set.projectiles.begin());
}

TEST_CASE("SS 4.9: a wind-up delays a press's shot, which a release does not stop", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    const FireMode& alt = *set.weapons[static_cast<std::size_t>(weaponNamed(set, "enforcer"))].alt;
    REQUIRE(alt.windUp > 3 * DT);
    REQUIRE(alt.pressAmmo == 0);
    Game game = duel(set, "enforcer", 600);
    const std::uint32_t full = game.bodies[0].weapons[static_cast<std::size_t>(game.bodies[0].weapon)].ammo;
    std::vector<MoveCommand> commands(2);
    commands[0].altFire = true;
    std::vector<int> shots; // the ticks a shot left in
    for (int k = 0; k < 60; ++k) {
        tick(world, set, RULES, game, commands, DT);
        commands[0].altFire = false; // a tap
        if (std::ranges::count(game.events, Event::Kind::InstantHit, &Event::kind) > 0) shots.push_back(k);
    }
    // The press's own tick counts toward the wind-up.
    REQUIRE(shots.size() == 1);
    CHECK(std::abs(shots[0] + 1 - alt.windUp / DT) <= 1);
    CHECK(game.bodies[0].weapons[static_cast<std::size_t>(game.bodies[0].weapon)].ammo == full - alt.ammo);
}

/// A hold of the Minigun's trigger: the ticks it fired in, and the ticks it
/// spent a round in and fired nothing.
struct Held {
    std::vector<int> shots, wasted;
};

/// `ticks` ticks, the trigger down for the first `down` of them.
Held holdMinigun(const BodyWorld& world, const WeaponSet& set, bool alt, int ticks, int down = 1 << 30) {
    Game game = duel(set, "minigun", 600);
    const std::size_t gun = static_cast<std::size_t>(weaponNamed(set, "minigun"));
    std::vector<MoveCommand> commands(2);
    Held held;
    for (int k = 0; k < ticks; ++k) {
        (alt ? commands[0].altFire : commands[0].fire) = k < down;
        const std::uint32_t ammo = game.bodies[0].weapons[gun].ammo;
        tick(world, set, RULES, game, commands, DT);
        const bool shot = std::ranges::count(game.events, Event::Kind::InstantHit, &Event::kind) > 0;
        if (shot) held.shots.push_back(k);
        else if (game.bodies[0].weapons[gun].ammo < ammo) held.wasted.push_back(k);
    }
    return held;
}

TEST_CASE("SS 4.9: a hold presses again every repress, paying the press's ammo and winding up again",
          "[ugame][game]") {
    const BodyWorld world = arena();
    // Times that fall inside ticks, so a moment and its tick's end differ.
    WeaponSet set = stock();
    FireMode& mode = set.weapons[static_cast<std::size_t>(weaponNamed(set, "minigun"))].primary;
    mode.repress = 20.9 * DT;
    mode.windUp = 5.5 * DT;
    mode.refire = 5 * DT;
    REQUIRE(mode.pressAmmo > 0);
    const Held held = holdMinigun(world, set, false, static_cast<int>(3.5 * mode.repress / DT));
    // The press, then one every repress, counted from the press.
    REQUIRE(held.wasted.size() == 4);
    // The tick a time falls in: the first to end at or after it.
    const auto tickOf = [](double time) { return static_cast<int>(std::ceil(time / DT - 1e-9)) - 1; };
    for (std::size_t k = 1; k < held.wasted.size(); ++k)
        CHECK(held.wasted[k] == tickOf(static_cast<double>(k) * mode.repress));
    // After each, the next shot is a wind-up after that moment, not after
    // its tick's end, and not a refire after the last shot.
    for (std::size_t k = 1; k < held.wasted.size(); ++k) {
        const auto next = std::ranges::upper_bound(held.shots, held.wasted[k]);
        REQUIRE(next != held.shots.end());
        CHECK(*next == tickOf(static_cast<double>(k) * mode.repress + mode.windUp));
    }
}

TEST_CASE("SS 4.9: a released hold presses no more", "[ugame][game]") {
    const BodyWorld world = arena();
    const FireMode& mode = primaryOf(stock(), "minigun");
    const int down = static_cast<int>(0.5 * mode.repress / DT);
    const Held held = holdMinigun(world, stock(), false, static_cast<int>(3 * mode.repress / DT), down);
    CHECK(held.wasted == std::vector<int>{0});
    REQUIRE(!held.shots.empty());
    CHECK(held.shots.back() <= down);
}

TEST_CASE("SS 4.9: a hold fires every spin-refire until it has been held for spin-up, and every refire after",
          "[ugame][game]") {
    const BodyWorld world = arena();
    const FireMode& mode = *stock().weapons[static_cast<std::size_t>(weaponNamed(stock(), "minigun"))].alt;
    REQUIRE(mode.spinUp > 2 * mode.spinRefire);
    REQUIRE(mode.spinRefire > mode.refire + 2 * DT);
    const Held held = holdMinigun(world, stock(), true, static_cast<int>(3 * mode.spinUp / DT));
    REQUIRE(held.shots.size() > 8);
    int slow = 0, fast = 0;
    for (std::size_t k = 0; k + 1 < held.shots.size(); ++k) {
        // The shot that finds the spin-up over is the first the faster time follows.
        const bool spun = (held.shots[k] + 1) * DT >= mode.spinUp;
        const double gap = (held.shots[k + 1] - held.shots[k]) * DT;
        CAPTURE(k, gap);
        CHECK(std::abs(gap - (spun ? mode.refire : mode.spinRefire)) <= DT + 1e-9);
        ++(spun ? fast : slow);
    }
    CHECK(slow >= 2);
    CHECK(fast >= 5);
}

TEST_CASE("SS 4.9: a press's press-ammo is spent at once, with no shot", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    const FireMode& mode = primaryOf(set, "minigun");
    REQUIRE(mode.pressAmmo > 0);
    REQUIRE(mode.windUp > 3 * DT);
    Game game = duel(set, "minigun", 600);
    const Carried& carried = game.bodies[0].weapons[static_cast<std::size_t>(game.bodies[0].weapon)];
    const std::uint32_t full = carried.ammo;
    CHECK(eventsWhileHeld(world, set, game, 1, Event::Kind::InstantHit) == 0);
    CHECK(carried.ammo == full - mode.pressAmmo);

    // INV-7: a press it cannot pay for changes nothing.
    Game empty = duel(set, "minigun", 600);
    empty.bodies[0].weapons[static_cast<std::size_t>(empty.bodies[0].weapon)].ammo = 0;
    const std::uint64_t before = stateHash(empty);
    Game idle = empty;
    std::vector<MoveCommand> none(2);
    none[1].yaw = 32768;
    hold(world, set, empty, 1);
    tick(world, set, RULES, idle, none, DT);
    CHECK(stateHash(empty) == stateHash(idle));
    CHECK(before != stateHash(idle)); // the tick itself is counted
}

TEST_CASE("SS 4.9: a mode's refire-lead delays only the shot after one fired from idle", "[ugame][game]") {
    const BodyWorld world = arena();
    WeaponSet set = stock();
    const int ripper = weaponNamed(set, "ripper");
    FireMode& mode = set.weapons[static_cast<std::size_t>(ripper)].primary;
    mode.refireLead = 0.05; // several ticks, to be seen past a tick's slack
    Game game = duel(set, "ripper", 1500);
    game.bodies[1].move.location.y = 1500;
    std::vector<int> fired; // the ticks a round was spent in
    std::uint32_t ammo = game.bodies[0].weapons[static_cast<std::size_t>(ripper)].ammo;
    for (int k = 0; fired.size() < 4; ++k) {
        hold(world, set, game, 1);
        const std::uint32_t now = game.bodies[0].weapons[static_cast<std::size_t>(ripper)].ammo;
        if (now < ammo) fired.push_back(k);
        ammo = now;
    }
    // Whole ticks: each interval is its time, within a tick.
    const int later = fired[2] - fired[1];
    CHECK(fired[0] == 0);
    CHECK(std::abs(later - mode.refire / DT) <= 1);
    CHECK(std::abs(fired[3] - fired[2] - later) <= 1);
    CHECK(std::abs(fired[1] - later - mode.refireLead / DT) <= 1);
}

TEST_CASE("SS 4.9: the pulse gun's shots start round a circle, a turn further each shot of a hold", "[ugame][game]") {
    const WeaponSet& set = stock();
    const FireMode& mode = primaryOf(set, "pulse-gun");
    REQUIRE(mode.spiralRadius == 4);
    REQUIRE(mode.spiralTurn == 18775);
    Body body = bodyAt(set, {0, 0, STANDING}, 0);
    const Vec3 centre = body.move.location + Vec3{mode.offset[0], mode.offset[1], EYE_HEIGHT + mode.offset[2]};
    // Facing +X the right-hand axis is +Y: the first is straight above the
    // offset, the second 1.8 radians round toward the left.
    CHECK(uta::uworld::length(detail::startOf(body, mode) - (centre + Vec3{0, 0, 4})) < 1e-9);
    body.burst = 1;
    const Vec3 second = centre + Vec3{0, -4 * std::sin(1.8), 4 * std::cos(1.8)};
    CHECK(uta::uworld::length(detail::startOf(body, mode) - second) < 1e-3);
    // The start follows the eye.
    body.eyeHeight = EYE_HEIGHT - 3;
    CHECK(uta::uworld::length(detail::startOf(body, mode) - (second - Vec3{0, 0, 3})) < 1e-3);

    // A hold counts its shots; letting go starts the circle again.
    const BodyWorld world = arena();
    Game game = duel(set, "pulse-gun", 1900);
    hold(world, set, game, 40);
    CHECK(game.bodies[0].burst == 3);
    const std::vector<MoveCommand> none(2);
    tick(world, set, RULES, game, none, DT);
    CHECK(game.bodies[0].burst == 0);
}

TEST_CASE("SS 4.9: a shot launched past its most speed is held to it from its first move", "[ugame][game]") {
    // Razor2 leaves at its speed, 1300, and the engine's projectile physics
    // caps it to MaxSpeed, 1200, whether or not it accelerates.
    const BodyWorld world = arena();
    const Projectile& razor = *stock().projectile("razor");
    REQUIRE(razor.acceleration == 0);
    REQUIRE(razor.maxSpeed > 0);
    REQUIRE(razor.maxSpeed < razor.speed);
    Game game = duel(stock(), "ripper", 1500);
    hold(world, stock(), game, 1);
    REQUIRE(game.shots.size() == 1);
    CHECK(std::abs(uta::uworld::length(game.shots[0].velocity) - razor.maxSpeed) < 1e-9);
    CHECK(std::abs(game.shots[0].location.x - levelStart(primaryOf(stock(), "ripper")).x - razor.maxSpeed * DT) < 1e-9);
}

TEST_CASE("SS 4.9: a shot that does not bounce gains its gravity-after once its move is made", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    const Projectile& shell = *set.projectile("flak-shell");
    REQUIRE(shell.gravity > 0);
    REQUIRE(shell.gravityAfter > 0);
    Game game = duel(set, "flak-cannon", 1500);
    game.bodies[1].move.location.y = 1500;
    std::vector<MoveCommand> commands(2);
    commands[0].altFire = true;
    tick(world, set, RULES, game, commands, DT);
    REQUIRE(game.shots.size() == 1);
    const Shot first = game.shots[0];
    commands[0].altFire = false;
    tick(world, set, RULES, game, commands, DT);
    const Shot& second = game.shots[0];
    // The move is made at the speed the first part alone gives.
    const double moving = first.velocity.z + RULES.gravityZ * shell.gravity * DT;
    CHECK(std::abs(second.location.z - first.location.z - moving * DT) < 1e-9);
    CHECK(std::abs(second.velocity.z - moving - RULES.gravityZ * shell.gravityAfter * DT) < 1e-9);
}

/// A set with one kind's bounce changed, and a game holding one shot of it.
WeaponSet bouncing(std::string_view kind, double off, double offRandom) {
    WeaponSet set = stock();
    const auto at = std::ranges::find(set.projectiles, kind, &Projectile::name);
    REQUIRE(at != set.projectiles.end());
    at->bounceOff = off;
    at->bounceOffRandom = offRandom;
    return set;
}

Game oneShot(const WeaponSet& set, std::string_view kind, const Vec3& at, const Vec3& velocity) {
    Game game = duel(set, "flak-cannon", 1500);
    game.bodies[1].move.location.y = -2500; // out of the way
    const auto found = std::ranges::find(set.projectiles, kind, &Projectile::name);
    game.shots.push_back(Shot{.kind = static_cast<std::uint32_t>(found - set.projectiles.begin()),
                              .owner = 0, .location = at, .velocity = velocity, .lifetime = found->lifetime});
    return game;
}

TEST_CASE("SS 4.9: a falling shot turning over fast in a tick falls only after the top of its arc", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    const Projectile& gel = *set.projectile("bio-gel");
    REQUIRE(gel.gravity > 0);
    REQUIRE(gel.gravityAfter == 0);
    // A tick at 30 frames a second, as recorded; its gravity takes 17.4 off.
    const double dt = uta::uworld::DEFAULT_GAME_SPEED / 30;
    const double gained = RULES.gravityZ * gel.gravity * dt;
    const auto fallFrom = [&](double rising) {
        Game game = oneShot(set, "bio-gel", Vec3{0, 500, 300}, Vec3{100, 0, rising});
        tick(world, set, RULES, game, std::vector<MoveCommand>(2), dt);
        REQUIRE(game.shots.size() == 1);
        return game.shots[0].location.z - 300;
    };
    // Rising 8.7, falling 8.7 after: still until the top, half the tick in.
    const double falling = 8.7 + gained;
    CHECK(std::abs(fallFrom(8.7) - falling * (dt - dt * 8.7 / (8.7 - falling))) < 1e-9);
    // Either side under 7.05: the whole tick at the speed it ends with.
    CHECK(std::abs(fallFrom(6.9) - (6.9 + gained) * dt) < 1e-9);
    CHECK(std::abs(fallFrom(-gained - 6.9) - -6.9 * dt) < 1e-9);
}

TEST_CASE("SS 4.9: a falling shot moves a long tick in steps of 0.05 and the rest in two halves",
          "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    const Projectile& gel = *set.projectile("bio-gel");
    REQUIRE(gel.gravity > 0);
    // Recorded: a 0.1566 s tick at 30 frames a second.
    const double dt = 0.1566;
    const Vec3 velocity{100, 0, -2};
    Game game = oneShot(set, "bio-gel", Vec3{0, 500, 300}, velocity);
    tick(world, set, RULES, game, std::vector<MoveCommand>(2), dt);
    REQUIRE(game.shots.size() == 1);
    const double rest = (dt - 0.1) / 2;
    double z = 300, vz = velocity.z;
    for (const double step : {0.05, 0.05, rest, rest}) {
        vz += RULES.gravityZ * gel.gravity * step;
        z += vz * step;
    }
    CHECK(std::abs(game.shots[0].location.z - z) < 1e-9);
    CHECK(std::abs(game.shots[0].velocity.z - vz) < 1e-9);
    CHECK(std::abs(game.shots[0].location.x - velocity.x * dt) < 1e-9);
}

TEST_CASE("SS 4.9: a shot whose lifetime ends explodes expire-up above where it is", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    const Projectile& grenade = *set.projectile("grenade");
    REQUIRE(grenade.expireExplodes);
    REQUIRE(grenade.expireUp == 16);
    const Vec3 at{0, 500, 300};
    Game game = oneShot(set, "grenade", at, Vec3{});
    game.shots[0].lifetime = DT / 2; // it ends this tick, before it moves
    tick(world, set, RULES, game, std::vector<MoveCommand>(2), DT);
    CHECK(game.shots.empty());
    const auto blast = std::ranges::find(game.events, Event::Kind::Explosion, &Event::kind);
    REQUIRE(blast != game.events.end());
    CHECK(blast->from == Vec3{at.x, at.y, at.z + grenade.expireUp});
}

TEST_CASE("SS 4.9: a bio gel still in flight bursts 3.1 to 3.3 s after it leaves", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    Game game = duel(set, "bio-rifle", 1500);
    game.bodies[1].move.location.y = -2500; // out of the way
    // Tossed high, so it is still rising or falling when its fuse ends.
    std::vector<MoveCommand> commands(2);
    commands[0].fire = true;
    commands[0].pitch = 12000;
    int launched = -1, burst = -1;
    double height = 0;
    for (int k = 0; k < 600 && burst < 0; ++k) {
        tick(world, set, RULES, game, commands, DT);
        commands[0].fire = false;
        if (launched < 0 && !game.shots.empty()) launched = k;
        const auto blast = std::ranges::find(game.events, Event::Kind::Explosion, &Event::kind);
        if (blast != game.events.end()) burst = k, height = blast->from.z;
    }
    REQUIRE(launched == 0);
    REQUIRE(burst > 0);
    CHECK(height > 100); // in the air, not on the floor
    // It flies in the tick it is fired, so that tick counts toward its age.
    const double age = (burst - launched + 1) * DT;
    CAPTURE(age);
    CHECK(age >= 3.1);
    CHECK(age < 3.3 + DT);
}

TEST_CASE("SS 4.9: a bounce keeps bounce-keep of the speed along the surface and bounce-off of it off",
          "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet set = bouncing("flak-chunk", 0.25, 0);
    const double keep = set.projectile("flak-chunk")->bounceKeep;
    // Down at the floor, a tenth of a tick's fall above where it stops.
    const Vec3 velocity{600, 300, -400};
    const double speed = uta::uworld::length(velocity);
    Game game = oneShot(set, "flak-chunk", Vec3{0, 500, 0} - velocity * (0.1 * DT + 2.5 / speed), velocity);
    tick(world, set, RULES, game, std::vector<MoveCommand>(2), DT);
    REQUIRE(game.shots.size() == 1);
    const Shot& shot = game.shots[0];
    CHECK(shot.bounces == 1);
    CHECK(std::abs(shot.velocity.x - velocity.x * keep) < 1e-9);
    CHECK(std::abs(shot.velocity.y - velocity.y * keep) < 1e-9);
    // Off the floor a quarter of what it came in at, less the fall since.
    const double left = 0.9 * DT;
    const double fall = RULES.gravityZ * *set.projectile("flak-chunk")->bounceGravity * left;
    CHECK(std::abs(shot.velocity.z - (400 * 0.25 + fall)) < 1e-6);
    // It stopped 2.5 short of the floor along its path, then flew on.
    const double stopped = 2.5 * 400 / speed;
    CHECK(std::abs(shot.location.z - (stopped + shot.velocity.z * (left + DT))) < 1e-6);
}

TEST_CASE("SS 4.9: a shot strikes a surface up to 2 past its move's end, and no further", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet set = bouncing("flak-chunk", 0.25, 0);
    const Vec3 velocity{600, 300, -400};
    const double speed = uta::uworld::length(velocity);
    // Its move ends `gap` short of the floor, along its path.
    const auto bouncesFrom = [&](double gap) {
        Game game = oneShot(set, "flak-chunk", Vec3{0, 500, 0} - velocity * (DT + gap / speed), velocity);
        tick(world, set, RULES, game, std::vector<MoveCommand>(2), DT);
        REQUIRE(game.shots.size() == 1);
        return game.shots[0];
    };
    const Shot near = bouncesFrom(1.9);
    CHECK(near.bounces == 1);
    // It stopped 2.5 short of the floor, 0.6 before its move's end, and
    // flew the time that left it and a tick more at what the bounce kept.
    const double left = 0.6 / speed;
    const double fall = RULES.gravityZ * *set.projectile("flak-chunk")->bounceGravity * left;
    CHECK(std::abs(near.velocity.z - (400 * 0.25 + fall)) < 1e-6);
    CHECK(std::abs(near.location.z - (2.5 * 400 / speed + near.velocity.z * (left + DT))) < 1e-6);
    CHECK(bouncesFrom(2.1).bounces == 0);
}

TEST_CASE("SS 4.9: a kind that falls from its first bounce moves a tick more in that tick, and falls from then on",
          "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet set = bouncing("flak-chunk", 0.5, 0);
    const Projectile& chunk = *set.projectile("flak-chunk");
    REQUIRE(chunk.gravity == 0);
    REQUIRE(chunk.bounceGravity);
    const Vec3 velocity{1000, 0, -400};
    const double speed = uta::uworld::length(velocity);
    const Vec3 start = Vec3{0, 500, 0} - velocity * (0.25 * DT + 2.5 / speed);
    Game game = oneShot(set, "flak-chunk", start, velocity);
    const std::vector<MoveCommand> commands(2);
    tick(world, set, RULES, game, commands, DT);
    REQUIRE(game.shots.size() == 1);
    // A quarter of the tick to the floor, then the rest of it and a whole
    // tick more along the floor, at the speed the bounce left.
    const double along = velocity.x * chunk.bounceKeep;
    CHECK(std::abs(game.shots[0].location.x - (start.x + velocity.x * 0.25 * DT + along * 1.75 * DT)) < 1e-6);
    // It falls for the rest of the tick alone.
    const double rising = 400 * 0.5 + RULES.gravityZ * *chunk.bounceGravity * 0.75 * DT;
    CHECK(std::abs(game.shots[0].velocity.z - rising) < 1e-6);
    // From then on it falls each tick, and a later bounce adds no tick.
    tick(world, set, RULES, game, commands, DT);
    REQUIRE(game.shots.size() == 1);
    CHECK(std::abs(game.shots[0].velocity.z - (rising + RULES.gravityZ * *chunk.bounceGravity * DT)) < 1e-6);
    CHECK(std::abs(game.shots[0].location.x - (start.x + velocity.x * 0.25 * DT + along * 2.75 * DT)) < 1e-6);
}

TEST_CASE("SS 4.9: a bounce's draw keeps from bounce-off less bounce-off-random to bounce-off plus it",
          "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet set = bouncing("flak-chunk", 0.4, 0.2);
    double least = 1, most = 0;
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
        const Vec3 velocity{0, 0, -1000};
        // Straight down, to stop half way through the tick.
        Game game = oneShot(set, "flak-chunk", Vec3{0, 500, 2.5} - velocity * (0.5 * DT), velocity);
        game.random = seed;
        tick(world, set, RULES, game, std::vector<MoveCommand>(2), DT);
        REQUIRE(game.shots.size() == 1);
        REQUIRE(game.shots[0].bounces == 1);
        const double fall = RULES.gravityZ * *set.projectile("flak-chunk")->bounceGravity * 0.5 * DT;
        const double kept = (game.shots[0].velocity.z - fall) / 1000;
        least = std::min(least, kept);
        most = std::max(most, kept);
    }
    CHECK(least >= 0.2 - 1e-9);
    CHECK(most <= 0.6 + 1e-9);
    CHECK(least < 0.25);
    CHECK(most > 0.55);
}

TEST_CASE("SS 4.9: a flak shell's pieces start 10 off the surface and fly on along the shell's own aim",
          "[ugame][game]") {
    // flakslug.Explode spawns each chunk at Location + 10 * HitNormal with
    // the slug's Rotation, which is its launch aim; the chunk turns itself.
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    const Projectile& chunk = *set.projectile("flak-chunk");
    const std::uint32_t shell = kindOf(set, "flak-shell"), piece = kindOf(set, "flak-chunk");
    Game game = duel(set, "flak-cannon", 1500);
    game.bodies[1].move.location.y = -1500; // out of the shell's way
    std::vector<MoveCommand> commands(2);
    commands[0].altFire = true;
    commands[0].yaw = 8192; // an eighth of a turn: the aim is not an axis
    const Vec3 aim{std::numbers::sqrt2 / 2, std::numbers::sqrt2 / 2, 0};

    Vec3 shellAt{};
    for (int k = 0; k < 120 && std::ranges::count(game.shots, piece, &Shot::kind) == 0; ++k) {
        if (!game.shots.empty()) shellAt = game.shots[0].location;
        tick(world, set, RULES, game, commands, DT);
        commands[0].altFire = false;
    }
    // The shell fell to the floor well short of the wall: its normal is up.
    REQUIRE(shellAt.x < 1900);
    REQUIRE(std::ranges::count(game.shots, shell, &Shot::kind) == 0);
    REQUIRE(game.shots.size() == set.projectile("flak-shell")->splitCount);
    const double mostTurn = std::numbers::sqrt2 * chunk.turnRandom * 2 * std::numbers::pi / 65536;
    for (const Shot& shot : game.shots) {
        const Shot was = launchedThisTick(set, shot);
        CHECK(was.location.z > 10); // 10 off the floor, plus the strike's back-off:
        CHECK(was.location.z <= 10 + 2.5); // 2.5 along a path that is never straight down
        CHECK(std::abs(was.location.x - shellAt.x) < 60); // where the shell came down
        const double speed = uta::uworld::length(was.velocity);
        CHECK(std::abs(speed - chunk.speed) <= chunk.speedRandom);
        CHECK(std::acos(uta::uworld::dot(was.velocity, aim) / speed) <= mostTurn + 1e-6);
    }
    CHECK(game.shots[0].velocity != game.shots[1].velocity);
}

/// The stock set with the rocket launcher's stand-in spread taken out, so a
/// launch's direction is its aim.
WeaponSet unspreadRockets() {
    WeaponSet set = stock();
    Weapon& launcher = set.weapons[static_cast<std::size_t>(weaponNamed(set, "rocket-launcher"))];
    launcher.primary.spread = 0;
    launcher.alt->spread = 0;
    return set;
}

/// Fires a charging mode at once: one tick held, one released.
void tapCharge(const BodyWorld& world, const WeaponSet& set, Game& game, bool alt) {
    std::vector<MoveCommand> commands(game.bodies.size());
    commands[0].fire = !alt;
    commands[0].altFire = alt;
    tick(world, set, RULES, game, commands, DT);
    commands[0].fire = commands[0].altFire = false;
    tick(world, set, RULES, game, commands, DT);
}

/// The dummy's health lost, and its velocity, each tick of held fire; the
/// dummy is put back standing still before each tick, so each tick's push
/// is read alone.
struct Hits {
    std::vector<double> losses;
    std::vector<Vec3> pushes;
};
Hits hitsWhileHeld(const BodyWorld& world, const WeaponSet& set, Game& game, int ticks, bool alt = false,
                   std::int32_t pitch = 0) {
    std::vector<MoveCommand> commands(game.bodies.size());
    commands[0].fire = !alt;
    commands[0].altFire = alt;
    commands[0].pitch = pitch;
    commands[1].yaw = 32768;
    const uta::uworld::MoveState still = game.bodies[1].move;
    Hits hits;
    for (int k = 0; k < ticks; ++k) {
        game.bodies[1].move = still;
        const double before = game.bodies[1].health;
        tick(world, set, RULES, game, commands, DT);
        hits.losses.push_back(before - game.bodies[1].health);
        hits.pushes.push_back(game.bodies[1].move.velocity);
    }
    return hits;
}

double across(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y); }

} // namespace

TEST_CASE("SS 4.9: a rocket speeds up by its acceleration, to its most", "[ugame][game]") {
    const BodyWorld world = arena();
    WeaponSet set = unspreadRockets();
    Game game = duel(set, "rocket-launcher", 1900);
    game.bodies[1].move.location.y = 500;
    tapCharge(world, set, game, false);
    REQUIRE(game.shots.size() == 1);
    const Projectile& rocket = set.projectiles[kindOf(set, "rocket")];
    CHECK(uta::uworld::length(launchedThisTick(set, game.shots[0]).velocity - Vec3{rocket.speed, 0, 0}) < 1e-9);
    std::vector<MoveCommand> idle(2);
    double expected = rocket.speed + rocket.acceleration * DT; // it flew in its launch tick
    for (int k = 0; k < 10; ++k) {
        tick(world, set, RULES, game, idle, DT);
        expected += rocket.acceleration * DT;
    }
    REQUIRE(game.shots.size() == 1);
    CHECK(std::abs(game.shots[0].velocity.x - expected) < 1e-9);

    // A most just past the launch speed stops it there.
    set.projectiles[kindOf(set, "rocket")].maxSpeed = rocket.speed + 5;
    Game capped = duel(set, "rocket-launcher", 1900);
    capped.bodies[1].move.location.y = 500;
    tapCharge(world, set, capped, false);
    for (int k = 0; k < 10; ++k) tick(world, set, RULES, capped, idle, DT);
    REQUIRE(capped.shots.size() == 1);
    CHECK(std::abs(capped.shots[0].velocity.x - (rocket.speed + 5)) < 1e-9);
}

TEST_CASE("SS 4.9: a tossed shot leaves with its lift, its share of the firer's speed and its draws", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    SECTION("the bio gel rises by its lift") {
        Game game = duel(set, "bio-rifle", 1900);
        game.bodies[1].move.location.y = 500;
        hold(world, set, game, 1);
        REQUIRE(game.shots.size() == 1);
        const Vec3 velocity = launchedThisTick(set, game.shots[0]).velocity;
        CHECK(uta::uworld::length(velocity - Vec3{set.projectiles[kindOf(set, "bio-gel")].speed, 0, 120}) < 1e-9);
    }
    SECTION("a grenade takes 0.4 of the firer's speed, and its speed and fuse are drawn in range") {
        const WeaponSet unspread = unspreadRockets();
        const Projectile& grenade = set.projectiles[kindOf(set, "grenade")];
        std::set<double> speeds, fuses;
        for (int i = 0; i < 8; ++i) {
            Game game = duel(unspread, "rocket-launcher", 1900);
            game.random += static_cast<std::uint64_t>(i);
            game.bodies[1].move.location.y = 500;
            Body& firer = game.bodies[0];
            firer.move.location.z = 1000; // in the air, coasting
            firer.move.physics = uta::uworld::Physics::Falling;
            firer.move.velocity = {300, 0, 0};
            tapCharge(world, unspread, game, true);
            REQUIRE(game.shots.size() == 1);
            const Shot shot = launchedThisTick(unspread, game.shots[0]);
            const double own = shot.velocity.x - grenade.inherit * 300;
            CHECK(own >= 600);
            CHECK(own <= 700);
            CHECK(std::abs(shot.velocity.z - 210) < 1e-9);
            CHECK(shot.lifetime >= 2.5);
            CHECK(shot.lifetime <= 3.0);
            speeds.insert(own);
            fuses.insert(shot.lifetime);
        }
        CHECK(speeds.size() > 1);
        CHECK(fuses.size() > 1);
    }
    SECTION("the flak's chunks leave at their drawn speeds") {
        Game game = duel(set, "flak-cannon", 1900);
        game.bodies[1].move.location.y = 500;
        hold(world, set, game, 1);
        REQUIRE(game.shots.size() == primaryOf(set, "flak-cannon").count);
        std::set<double> speeds;
        for (const Shot& chunk : game.shots) {
            const double speed = uta::uworld::length(chunk.velocity);
            CHECK(speed >= 2400 - 1e-9);
            CHECK(speed <= 2600 + 1e-9);
            speeds.insert(speed);
        }
        CHECK(speeds.size() > 1);
    }
}

TEST_CASE("SS 4.9: the Minigun's damage is drawn from 9 to 14 and pushes nothing", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    Game game = duel(set, "minigun", 400);
    const Hits hits = hitsWhileHeld(world, set, game, 400);
    std::set<double> allowed, seen;
    for (int damage = 9; damage <= 14; ++damage) allowed.insert(landed(set, damage));
    for (std::size_t k = 0; k < hits.losses.size(); ++k) {
        if (hits.losses[k] == 0) continue;
        CHECK(allowed.contains(hits.losses[k]));
        CHECK(hits.pushes[k] == Vec3{});
        seen.insert(hits.losses[k]);
    }
    CHECK(seen.size() > 2);
}

TEST_CASE("SS 4.9: one Enforcer hit in five pushes five times as hard", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    Game game = duel(set, "enforcer", 400);
    const Hits hits = hitsWhileHeld(world, set, game, 900);
    int plain = 0, hard = 0;
    for (std::size_t k = 0; k < hits.losses.size(); ++k) {
        if (hits.losses[k] == 0) continue;
        const double speed = across(hits.pushes[k]);
        CAPTURE(k, speed);
        // The primary's spread tilts a push off the line between the bodies.
        if (std::abs(speed - 30) < 0.1) ++plain;
        else if (std::abs(speed - 150) < 0.1) ++hard;
        else FAIL("a push of neither 3000 nor 15000");
    }
    CAPTURE(plain, hard);
    CHECK(hard > 0);
    CHECK(plain > hard);
}

TEST_CASE("SS 4.9: a held mode's cone widens by spread-grow a shot to spread-max, and narrows at a new press",
          "[ugame][game]") {
    const BodyWorld world = arena();
    const FireMode& stockAlt = *stock().weapons[static_cast<std::size_t>(weaponNamed(stock(), "enforcer"))].alt;
    REQUIRE(stockAlt.spreadGrow > 0);
    REQUIRE(stockAlt.windUp > 0); // its first shot leaves through the wind-up
    // Numbers that make each part show: no cone at first, one shot's growth
    // its whole width.
    WeaponSet set = stock();
    FireMode& alt = *set.weapons[static_cast<std::size_t>(weaponNamed(set, "enforcer"))].alt;
    alt.spread = 0;
    alt.spreadGrow = 1000;
    alt.spreadMax = 1000;
    Game game = duel(set, "enforcer", 1500);
    game.bodies[1].move.location.y = -2500; // out of the way
    // Each shot's turn off the aim, in angle units: the larger of its yaw's and pitch's.
    const auto turnOf = [](const Event& shot) {
        const Vec3 d = shot.to - shot.from;
        const double units = 32768 / std::numbers::pi;
        return std::max(std::abs(std::atan2(d.y, d.x)), std::abs(std::atan2(d.z, std::hypot(d.x, d.y)))) * units;
    };
    std::vector<double> turns;
    std::vector<MoveCommand> commands(2);
    for (int k = 0; k < 300; ++k) {
        commands[0].altFire = k != 150; // let go once, and press again
        tick(world, set, RULES, game, commands, DT);
        for (const Event& event : game.events)
            if (event.kind == Event::Kind::InstantHit) turns.push_back(turnOf(event));
        if (k == 150) {
            REQUIRE(turns.size() >= 4);
            turns.push_back(-1); // marks the new press
        }
    }
    CAPTURE(turns);
    const auto press = std::ranges::find(turns, -1.0);
    REQUIRE(press - turns.begin() >= 4);
    REQUIRE(turns.end() - press >= 3);
    // A press's first shot has no cone, its second has one shot's growth,
    // and none passes the most.
    CHECK(turns[0] < 1);
    CHECK(turns[1] > 1);
    CHECK(press[1] < 1);
    CHECK(press[2] > 1);
    for (const double turn : turns) CHECK(turn <= 1000.5);
    // Held on, it is still the whole width: the draws reach past half of it.
    CHECK(*std::ranges::max_element(turns.begin() + 1, press) > 500);
}

TEST_CASE("SS 4.9: the sniper's head hit and body hit, and no alt fire", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    CHECK_FALSE(set.weapons[static_cast<std::size_t>(weaponNamed(set, "sniper-rifle"))].alt);
    // Level, the eye's line meets the dummy 27 above its centre: its head.
    // Aimed 1000 down, it drops about 57 over 600: its body.
    for (const auto& [pitch, damage, push] : {std::tuple{0, 100.0, 350.0}, std::tuple{-1000, 45.0, 300.0}}) {
        CAPTURE(pitch);
        Game game = duel(set, "sniper-rifle", 600);
        const Hits hits = hitsWhileHeld(world, set, game, 1, false, pitch);
        CHECK(hits.losses[0] == landed(set, damage));
        CHECK(std::abs(across(hits.pushes[0]) - push * across(aimOf(0, pitch))) < 1e-9);
    }
}

TEST_CASE("SS 4.9: the hammer's alt pushes by its hit's distance, counted into the body", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    const FireMode& alt = *set.weapons[static_cast<std::size_t>(weaponNamed(set, "impact-hammer"))].alt;
    REQUIRE(alt.distanceScale > 0);
    // Pawn.AdjustHitLocation: the hit is 0.4 of the body's radius past
    // where the trace meets it, less a thousandth of the trace
    // (UPrimitive::LineCheck), and the push is by that distance.
    std::array<double, 2> push{}, distance{};
    for (std::size_t i = 0; i < 2; ++i) {
        const double away = i == 0 ? 60 : 100;
        Game game = duel(set, "impact-hammer", away);
        const double radius = game.bodies[1].size.radius;
        distance[i] = away - std::sqrt(radius * radius - alt.offset[1] * alt.offset[1]) - alt.offset[0] + 0.4 * radius - 0.001 * alt.range;
        push[i] = hitsWhileHeld(world, set, game, 1, true).pushes[0].x;
        CHECK(push[i] > 0);
    }
    CHECK(std::abs(push[0] / push[1] - distance[0] / distance[1]) < 1e-9);

    // UTA-0498: on a player, PlayerPawn.AdjustHitLocation's 0.5.
    Game game = duel(set, "impact-hammer", 60);
    game.bodies[1].hitInset = PLAYER_HIT_INSET;
    const double radius = game.bodies[1].size.radius;
    const double further = distance[0] + (PLAYER_HIT_INSET - PAWN_HIT_INSET) * radius;
    CHECK(std::abs(hitsWhileHeld(world, set, game, 1, true).pushes[0].x / push[0] - further / distance[0]) < 1e-9);
}

TEST_CASE("UTA-0468: the hammer's alt turns shots near and ahead aside, keeping their speed", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    const FireMode& alt = *set.weapons[static_cast<std::size_t>(weaponNamed(set, "impact-hammer"))].alt;
    REQUIRE(alt.deflectRadius > 0);
    const auto rocket = static_cast<std::uint32_t>(
        std::ranges::find(set.projectiles, "rocket", &Projectile::name) - set.projectiles.begin());
    // A rocket at `at` flying `velocity`, with no acceleration of its own,
    // and the velocity it has after one tick of the hammer's alt; and where
    // the firer's centre stood as it fired, after the tick's move.
    Vec3 centre{};
    const auto after = [&](const Vec3& at, const Vec3& velocity) {
        Game game = duel(set, "impact-hammer", 1500);
        game.shots.push_back(Shot{.kind = rocket, .owner = 1, .location = at, .velocity = velocity, .previous = at,
                                  .lifetime = 10});
        hold(world, set, game, 1, true);
        REQUIRE(game.shots.size() == 1);
        centre = game.bodies[0].move.location;
        return game.shots[0].velocity;
    };
    const double eye = STANDING + EYE_HEIGHT;
    const auto pushedBy = [&](const Vec3& at) { return alt.deflectPush - uta::uworld::length(at - centre); };

    // Ahead and heading a little right: pushed right, by the push less its
    // distance from the firer's centre, at the same speed.
    const Vec3 at{300, 20, eye};
    const Vec3 v{-900, 50, 0};
    const Vec3 got = after(at, v);
    const Vec3 turned = v + Vec3{0, pushedBy(at), 0};
    const Vec3 want = turned * (uta::uworld::length(v) / uta::uworld::length(turned));
    CHECK(uta::uworld::length(got - want) < 1e-9);
    CHECK(std::abs(uta::uworld::length(got) - uta::uworld::length(v)) < 1e-9);
    // Heading a little left: pushed left.
    CHECK(after(at, {-900, -50, 0}).y < -50);
    // Outside the cone, or past the radius: untouched.
    CHECK(after({300, 300, eye}, v) == v);
    CHECK(after({alt.deflectRadius + 10, 0, eye}, v) == v);
}

TEST_CASE("UTA-0468: the hammer's alt turns aside no shot the level hides", "[ugame][game]") {
    uta::ubundle::Collision collision;
    collision.level = solidBlocks({box({-3000, -3000, -64}, {3000, 3000, 0}), box({100, -3000, 0}, {164, 3000, 600})});
    auto built = BodyWorld::build(collision, {});
    REQUIRE(built.has_value());
    const WeaponSet& set = stock();
    const auto rocket = static_cast<std::uint32_t>(
        std::ranges::find(set.projectiles, "rocket", &Projectile::name) - set.projectiles.begin());
    Game game = duel(set, "impact-hammer", 1500);
    const Vec3 at{300, 0, STANDING + EYE_HEIGHT};
    const Vec3 v{-900, 50, 0};
    game.shots.push_back(Shot{.kind = rocket, .owner = 1, .location = at, .velocity = v, .previous = at, .lifetime = 10});
    hold(*built, set, game, 1, true);
    REQUIRE(game.shots.size() == 1);
    CHECK(game.shots[0].velocity == v);
}

namespace {

std::uint32_t kindNamed(const WeaponSet& set, std::string_view name) {
    const auto at = std::ranges::find(set.projectiles, name, &Projectile::name);
    REQUIRE(at != set.projectiles.end());
    return static_cast<std::uint32_t>(at - set.projectiles.begin());
}

/// `ticks` idle ticks: nobody presses anything.
void idle(const BodyWorld& world, const WeaponSet& set, Game& game, int ticks) {
    std::vector<MoveCommand> commands(game.bodies.size());
    commands[1].yaw = 32768;
    for (int k = 0; k < ticks; ++k) tick(world, set, RULES, game, commands, DT);
}

/// Ticks of idle that pass the launcher's look-every once, and twice.
int looksTicks(const WeaponSet& set, int looks) {
    return static_cast<int>(std::ceil(looks * primaryOf(set, "rocket-launcher").lockEvery / DT)) + 1;
}

} // namespace

TEST_CASE("UTA-0468: an idle launcher locks on a body in sight near its aim at its first look", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    Game game = duel(set, "rocket-launcher", 800);
    // Its look on going idle finds the body; the look lock-every later finds
    // it again, which locks it.
    idle(world, set, game, looksTicks(set, 1) - 3);
    CHECK(game.bodies[0].lockTarget == -1);
    CHECK(game.bodies[0].lockCandidate == 1);
    idle(world, set, game, 3);
    CHECK(game.bodies[0].lockTarget == 1);
    // A tap: the rocket it fires seeks the body.
    hold(world, set, game, 1);
    idle(world, set, game, 1);
    REQUIRE(game.shots.size() == 1);
    CHECK(game.shots[0].kind == kindNamed(set, "seeking-rocket"));
    CHECK(game.shots[0].seeking == 1);
}

TEST_CASE("UTA-0468: a body that comes into view locks only at the second look that finds it", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    Game game = duel(set, "rocket-launcher", 800);
    game.bodies[1].move.location = {700, 404, STANDING}; // off the aim as the launcher goes idle
    idle(world, set, game, 2);
    REQUIRE(game.bodies[0].lockCandidate == -1);
    game.bodies[1].move.location = {800, 0, STANDING};
    idle(world, set, game, looksTicks(set, 1));
    CHECK(game.bodies[0].lockCandidate == 1);
    CHECK(game.bodies[0].lockTarget == -1);
    idle(world, set, game, looksTicks(set, 1));
    CHECK(game.bodies[0].lockTarget == 1);
}

TEST_CASE("UTA-0468: the launcher locks on no body off its aim, out of range or hidden", "[ugame][game]") {
    const WeaponSet& set = stock();
    const FireMode& rockets = primaryOf(set, "rocket-launcher");
    const auto locksOn = [&](const BodyWorld& world, const Vec3& at) {
        Game game = duel(set, "rocket-launcher", 800);
        game.bodies[1].move.location = at;
        idle(world, set, game, looksTicks(set, 2));
        return game.bodies[0].lockTarget;
    };
    // A floor with no wall: the arena's would hide a body past lock-range.
    uta::ubundle::Collision floorOnly;
    floorOnly.level = solidBlocks({box({-3000, -3000, -64}, {3000, 3000, 0})});
    auto built = BodyWorld::build(floorOnly, {});
    REQUIRE(built.has_value());
    const BodyWorld& open = *built;
    CHECK(locksOn(open, {800, 0, STANDING}) == 1);
    // 30 degrees off the aim: a cosine of 0.87, under lock-aim.
    REQUIRE(rockets.lockAim > 0.87);
    CHECK(locksOn(open, {700, 404, STANDING}) == -1);
    CHECK(locksOn(open, {rockets.lockRange - 50, 0, STANDING}) == 1);
    CHECK(locksOn(open, {rockets.lockRange + 50, 0, STANDING}) == -1);
    uta::ubundle::Collision collision;
    collision.level = solidBlocks({box({-3000, -3000, -64}, {3000, 3000, 0}), box({400, -3000, 0}, {464, 3000, 600})});
    auto walled = BodyWorld::build(collision, {});
    REQUIRE(walled.has_value());
    CHECK(locksOn(*walled, {800, 0, STANDING}) == -1);
}

TEST_CASE("UTA-0468: a lock the shot's own look loses fires a plain rocket; the grenades drop it", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    Game game = duel(set, "rocket-launcher", 800);
    idle(world, set, game, looksTicks(set, 2));
    REQUIRE(game.bodies[0].lockTarget == 1);
    game.bodies[1].move.location = {700, 404, STANDING}; // off the aim before the press
    hold(world, set, game, 1);
    idle(world, set, game, 1);
    REQUIRE(game.shots.size() == 1);
    CHECK(game.shots[0].kind == kindNamed(set, "rocket"));
    CHECK(game.shots[0].seeking == -1);

    Game alt = duel(set, "rocket-launcher", 800);
    idle(world, set, alt, looksTicks(set, 2));
    REQUIRE(alt.bodies[0].lockTarget == 1);
    hold(world, set, alt, 1, true);
    idle(world, set, alt, 1);
    CHECK(alt.bodies[0].lockTarget == -1);
}

TEST_CASE("UTA-0468: a seeking rocket turns towards its body every seek-every, keeping its speed", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    const Projectile& kind = set.projectiles[kindNamed(set, "seeking-rocket")];
    // Seeks the body at `at` from the origin's air, flying +X; the velocity
    // after the ticks that pass seek-every once.
    const auto flown = [&](const Vec3& at, int ticks) {
        Game game = duel(set, "enforcer", 1500);
        game.bodies[1].move.location = at;
        const Vec3 from{0, 0, 300};
        game.shots.push_back(Shot{.kind = kindNamed(set, "seeking-rocket"), .owner = 0, .location = from,
                                  .velocity = {900, 0, 0}, .previous = from, .lifetime = 10, .seeking = 1});
        idle(world, set, game, ticks);
        REQUIRE(game.shots.size() == 1);
        return game.shots[0];
    };
    const int once = static_cast<int>(std::ceil(kind.seekEvery / DT));
    const Shot before = flown({1200, 600, STANDING}, once - 1);
    CHECK(before.velocity == Vec3{900, 0, 0});
    const Shot turned = flown({1200, 600, STANDING}, once);
    CHECK(turned.velocity.y > 0);
    CHECK(turned.velocity.z < 0);
    // Its speed is kept by the turn; the move after gains its new acceleration.
    CHECK(std::abs(uta::uworld::length(turned.velocity) - 900 - kind.seekAcceleration * DT) < 1e-6);
    CHECK(std::abs(uta::uworld::length(turned.acceleration) - kind.seekAcceleration) < 1e-9);
    // A body behind the way it first flew is not sought.
    CHECK(flown({-600, 600, STANDING}, once).velocity == Vec3{900, 0, 0});
}

TEST_CASE("UTA-0468: a doubled Enforcer's second gun fires double-delay after the first, from the other side",
          "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    const Weapon& enforcer = set.weapons[static_cast<std::size_t>(weaponNamed(set, "enforcer"))];
    REQUIRE(enforcer.doubleDelay > 0);
    // The tick of each instant hit over `ticks` from a tap, and its start's side.
    const auto shots = [&](bool doubled, bool alt, int ticks, bool held) {
        Game game = duel(set, "enforcer", 400);
        game.bodies[0].weapons[static_cast<std::size_t>(weaponNamed(set, "enforcer"))].doubled = doubled;
        std::vector<std::pair<int, double>> out;
        std::vector<MoveCommand> commands(game.bodies.size());
        commands[1].yaw = 32768;
        for (int k = 0; k < ticks; ++k) {
            commands[0].fire = !alt && (held || k == 0);
            commands[0].altFire = alt && (held || k == 0);
            tick(world, set, RULES, game, commands, DT);
            for (const Event& event : game.events)
                if (event.kind == Event::Kind::InstantHit) out.emplace_back(k, event.from.y);
        }
        return out;
    };
    const int window = static_cast<int>(std::ceil(enforcer.primary.refire / DT)) - 1;
    CHECK(shots(false, false, window, false).size() == 1);
    const auto two = shots(true, false, window, false);
    REQUIRE(two.size() == 2);
    CHECK(two[0].first == 0);
    CHECK(std::abs(two[1].first * DT - enforcer.doubleDelay) <= DT);
    CHECK(two[0].second > 0);
    CHECK(two[1].second == -two[0].second);

    // The alt: the second gun turns too, from double-delay after the press.
    const FireMode& alt = *enforcer.alt;
    const auto alts = shots(true, true, static_cast<int>(std::ceil((enforcer.doubleDelay + alt.windUp) / DT)) + 2, true);
    REQUIRE(alts.size() >= 2);
    const auto second = std::ranges::find_if(alts, [](const auto& s) { return s.second < 0; });
    REQUIRE(second != alts.end());
    CHECK(std::abs(second->first * DT - (enforcer.doubleDelay + alt.windUp)) <= DT);
}

TEST_CASE("UTA-0468: a doubled weapon's first gun takes the double spread too", "[ugame][game]") {
    const BodyWorld world = arena();
    WeaponSet set = stock();
    const auto at = static_cast<std::size_t>(weaponNamed(set, "enforcer"));
    set.weapons[at].primary.spread = 0;
    set.weapons[at].primary.doubleSpread = 4000;
    // The first gun's one shot from a tap: straight along +X unless spread.
    const auto firstShot = [&](bool doubled) {
        Game game = duel(set, "enforcer", 3000);
        game.bodies[0].weapons[at].doubled = doubled;
        hold(world, set, game, 1);
        const auto shot = std::ranges::find(game.events, Event::Kind::InstantHit, &Event::kind);
        REQUIRE(shot != game.events.end());
        const Vec3 way = shot->to - shot->from;
        return std::hypot(way.y, way.z) / uta::uworld::length(way);
    };
    CHECK(firstShot(false) < 1e-9);
    CHECK(firstShot(true) > 1e-3);
}

TEST_CASE("UTA-0468: the second gun shares the ammo and spreads by its own keys", "[ugame][game]") {
    const WeaponSet& set = stock();
    const FireMode& primary = primaryOf(set, "enforcer");
    Body body = bodyAt(set, {0, 0, STANDING}, 0);
    CHECK(detail::startOf(body, primary, true).y == -detail::startOf(body, primary, false).y);

    const BodyWorld world = arena();
    Game game = duel(set, "enforcer", 400);
    const auto at = static_cast<std::size_t>(weaponNamed(set, "enforcer"));
    game.bodies[0].weapons[at].doubled = true;
    game.bodies[0].weapons[at].ammo = 1;
    hold(world, set, game, static_cast<int>(std::ceil(0.5 / DT)));
    CHECK(game.bodies[0].weapons[at].ammo == 0); // one round: the first gun's; the second finds none
}

TEST_CASE("SS 4.9: the chainsaw's cut pulls, and its slash lands after its wind-up and pushes left", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    const Weapon& saw = set.weapons[static_cast<std::size_t>(weaponNamed(set, "chainsaw"))];
    REQUIRE(saw.primary.momentum < 0);
    REQUIRE(saw.alt->momentumSide < 0);
    // Facing +X with the dummy in reach: the cut pulls it back along -X.
    Game cut = duel(set, "chainsaw", 60);
    const Hits cuts = hitsWhileHeld(world, set, cut, 40);
    CHECK(cuts.losses[0] == landed(set, saw.primary.damage));
    CHECK(cuts.pushes[0].x < 0);
    CHECK(cuts.pushes[0].y == 0);
    // The cut after the press's comes refire-lead early; the rest a refire apart.
    std::vector<int> at;
    for (int k = 0; k < 40; ++k)
        if (cuts.losses[static_cast<std::size_t>(k)] > 0) at.push_back(k);
    REQUIRE(at.size() >= 3);
    CHECK(std::abs(at[1] - (saw.primary.refire + saw.primary.refireLead) / DT) <= 1);
    CHECK(std::abs(at[2] - at[1] - saw.primary.refire / DT) <= 1);

    // The slash lands a wind-up after its press, and pushes along the
    // firer's left: +Y is its right.
    Game slash = duel(set, "chainsaw", 60);
    const Hits slashes = hitsWhileHeld(world, set, slash, 40, true);
    int first = 0;
    while (first < 40 && slashes.losses[static_cast<std::size_t>(first)] == 0) ++first;
    REQUIRE(first < 40);
    CHECK(std::abs(first + 1 - saw.alt->windUp / DT) <= 1);
    CHECK(slashes.losses[static_cast<std::size_t>(first)] == landed(set, saw.alt->damage));
    CHECK(slashes.pushes[static_cast<std::size_t>(first)].y < 0);
    CHECK(std::abs(slashes.pushes[static_cast<std::size_t>(first)].x) < 1e-9);

    // ChainSaw.TraceFire: the cut's trace ends its range from the firer's
    // centre, not from where it starts, so it slants in from the saw.
    REQUIRE(saw.primary.rangeFromBody);
    Game far = duel(set, "chainsaw", 600);
    const Vec3 centre = far.bodies[0].move.location;
    hold(world, set, far, 1);
    const auto melee = std::ranges::find(far.events, Event::Kind::Melee, &Event::kind);
    REQUIRE(melee != far.events.end());
    CHECK(std::abs(melee->to.x - (centre.x + saw.primary.range)) < 1e-9);
    CHECK(std::abs(melee->to.y - centre.y) < 1e-9);
    CHECK(std::abs(melee->to.z - centre.z) < 1e-3); // the firer settles a hair in its first tick
    CHECK(melee->from.y != centre.y);
}

TEST_CASE("SS 4.9: the pulse beam grows to a body, hurts it at a touch and then by its store", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    const FireMode& beam = *set.weapons[static_cast<std::size_t>(weaponNamed(set, "pulse-gun"))].alt;
    REQUIRE(beam.extend == 6259);
    REQUIRE(beam.firstHit == 0.1);
    REQUIRE(beam.hitEvery == 0.22);
    Game game = duel(set, "pulse-gun", 400);
    game.bodies[1].move.location.y = levelStart(beam).y;
    const Hits hits = hitsWhileHeld(world, set, game, 40, true);
    // The dummy's near side is 383 off, 366.5 along a beam that starts 16.5
    // out: 81 a tick reaches it on the fifth. The touch hurts as 0.1 s.
    const int touch = 4;
    // The store starts with the touch's own tick and pays once past 0.22 s:
    // 17 ticks are 0.22 to a hair, so the store is summed as the tick sums it.
    int stored = 0;
    double held = 0;
    do held += DT, ++stored;
    while (held <= beam.hitEvery);
    for (int k = 0; k < 40; ++k) {
        CAPTURE(k);
        const double loss = k == touch                ? landed(set, beam.damage * beam.firstHit)
                            : k == touch + stored - 1 ? landed(set, beam.damage * held)
                                                      : 0;
        if (k < touch + stored) CHECK(hits.losses[static_cast<std::size_t>(k)] == loss);
    }

    // A body the beam leaves takes what was stored on it.
    Game left = duel(set, "pulse-gun", 400);
    left.bodies[1].move.location.y = levelStart(beam).y;
    hitsWhileHeld(world, set, left, touch + 6, true);
    left.bodies[1].move.location.y += 500;
    const double before = left.bodies[1].health;
    hold(world, set, left, 1, true);
    CHECK(before - left.bodies[1].health == landed(set, beam.damage * 6 * DT));
    // And touched again at once, takes half the time since, not the whole first hit.
    left.bodies[1].move.location.y -= 500;
    const double again = left.bodies[1].health;
    hold(world, set, left, 1, true);
    CHECK(again - left.bodies[1].health == landed(set, beam.damage * 0.5 * 2 * DT));
}

TEST_CASE("SS 4.9: the pulse beam spends a round at its press and one each 0.24 s held", "[ugame][game]") {
    const BodyWorld world = arena();
    Game game = duel(stock(), "pulse-gun", 1900);
    // 0.24 s is 18.5 ticks and what is past it is kept: 19, 38 (0.48 s), 56 (0.72 s).
    CHECK(spendTicks(world, game, 60, true) == std::vector{1, 19, 38, 56});
    // INV-7: with one round the press cannot be paid for beside the hold's.
    Game low = duel(stock(), "pulse-gun", 400);
    low.bodies[1].move.location.y = levelStart(*stock().weapons[static_cast<std::size_t>(low.bodies[0].weapon)].alt).y;
    low.bodies[0].weapons[static_cast<std::size_t>(low.bodies[0].weapon)].ammo = 1;
    const Hits none = hitsWhileHeld(world, stock(), low, 40, true);
    CHECK(std::ranges::all_of(none.losses, [](double loss) { return loss == 0; }));
    CHECK(low.bodies[0].weapons[static_cast<std::size_t>(low.bodies[0].weapon)].ammo == 1);
}

namespace {

/// INV-5's script: tick k's commands for a shooter and a strafing dummy.
/// The shooter walks the slots, holding fire or alt, aiming across the dummy.
std::vector<MoveCommand> scriptAt(int k) {
    std::vector<MoveCommand> commands(2);
    MoveCommand& shooter = commands[0];
    shooter.yaw = (k * 37) % 1200 - 600;
    shooter.pitch = (k * 13) % 600 - 300;
    if (k % 120 == 0) shooter.weaponSlot = static_cast<std::int8_t>((k / 120) % 10);
    shooter.fire = k % 120 > 30 && k % 120 < 80;
    shooter.altFire = k % 120 >= 85 && k % 120 < 115;
    shooter.forward = k % 300 < 40 ? 1 : 0;
    shooter.wheelOpen = k % 240 >= 200; // UTA-0020 INV-4: over the end of an alt-fire hold
    MoveCommand& dummy = commands[1];
    dummy.yaw = 32768;
    dummy.strafe = (k / 60) % 2 == 0 ? 1 : -1;
    return commands;
}

/// The state's hash after each tick, while frames of `frames` (cycled) pass.
/// `kinds` gathers which kinds of event the script made.
std::vector<std::uint64_t> scripted(const BodyWorld& world, const std::vector<double>& frames, int ticks,
                                    std::vector<Event::Kind>* kinds = nullptr) {
    uta::uworld::FixedTick clock;
    Game game = duel(stock(), "impact-hammer", 900);
    game.bodies[0].human = true; // UTA-0020 INV-4: the open wheel slows this game
    std::vector<std::uint64_t> hashes;
    for (std::size_t f = 0; static_cast<int>(hashes.size()) < ticks; ++f) {
        const int due = clock.advance(frames[f % frames.size()]);
        for (int t = 0; t < due; ++t) {
            tick(world, stock(), RULES, game, scriptAt(static_cast<int>(hashes.size())), clock.dt());
            hashes.push_back(stateHash(game));
            if (kinds != nullptr)
                for (const Event& event : game.events)
                    if (std::ranges::find(*kinds, event.kind) == kinds->end()) kinds->push_back(event.kind);
        }
    }
    hashes.resize(static_cast<std::size_t>(ticks));
    return hashes;
}

} // namespace

TEST_CASE("INV-5: a scripted fight is the same at every frame rate and on every compiler", "[ugame][game]") {
    const BodyWorld world = arena();
    constexpr int TICKS = 1200;
    const auto start = std::chrono::steady_clock::now();
    std::vector<Event::Kind> kinds;
    const auto reference = scripted(world, {1.0 / 60}, TICKS, &kinds);
    // The script is worth hashing only if it fires every archetype and
    // something explodes; INV-8 covers the combo.
    for (const Event::Kind kind : {Event::Kind::InstantHit, Event::Kind::Beam, Event::Kind::Melee,
                                   Event::Kind::Explosion})
        CHECK(std::ranges::find(kinds, kind) != kinds.end());
    const auto took = std::chrono::steady_clock::now() - start;
    // SS 13: a busy tick's cost, printed rather than asserted.
    const double micros = std::chrono::duration<double, std::micro>(took).count() / TICKS;
    WARN("a scripted tick took " << micros << " us");
    for (const auto& frames : std::vector<std::vector<double>>{{1.0 / 30}, {1.0 / 144}, {0.004, 0.031, 0.0167}}) {
        const auto again = scripted(world, frames, TICKS);
        CAPTURE(frames.front());
        CHECK(again == reference);
    }
    INFO("final hash " << reference.back());
    // UTA-0020: the script opens the wheel. UTA-0022: the hash covers coyoteLeft and mantleTo;
    // UTA-0108 landedZ; UTA-0493 painClock; UTA-0468 the lock, the second gun and the seeking,
    // each of which moved the pin and nothing else.
    CHECK(reference.back() == 12121377059366180465u);
}

// UTA-0020: the weapon wheel's time scale, and what it lists and picks.

namespace {

/// The time scale one tick of `game` sets, the shooter's command having the
/// wheel open or not; the dummy's has it where `dummyOpens`.
double scaleAfterTick(const BodyWorld& world, Game game, bool shooterOpens, bool dummyOpens = false) {
    std::vector<MoveCommand> commands(game.bodies.size());
    commands[0].wheelOpen = shooterOpens;
    commands[1].wheelOpen = dummyOpens;
    tick(world, stock(), RULES, game, commands, DT);
    return game.timeScale;
}

} // namespace

TEST_CASE("UTA-0020 INV-1: the open wheel slows time only for one live human", "[ugame][game]") {
    const BodyWorld world = arena();
    Game game = duel(stock(), "enforcer", 900);
    game.bodies[0].human = true;
    CHECK(scaleAfterTick(world, game, true) == WHEEL_TIME_SCALE);
    CHECK(scaleAfterTick(world, game, false) == 1);
    // A non-human's bit is not read: the dummy opening a wheel slows nothing.
    CHECK(scaleAfterTick(world, game, false, true) == 1);

    // Two humans, either one opening a wheel: nobody is slowed.
    Game two = game;
    two.bodies[1].human = true;
    CHECK(scaleAfterTick(world, two, true) == 1);
    CHECK(scaleAfterTick(world, two, false, true) == 1);

    Game dead = game;
    dead.bodies[0].health = 0;
    CHECK(scaleAfterTick(world, dead, true) == 1);

    // Set each tick, so a slowed game runs at full speed again once the wheel closes.
    game.timeScale = WHEEL_TIME_SCALE;
    CHECK(scaleAfterTick(world, game, false) == 1);
}

TEST_CASE("UTA-0020 INV-2: the wheel bit changes nothing but the time scale", "[ugame][game]") {
    const BodyWorld world = arena();
    Game open = duel(stock(), "impact-hammer", 900);
    open.bodies[0].human = true;
    Game shut = open;
    bool slowed = false;
    for (int k = 0; k < 600; ++k) {
        std::vector<MoveCommand> commands = scriptAt(k);
        commands[0].wheelOpen = false;
        tick(world, stock(), RULES, shut, commands, DT);
        commands[0].wheelOpen = k % 50 < 20;
        tick(world, stock(), RULES, open, commands, DT);
        slowed = slowed || open.timeScale != 1;
        Game reset = open;
        reset.timeScale = 1;
        CAPTURE(k);
        REQUIRE(stateHash(reset) == stateHash(shut));
    }
    CHECK(slowed); // the bit was read, so the run says something
}

TEST_CASE("UTA-0020 INV-4: the time scale and a body's human are in the hash", "[ugame][game]") {
    const Game game = duel(stock(), "enforcer", 900);
    Game slowed = game;
    slowed.timeScale = WHEEL_TIME_SCALE;
    Game person = game;
    person.bodies[1].human = true;
    CHECK(stateHash(slowed) != stateHash(game));
    CHECK(stateHash(person) != stateHash(game));
}

TEST_CASE("UTA-0020 INV-5: the wheel lists the weapons carried by slot with the 0 key last", "[ugame][game]") {
    const WeaponSet& set = stock();
    Body body = bodyAt(set, {0, 0, STANDING}, 0);
    for (Carried& carried : body.weapons) carried.has = false;
    // Slots 1 (two of them), 2 and 0; not 3.
    for (const char* name : {"sniper-rifle", "enforcer", "chainsaw", "impact-hammer"})
        body.weapons[static_cast<std::size_t>(weaponNamed(set, name))].has = true;
    REQUIRE(set.weapons[static_cast<std::size_t>(weaponNamed(set, "bio-rifle"))].slot == 3);
    CHECK(wheelWeapons(set, body) == std::vector<int>{weaponNamed(set, "impact-hammer"), weaponNamed(set, "chainsaw"),
                                                      weaponNamed(set, "enforcer"), weaponNamed(set, "sniper-rifle")});
}

namespace {

/// What `body` is bringing up after one tick of `command`; -1 for nothing.
int pendingAfter(const BodyWorld& world, Game game, const MoveCommand& command) {
    std::vector<MoveCommand> commands(game.bodies.size());
    commands[0] = command;
    tick(world, stock(), RULES, game, commands, DT);
    return game.bodies[0].pending;
}

} // namespace

TEST_CASE("UTA-0020 INV-6: a pick of a carried weapon wins and any other is refused", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    const Game game = duel(set, "impact-hammer", 900);
    const int rockets = weaponNamed(set, "rocket-launcher");
    const int enforcer = weaponNamed(set, "enforcer");

    MoveCommand picked;
    picked.weaponPick = static_cast<std::int8_t>(enforcer);
    picked.weaponSlot = 9;
    CHECK(pendingAfter(world, game, picked) == enforcer); // the pick wins over the slot

    MoveCommand stepped;
    stepped.weaponPick = 99;
    stepped.weaponStep = 1;
    CHECK(pendingAfter(world, game, stepped) == weaponNamed(set, "chainsaw")); // out of range: the step applies

    Game without = game;
    without.bodies[0].weapons[static_cast<std::size_t>(rockets)].has = false;
    MoveCommand uncarried;
    uncarried.weaponPick = static_cast<std::int8_t>(rockets);
    uncarried.weaponSlot = 2;
    CHECK(pendingAfter(world, without, uncarried) == enforcer); // not carried: the slot applies

    MoveCommand inHand;
    inHand.weaponPick = static_cast<std::int8_t>(game.bodies[0].weapon);
    inHand.weaponSlot = 9;
    CHECK(pendingAfter(world, game, inHand) == -1); // the weapon in hand: nothing changes
}

// docs/specs/UTA-0467-weapon-sounds.md INV-2 to INV-7: the cues a tick raises
// for a client's sounds, the sound each plays and the mode that loops.

namespace {

int cuesOf(const Game& game, Cue::Kind kind) {
    return static_cast<int>(std::ranges::count(game.cues, kind, &Cue::kind));
}

/// The stock set with every loop taken out: what the file names is not
/// under test here.
WeaponSet silentSet() {
    WeaponSet set = stock();
    for (Weapon& weapon : set.weapons) {
        weapon.primary.loopSound.clear();
        weapon.primary.endSound.clear();
        if (weapon.alt) weapon.alt->loopSound.clear(), weapon.alt->endSound.clear();
    }
    return set;
}

/// The silent set with a loop named on the pulse gun's two modes and on the
/// minigun's wound-up primary, and on no other mode.
WeaponSet loopingSet() {
    WeaponSet set = silentSet();
    set.weapons[static_cast<std::size_t>(weaponNamed(set, "pulse-gun"))].primary.loopSound = "test.plasma";
    set.weapons[static_cast<std::size_t>(weaponNamed(set, "pulse-gun"))].alt->loopSound = "test.beam";
    set.weapons[static_cast<std::size_t>(weaponNamed(set, "minigun"))].primary.loopSound = "test.spin";
    return set;
}

} // namespace

TEST_CASE("UTA-0467 INV-2: a shot raises one Fire cue naming its body, weapon and mode", "[ugame][game][cues]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();

    // Eight chunks, one shot, one cue.
    Game flak = duel(set, "flak-cannon", 1900);
    hold(world, set, flak, 1);
    REQUIRE(flak.shots.size() == 8);
    REQUIRE(flak.cues.size() == 1);
    CHECK(flak.cues[0] == Cue{Cue::Kind::Fire, 0, static_cast<std::uint32_t>(weaponNamed(set, "flak-cannon")), 0,
                              flak.bodies[0].move.location});

    Game alt = duel(set, "shock-rifle", 1900);
    hold(world, set, alt, 1, true);
    REQUIRE(cuesOf(alt, Cue::Kind::Fire) == 1);
    CHECK(alt.cues[0].mode == 1);
    CHECK(alt.cues[0].index == static_cast<std::uint32_t>(weaponNamed(set, "shock-rifle")));

    // The same press with no ammo: the fixture differs in ammo alone.
    Game empty = duel(set, "shock-rifle", 1900);
    empty.bodies[0].weapons[static_cast<std::size_t>(empty.bodies[0].weapon)].ammo = 0;
    int heard = 0;
    for (int k = 0; k < 40; ++k) {
        hold(world, set, empty, 1);
        heard += cuesOf(empty, Cue::Kind::Fire);
    }
    CHECK(heard == 0);
}

TEST_CASE("UTA-0467 INV-2: a wound-up shot is cued when it leaves, and a beam never", "[ugame][game][cues]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();

    // The minigun's press pays and winds up; no bullet leaves that tick.
    Game minigun = duel(set, "minigun", 600);
    hold(world, set, minigun, 1);
    CHECK(minigun.events.empty());
    CHECK(cuesOf(minigun, Cue::Kind::Fire) == 0);
    int shots = 0, cues = 0;
    for (int k = 0; k < 60; ++k) {
        hold(world, set, minigun, 1);
        shots += static_cast<int>(std::ranges::count(minigun.events, Event::Kind::InstantHit, &Event::kind));
        cues += cuesOf(minigun, Cue::Kind::Fire);
    }
    CHECK(shots > 1);
    CHECK(cues == shots);

    Game pulse = duel(set, "pulse-gun", 600);
    int beams = 0, fired = 0;
    for (int k = 0; k < 20; ++k) {
        hold(world, set, pulse, 1, true);
        beams += static_cast<int>(std::ranges::count(pulse.events, Event::Kind::Beam, &Event::kind));
        fired += cuesOf(pulse, Cue::Kind::Fire);
    }
    CHECK(beams == 20);
    CHECK(fired == 0);
}

TEST_CASE("UTA-0467 INV-3: starting a switch raises one Select cue for the weapon asked for", "[ugame][game][cues]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    Game game = duel(set, "enforcer", 600);
    const int rockets = weaponNamed(set, "rocket-launcher");
    REQUIRE(set.weapons[static_cast<std::size_t>(rockets)].switchTime > 2 * DT);

    std::vector<MoveCommand> commands(2);
    commands[0].weaponPick = static_cast<std::int8_t>(rockets);
    tick(world, set, RULES, game, commands, DT);
    REQUIRE(game.cues.size() == 1);
    CHECK(game.cues[0] == Cue{Cue::Kind::Select, 0, static_cast<std::uint32_t>(rockets), -1,
                              Vec3{0, 0, STANDING}});
    REQUIRE(game.bodies[0].pending == rockets);

    // Asking again for the weapon already on its way, and the ticks that
    // bring it up, raise none.
    int later = 0;
    tick(world, set, RULES, game, commands, DT);
    later += cuesOf(game, Cue::Kind::Select);
    commands[0].weaponPick = -1;
    for (int k = 0; k < 200 && game.bodies[0].pending >= 0; ++k) {
        tick(world, set, RULES, game, commands, DT);
        later += cuesOf(game, Cue::Kind::Select);
    }
    CHECK(game.bodies[0].weapon == rockets);
    CHECK(later == 0);
}

TEST_CASE("UTA-0467 INV-4: an explosion and a combo are cued where they happen, by kind", "[ugame][game][cues]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    const auto kindOf = [&set](std::string_view name) {
        return static_cast<std::uint32_t>(std::ranges::find(set.projectiles, name, &Projectile::name) -
                                          set.projectiles.begin());
    };

    // A flak shell into the wall: the shell explodes and splits into chunks,
    // which strike later. Every explosion drawn is cued, at its own place,
    // and the shell's cue carries the shell's kind, a chunk's the chunk's.
    Game game = duel(set, "flak-cannon", 1900);
    game.bodies[1].move.location.y = 2500; // out of the way
    std::vector<std::uint32_t> kinds;
    for (int k = 0; k < 400; ++k) {
        hold(world, set, game, 1, k == 0);
        std::vector<Vec3> drawn, cued;
        for (const Event& event : game.events)
            if (event.kind == Event::Kind::Explosion) drawn.push_back(event.from);
        for (const Cue& cue : game.cues) {
            if (cue.kind != Cue::Kind::Explode) continue;
            cued.push_back(cue.at);
            kinds.push_back(cue.index);
            CHECK(cue.owner == 0);
            CHECK(cue.mode == -1);
        }
        CHECK(drawn == cued);
        if (k > 0 && game.shots.empty()) break;
    }
    REQUIRE(!kinds.empty());
    CHECK(kinds.front() == kindOf("flak-shell"));
    CHECK(std::ranges::count(kinds, kindOf("flak-shell")) == 1);
    CHECK(std::ranges::all_of(kinds, [&](std::uint32_t kind) {
        return kind == kindOf("flak-shell") || kind == kindOf("flak-chunk");
    }));

    // INV-8's combo, cued at the ball.
    Game combo = duel(set, "shock-rifle", 1900);
    combo.bodies[1].move.location.y = 500;
    const Vec3 start = levelStart(primaryOf(set, "shock-rifle"));
    const std::uint32_t ball = kindOf("shock-ball");
    combo.shots.push_back(Shot{.kind = ball, .owner = 0, .location = {800, start.y, start.z}, .velocity = {},
                               .lifetime = set.projectiles[ball].lifetime});
    hold(world, set, combo, 1);
    REQUIRE(cuesOf(combo, Cue::Kind::Combo) == 1);
    const Cue& cue = *std::ranges::find(combo.cues, Cue::Kind::Combo, &Cue::kind);
    REQUIRE(cue.index < set.combos.size());
    CHECK(set.combos[cue.index].name == set.projectiles[ball].combo);
    CHECK(cue.at == Vec3{800, start.y, start.z});
    CHECK(cue.owner == 0);
}

TEST_CASE("UTA-0467 INV-5: cues are not part of the state's hash", "[ugame][game][cues]") {
    Game game = duel(stock(), "enforcer", 600);
    Game cued = game;
    cued.cues.push_back(Cue{Cue::Kind::Fire, 0, 2, 1, {1, 2, 3}});
    CHECK(stateHash(cued) == stateHash(game));
}

TEST_CASE("UTA-0467 INV-6: a beam's loop sounds while the beam is on", "[ugame][game][cues]") {
    const BodyWorld world = arena();
    const WeaponSet set = loopingSet();
    Game game = duel(set, "pulse-gun", 600);
    CHECK(loopingMode(set, game.bodies[0]) == -1);
    hold(world, set, game, 5, true);
    CHECK(loopingMode(set, game.bodies[0]) == 1);
    // A set naming no loop: the same beam, silent.
    CHECK(loopingMode(silentSet(), game.bodies[0]) == -1);

    Game dead = game;
    dead.bodies[0].health = 0;
    CHECK(loopingMode(set, dead.bodies[0]) == -1);

    // A switch asked for while the trigger stays down.
    Game switching = game;
    std::vector<MoveCommand> commands(2);
    commands[0].altFire = true;
    commands[0].weaponPick = static_cast<std::int8_t>(weaponNamed(set, "rocket-launcher"));
    tick(world, set, RULES, switching, commands, DT);
    REQUIRE(switching.bodies[0].pending >= 0);
    CHECK(loopingMode(set, switching.bodies[0]) == -1);

    // The trigger up: the tick after, it is silent.
    std::vector<MoveCommand> nothing(2);
    tick(world, set, RULES, game, nothing, DT);
    CHECK(loopingMode(set, game.bodies[0]) == -1);

    // The trigger down on an empty gun: no beam, so no hum.
    Game empty = duel(set, "pulse-gun", 600);
    empty.bodies[0].weapons[static_cast<std::size_t>(empty.bodies[0].weapon)].ammo = 0;
    hold(world, set, empty, 5, true);
    CHECK(loopingMode(set, empty.bodies[0]) == -1);
}

TEST_CASE("UTA-0467 INV-6: a wound-up mode's loop sounds while its press is held", "[ugame][game][cues]") {
    const BodyWorld world = arena();
    const WeaponSet set = loopingSet();
    Game game = duel(set, "minigun", 600);
    hold(world, set, game, 1);
    CHECK(loopingMode(set, game.bodies[0]) == 0); // winding up, before its first bullet
    hold(world, set, game, 60);
    CHECK(loopingMode(set, game.bodies[0]) == 0);
    // The alt trigger held in its place: that mode names no loop.
    Game alt = duel(set, "minigun", 600);
    hold(world, set, alt, 30, true);
    CHECK(loopingMode(set, alt.bodies[0]) == -1);
    // A switch asked for with the trigger still down ends the hold.
    Game switching = game;
    std::vector<MoveCommand> commands(2);
    commands[0].fire = true;
    commands[0].weaponPick = static_cast<std::int8_t>(weaponNamed(set, "rocket-launcher"));
    tick(world, set, RULES, switching, commands, DT);
    REQUIRE(switching.bodies[0].pending >= 0);
    CHECK(loopingMode(set, switching.bodies[0]) == -1);
    std::vector<MoveCommand> nothing(2);
    for (int k = 0; k < 30; ++k) tick(world, set, RULES, game, nothing, DT);
    CHECK(loopingMode(set, game.bodies[0]) == -1);
}

TEST_CASE("UTA-0467 INV-7: soundOf gives the cue's own sound", "[ugame][game][cues]") {
    WeaponSet set = stock();
    const auto gun = static_cast<std::uint32_t>(weaponNamed(set, "shock-rifle"));
    const auto other = static_cast<std::uint32_t>(weaponNamed(set, "sniper-rifle"));
    REQUIRE(set.weapons[gun].alt.has_value());
    REQUIRE_FALSE(set.weapons[other].alt.has_value());
    set.weapons[gun].selectSound = "t.select";
    set.weapons[gun].primary.sound = "t.primary";
    set.weapons[gun].alt->sound = "t.alt";
    set.weapons[other].selectSound = "t.other_select";
    set.weapons[other].primary.sound = "t.other";
    REQUIRE(set.projectiles.size() >= 3);
    set.projectiles[2].sound.clear();
    set.projectiles[0].sound = "t.first";
    set.projectiles[1].sound = "t.second";
    REQUIRE(!set.combos.empty());
    set.combos[0].sound = "t.combo";

    CHECK(soundOf(set, Cue{Cue::Kind::Fire, 0, gun, 0, {}}) == "t.primary");
    CHECK(soundOf(set, Cue{Cue::Kind::Fire, 0, gun, 1, {}}) == "t.alt");
    CHECK(soundOf(set, Cue{Cue::Kind::Fire, 0, other, 0, {}}) == "t.other");
    CHECK(soundOf(set, Cue{Cue::Kind::Fire, 0, other, 1, {}}).empty()); // it has no alt
    CHECK(soundOf(set, Cue{Cue::Kind::Select, 0, gun, -1, {}}) == "t.select");
    CHECK(soundOf(set, Cue{Cue::Kind::Select, 0, other, -1, {}}) == "t.other_select");
    CHECK(soundOf(set, Cue{Cue::Kind::Explode, 0, 0, -1, {}}) == "t.first");
    CHECK(soundOf(set, Cue{Cue::Kind::Explode, 0, 1, -1, {}}) == "t.second");
    CHECK(soundOf(set, Cue{Cue::Kind::Combo, 0, 0, -1, {}}) == "t.combo");
    // One that names nothing, and an index past the set, are empty, not a read.
    CHECK(soundOf(set, Cue{Cue::Kind::Explode, 0, 2, -1, {}}).empty());
    CHECK(soundOf(set, Cue{Cue::Kind::Fire, 0, 999, 0, {}}).empty());
    CHECK(soundOf(set, Cue{Cue::Kind::Explode, 0, 999, -1, {}}).empty());
    CHECK(soundOf(set, Cue{Cue::Kind::Combo, 0, 999, -1, {}}).empty());
}

TEST_CASE("UTA-0467 INV-6: a mode fired shot after shot loops from its first shot of a hold", "[ugame][game][cues]") {
    const BodyWorld world = arena();
    const WeaponSet set = loopingSet();
    // The pulse gun's primary: a projectile mode with no wind-up and no beam.
    const FireMode& plasma = primaryOf(set, "pulse-gun");
    REQUIRE(plasma.archetype == Archetype::Projectile);
    REQUIRE(plasma.windUp == 0);
    REQUIRE(plasma.ammo > 0);

    Game game = duel(set, "pulse-gun", 600);
    CHECK(loopingMode(set, game.bodies[0]) == -1);
    hold(world, set, game, 1);
    REQUIRE(cuesOf(game, Cue::Kind::Fire) == 1);
    CHECK(loopingMode(set, game.bodies[0]) == 0);
    hold(world, set, game, 30);
    CHECK(loopingMode(set, game.bodies[0]) == 0);

    // The same hold on an empty gun never fired, so it never sounds.
    Game empty = duel(set, "pulse-gun", 600);
    empty.bodies[0].weapons[static_cast<std::size_t>(empty.bodies[0].weapon)].ammo = 0;
    hold(world, set, empty, 30);
    CHECK(loopingMode(set, empty.bodies[0]) == -1);

    // Ammo running out under a held trigger stops it.
    Game spent = game;
    spent.bodies[0].weapons[static_cast<std::size_t>(spent.bodies[0].weapon)].ammo = 0;
    CHECK(loopingMode(set, spent.bodies[0]) == -1);

    // A switch asked for with the trigger down: the gun in hand is still
    // the pulse gun and its burst still counts, and it is silent.
    Game switching = game;
    std::vector<MoveCommand> commands(2);
    commands[0].fire = true;
    commands[0].weaponPick = static_cast<std::int8_t>(weaponNamed(set, "rocket-launcher"));
    tick(world, set, RULES, switching, commands, DT);
    REQUIRE(switching.bodies[0].pending >= 0);
    REQUIRE(switching.bodies[0].weapon == weaponNamed(set, "pulse-gun"));
    CHECK(loopingMode(set, switching.bodies[0]) == -1);

    // The trigger up.
    std::vector<MoveCommand> nothing(2);
    tick(world, set, RULES, game, nothing, DT);
    CHECK(loopingMode(set, game.bodies[0]) == -1);

    // A tap, then the trigger down again while the gun still cools: held,
    // loaded, and not yet fired in this hold. Silent until its shot.
    REQUIRE(plasma.refire > 3 * DT);
    Game again = duel(set, "pulse-gun", 600);
    hold(world, set, again, 1);
    tick(world, set, RULES, again, nothing, DT);
    hold(world, set, again, 1);
    REQUIRE(cuesOf(again, Cue::Kind::Fire) == 0);
    REQUIRE(again.bodies[0].fireHeld);
    CHECK(loopingMode(set, again.bodies[0]) == -1);

    // The other trigger held: the alt's beam, not the primary's loop.
    Game alt = duel(set, "pulse-gun", 600);
    hold(world, set, alt, 5, true);
    CHECK(loopingMode(set, alt.bodies[0]) == 1);
}

namespace {

/// A lone body dropped from `height` above the arena's floor and ticked
/// until it lands: the vertical velocity it landed with.
double dropped(const BodyWorld& world, const MoveRules& rules, Game& game, double height) {
    game.bodies.push_back(bodyAt(stock(), {0, 0, STANDING + height}, 0));
    game.bodies[0].move.physics = uta::uworld::Physics::Falling;
    const std::vector<MoveCommand> idle(1);
    for (int k = 0; k < 1000; ++k) {
        tick(world, stock(), rules, game, idle, DT);
        if (game.bodies[0].move.landedZ != 0) return game.bodies[0].move.landedZ;
    }
    FAIL("the body never landed");
    return 0;
}

} // namespace

TEST_CASE("UTA-0108: a landing costs what Pawn.TakeFallingDamage takes", "[ugame][game]") {
    const double jumpZ = 357.5; // so nothing down to 1107.5, and death past 2007.5
    CHECK(landingDamage(0, jumpZ) == 0);
    CHECK(landingDamage(300, jumpZ) == 0);
    CHECK(landingDamage(-1107.4, jumpZ) == 0);
    CHECK(landingDamage(-1107.5, jumpZ) == 7); // 0.15 of 50, whole
    CHECK(landingDamage(-2007.5, jumpZ) == 142);
    CHECK(landingDamage(-2007.6, jumpZ) == LANDING_DEATH_DAMAGE);
    CHECK(landingDamage(-1000, 200) == 15); // the thresholds move with the jump
}

TEST_CASE("UTA-0108: a hard landing hurts once, and not at all with the rule off", "[ugame][game]") {
    const BodyWorld world = arena();
    Game hurt;
    const double landedZ = dropped(world, RULES, hurt, 1000);
    const double cost = landingDamage(landedZ, RULES.jumpZ);
    REQUIRE(cost > 0);
    REQUIRE(cost < START_HEALTH);
    CHECK(hurt.bodies[0].health == START_HEALTH - cost);
    const std::vector<MoveCommand> idle(1);
    tick(world, stock(), RULES, hurt, idle, DT);
    CHECK(hurt.bodies[0].health == START_HEALTH - cost);

    MoveRules spared = RULES;
    spared.landingDamage = false;
    Game unhurt;
    CHECK(dropped(world, spared, unhurt, 1000) == landedZ);
    CHECK(unhurt.bodies[0].health == START_HEALTH);
}

TEST_CASE("UTA-0108: a very hard landing kills, and the rule off spares it", "[ugame][game]") {
    const BodyWorld world = arena();
    Game game;
    REQUIRE(dropped(world, RULES, game, 2600) < -1650 - RULES.jumpZ);
    CHECK_FALSE(game.bodies[0].alive());

    MoveRules spared = RULES;
    spared.landingDamage = false;
    Game other;
    (void)dropped(world, spared, other, 2600);
    CHECK(other.bodies[0].health == START_HEALTH);
}

TEST_CASE("UTA-0108: a soft landing costs nothing", "[ugame][game]") {
    const BodyWorld world = arena();
    Game game;
    REQUIRE(dropped(world, RULES, game, 300) < 0);
    CHECK(game.bodies[0].health == START_HEALTH);
}

namespace {

using uta::umap::INDEX_NONE;
using uta::umap::NO_ROOM;
using uta::umap::RoomMap;
using uta::ubundle::Zone;
using uta::uworld::ZoneWorld;

/// UTA-0493: one plane at height `z`; above it zone `above`, below it zone
/// `below`. Zones 1 and 2 are rooms 0 and 1; zone 0 is the null zone.
RoomMap splitAt(double z, std::uint8_t above, std::uint8_t below) {
    RoomMap map;
    RoomMap::Node node;
    node.normal = uta::umap::Point3{0, 0, 1};
    node.w = static_cast<float>(z);
    node.iZone[1] = above;
    node.iZone[0] = below;
    map.nodes.push_back(node);
    map.roomForZone = {NO_ROOM, 0, 1};
    map.rooms.resize(2);
    map.rooms[0].zoneIndex = 1;
    map.rooms[1].zoneIndex = 2;
    return map;
}

/// Zone 1 plain, zone 2 a pain zone of `damagePerSec`.
std::vector<Zone> painIn2(std::int32_t damagePerSec) {
    std::vector<Zone> zones(3);
    zones[2].pain = 1;
    zones[2].damagePerSec = damagePerSec;
    return zones;
}

/// A body standing on the arena's floor, landing damage off, so no other rule
/// can change its health.
Game standing() {
    Game game;
    game.bodies.push_back(bodyAt(stock(), {0, 0, STANDING}, 0));
    return game;
}

MoveRules noLanding() {
    MoveRules rules = RULES;
    rules.landingDamage = false;
    return rules;
}

} // namespace

TEST_CASE("UTA-0493 INV-4: a body outside the level dies with landing damage off", "[ugame][game]") {
    // The body stands still and never lands, so only the out-of-world rule
    // can take its health. Everything below z = 1000 is the null zone.
    const BodyWorld world = arena();
    const RoomMap rooms = splitAt(1000, 1, 0);
    const std::vector<Zone> zones(3);
    const ZoneWorld lookup(rooms, zones);
    Game game = standing();
    const std::vector<MoveCommand> idle(1);
    tick(world, stock(), noLanding(), game, idle, DT, &lookup);
    CHECK(game.bodies[0].move.landedZ == 0);
    CHECK(game.bodies[0].health == OUT_OF_WORLD_HEALTH);
    CHECK_FALSE(game.bodies[0].alive());
}

TEST_CASE("UTA-0493 INV-5: a lookup that failed kills nobody, and no lookup changes nothing", "[ugame][game]") {
    const BodyWorld world = arena();
    RoomMap rooms = splitAt(1000, 1, 0);
    rooms.rooms.clear(); // zone 0 on a map with no rooms is Unknown
    const std::vector<Zone> zones(3);
    const ZoneWorld lookup(rooms, zones);
    const std::vector<MoveCommand> idle(1);
    Game unknown = standing();
    Game none = standing();
    for (int k = 0; k < 10; ++k) {
        tick(world, stock(), noLanding(), unknown, idle, DT, &lookup);
        tick(world, stock(), noLanding(), none, idle, DT);
    }
    CHECK(unknown.bodies[0].health == START_HEALTH);
    CHECK(none.bodies[0].health == START_HEALTH);
}

namespace {

/// Ticks `game` until body 0's health moves, at most `limit` ticks: how many it took.
int ticksToHit(const BodyWorld& world, const MoveRules& rules, Game& game, const ZoneWorld& lookup, int limit) {
    const std::vector<MoveCommand> idle(1);
    const double before = game.bodies[0].health;
    for (int k = 1; k <= limit; ++k) {
        tick(world, stock(), rules, game, idle, DT, &lookup);
        if (game.bodies[0].health != before) return k;
    }
    return -1;
}

} // namespace

TEST_CASE("UTA-0493 INV-6: a pain zone hurts on PainTimer's clock and depth", "[ugame][game]") {
    const BodyWorld world = arena();
    const int second = static_cast<int>(std::ceil(PAIN_WAIT / DT));
    const std::vector<Zone> zones = painIn2(40);

    SECTION("feet, centre and head in it: 40 on the tick after entering, then 40 a second on") {
        const RoomMap rooms = splitAt(1000, 1, 2); // the body stands wholly below
        const ZoneWorld lookup(rooms, zones);
        for (const bool landing : {true, false}) {
            MoveRules rules = RULES;
            rules.landingDamage = landing;
            Game game = standing();
            CHECK(ticksToHit(world, rules, game, lookup, 100) == 2);
            CHECK(game.bodies[0].health == START_HEALTH - 40);
            const int next = ticksToHit(world, rules, game, lookup, 1000);
            CHECK((next == second || next == second + 1)); // a second of ticks, give or take rounding
            CHECK(next * DT >= PAIN_WAIT);
            CHECK(game.bodies[0].health == START_HEALTH - 80);
        }
    }

    SECTION("feet alone in it: 16 each time") {
        // The plane sits between the feet and the centre.
        const RoomMap rooms = splitAt(STANDING / 2, 1, 2);
        const ZoneWorld lookup(rooms, zones);
        Game game = standing();
        CHECK(ticksToHit(world, noLanding(), game, lookup, 100) == 2);
        CHECK(game.bodies[0].health == START_HEALTH - 16);
    }

    SECTION("leaving it stops the clock") {
        const RoomMap rooms = splitAt(1000, 1, 2);
        const RoomMap clear = splitAt(-1000, 1, 2); // the body stands wholly above
        const ZoneWorld in(rooms, zones);
        const ZoneWorld out(clear, zones);
        Game game = standing();
        const std::vector<MoveCommand> idle(1);
        tick(world, stock(), noLanding(), game, idle, DT, &in);
        CHECK(game.bodies[0].painClock == PAIN_FIRST_WAIT);
        tick(world, stock(), noLanding(), game, idle, DT, &out);
        CHECK(game.bodies[0].painClock < 0);
        CHECK(game.bodies[0].health == START_HEALTH);
    }
}

TEST_CASE("UTA-0493 INV-7: a zone of negative damage heals, never past full health", "[ugame][game]") {
    const BodyWorld world = arena();
    const RoomMap rooms = splitAt(1000, 1, 2);
    const std::vector<Zone> zones = painIn2(-10);
    const ZoneWorld lookup(rooms, zones);
    Game game = standing();
    game.bodies[0].health = 50;
    CHECK(ticksToHit(world, noLanding(), game, lookup, 100) == 2);
    CHECK(game.bodies[0].health == 60);

    Game nearly = standing();
    nearly.bodies[0].health = 95;
    CHECK(ticksToHit(world, noLanding(), nearly, lookup, 100) == 2);
    CHECK(nearly.bodies[0].health == START_HEALTH);
}
