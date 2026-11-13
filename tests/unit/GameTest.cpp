// Locks docs/specs/UTA-0019-weapon-set.md INV-4 (behaviour comes from the
// file), INV-5 (the tick is deterministic), INV-7 (ammo is never spent below
// zero), INV-8 (a combo needs a hit) and INV-9 (a body is hit where its
// cylinder is), on BlockFixture's hand-built levels.

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
    REQUIRE(charge.stepLast == 0.84);
    // The press loads the first, and its tick counts. The rest load once
    // the hold passes 0.9, 1.8, 2.7, 3.6 and 4.44 s: a step's overshoot
    // counts toward the next (209, not 210), and the sixth takes 0.84.
    CHECK(spendTicks(world, game, 343, false) == std::vector{1, 70, 140, 209, 279});
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
/// 600 ahead, on its line.
Vec3 altBlastAt(const BodyWorld& world, const WeaponSet& set, std::string_view weapon) {
    Game game = duel(set, weapon, 600);
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
    // Where it met it is moved 0.4 of the body's radius on (Pawn.AdjustHitLocation).
    const double inset = 0.4 * uta::uworld::BodySize{}.radius;
    CHECK(std::abs(altBlastAt(world, met, "shock-rifle").x - contact - inset) < 1e-6);
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
        CHECK(was.location.z > 10); // 10 off the floor, plus the strike's back-off
        CHECK(was.location.z <= 10 + 0.5);
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

TEST_CASE("SS 4.9: the pulse beam stores its hold and hurts once past hit-every", "[ugame][game]") {
    const BodyWorld world = arena();
    const WeaponSet& set = stock();
    const FireMode& beam = *set.weapons[static_cast<std::size_t>(weaponNamed(set, "pulse-gun"))].alt;
    Game game = duel(set, "pulse-gun", 400);
    game.bodies[1].move.location.y = levelStart(beam).y;
    const Hits hits = hitsWhileHeld(world, set, game, 40, true);
    double held = 0;
    std::size_t first = 0;
    for (; first < hits.losses.size(); ++first) {
        held += DT;
        if (held > beam.hitEvery) break;
        CHECK(hits.losses[first] == 0);
    }
    REQUIRE(first < hits.losses.size());
    CHECK(hits.losses[first] == landed(set, beam.damage * held));
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
    CHECK(reference.back() == 12239779405718537928u);
}
