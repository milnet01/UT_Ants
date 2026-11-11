// Locks docs/specs/UTA-0019-weapon-set.md INV-4 (behaviour comes from the
// file), INV-5 (the tick is deterministic), INV-7 (ammo is never spent below
// zero), INV-8 (a combo needs a hit) and INV-9 (a body is hit where its
// cylinder is), on BlockFixture's hand-built levels.

#include "BlockFixture.h"

#include "ugame/Game.h"
#include "uworld/FixedTick.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
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
        game.bodies[1].move.location.y = offset; // the shot passes `offset` from its axis
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
        game.shots.push_back(Shot{.kind = ball, .owner = 0,
                                  .location = {800, offset, STANDING + EYE_HEIGHT}, .velocity = {}});
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

TEST_CASE("INV-4: one mode's damage, refire or speed changes only that", "[ugame][game]") {
    const BodyWorld world = arena();

    SECTION("damage") {
        WeaponSet edited = stock();
        edited.weapons[static_cast<std::size_t>(weaponNamed(edited, "shock-rifle"))].primary.damage += 7;
        Game a = duel(stock(), "shock-rifle", 600), b = duel(edited, "shock-rifle", 600);
        hold(world, stock(), a, 1);
        hold(world, edited, b, 1);
        CHECK(1e9 - b.bodies[1].health == 1e9 - a.bodies[1].health + 7);
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
    // The first tick starts the charge; the next finds the floor in reach
    // and goes off.
    tick(world, stock(), RULES, game, commands, DT);
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
    CHECK(reference.back() == 15636726512813428711u);
}
