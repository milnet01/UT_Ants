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
    const double fall = RULES.gravityZ * set.projectiles[shot.kind].gravity;
    was.location = shot.location - shot.velocity * DT;
    was.velocity = shot.velocity - (Vec3{0, 0, fall} + shot.acceleration) * DT;
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
}

TEST_CASE("SS 4.9: the launcher spends a round a loading step and fires itself at six", "[ugame][game]") {
    const BodyWorld world = arena();
    Game game = duel(stock(), "rocket-launcher", 1900);
    const auto ammo = [&game] { return game.bodies[0].weapons[static_cast<std::size_t>(game.bodies[0].weapon)].ammo; };
    const std::uint32_t full = ammo();
    // A step is 0.88 s, 68 ticks to the hair; the counts bracket it.
    hold(world, stock(), game, 1);
    CHECK(ammo() == full - 1); // the press loads the first
    hold(world, stock(), game, 66);
    CHECK(ammo() == full - 1);
    hold(world, stock(), game, 5);
    CHECK(ammo() == full - 2);
    CHECK(game.shots.empty());
    for (int k = 0; k < 400 && game.shots.empty(); ++k) hold(world, stock(), game, 1);
    CHECK(game.shots.size() == 6);
    CHECK(ammo() == full - 6);
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
        edited.weapons[static_cast<std::size_t>(weaponNamed(edited, "minigun"))].primary.refire *= 2;
        Game a = duel(stock(), "minigun", 600), b = duel(edited, "minigun", 600);
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
    CHECK(std::abs(altBlastAt(world, met, "shock-rifle").x - contact) < 1e-6);
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
    CHECK(reference.back() == 17954599428131284997u);
}
