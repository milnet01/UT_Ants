// UTA-0019 SS 4.8: ut-ants' walking mode carries the weapon set. The walker
// fires the stock weapons at dummies it drops, zooms a weapon with no alt
// fire, and comes back at a spawn when it kills itself; StandIns turns the
// game into the frame's shapes and the overlay's lines.
//
// NO TEST NAME CONTAINS A COMMA -- BakeCliTest.cpp says why.

#include "BlockFixture.h"

#include "ut-ants/StandIns.h"
#include "ut-ants/Walker.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using uta::client::StandIns;
using uta::client::Walker;
using uta::test::blocks::box;
using uta::test::blocks::solidBlocks;
using uta::ugame::Event;
using uta::uinput::Action;
using uta::uinput::Frame;
using uta::urender::Shape;

namespace {

constexpr double FRAME = 1.0 / 60;

const uta::ugame::WeaponSet& stock() {
    static const uta::ugame::WeaponSet set =
        uta::ugame::load(uta::ugame::shippedDirectory() / "stock.weapons").value();
    return set;
}

/// A wide floor at z = 0, and a wall across y = `wallAt` where it is above 0.
uta::uworld::BodyWorld room(double wallAt = 0) {
    uta::ubundle::Collision collision;
    std::vector<uta::test::blocks::Block> blocks{box({-4000, -4000, -64}, {4000, 4000, 0})};
    if (wallAt > 0) blocks.push_back(box({-4000, wallAt, 0}, {4000, wallAt + 64, 400}));
    collision.level = solidBlocks(blocks);
    return uta::uworld::BodyWorld::build(collision, {}).value();
}

Frame pressing(Action action) {
    Frame frame;
    frame.pressed.set(static_cast<std::size_t>(action));
    return frame;
}

void run(Walker& walker, const uta::uworld::BodyWorld& world, double seconds, const Frame& frame = {}) {
    for (int i = 0; i < static_cast<int>(std::lround(seconds / FRAME)); ++i) walker.update(frame, FRAME, world);
}

/// A walker with the stock set, landed at the origin and facing +Y.
Walker landed(const uta::uworld::BodyWorld& world, std::vector<Walker::Spawn> spawns = {}) {
    Walker walker({0, 0, 100}, 0, 16384, {}, stock(), std::move(spawns));
    run(walker, world, 2);
    REQUIRE(walker.state().physics == uta::uworld::Physics::Walking);
    return walker;
}

std::string inHand(const Walker& walker) {
    return walker.body().weapon < 0 ? std::string()
                                    : walker.set().weapons[static_cast<std::size_t>(walker.body().weapon)].name;
}

std::size_t countOf(const std::vector<Shape>& shapes, Shape::Kind kind) {
    return static_cast<std::size_t>(
        std::count_if(shapes.begin(), shapes.end(), [kind](const Shape& shape) { return shape.kind == kind; }));
}

} // namespace

TEST_CASE("UTA-0019 SS 4.8: the walker carries every weapon with its most ammo", "[client][weapons]") {
    const auto world = room();
    const Walker walker = landed(world);
    REQUIRE(walker.body().weapons.size() == stock().weapons.size());
    for (std::size_t i = 0; i < stock().weapons.size(); ++i) {
        CHECK(walker.body().weapons[i].has);
        CHECK(walker.body().weapons[i].ammo == stock().weapons[i].ammoMax);
    }
    CHECK(walker.body().health == uta::ugame::START_HEALTH);
}

TEST_CASE("UTA-0019 SS 4.8: a key stands a dummy ahead and a shot hurts it", "[client][weapons]") {
    const auto world = room();
    Walker walker = landed(world);
    walker.update(pressing(Action::DropDummy), FRAME, world);
    run(walker, world, 1);
    REQUIRE(walker.game().bodies.size() == 2);
    const uta::ugame::Body& dummy = walker.game().bodies[1];
    CHECK(std::abs(dummy.move.location.x) < 0.01);
    CHECK(std::abs(dummy.move.location.y - Walker::DUMMY_AHEAD) < 1);
    CHECK(dummy.move.physics == uta::uworld::Physics::Walking); // it fell to the floor
    CHECK(dummy.health == uta::ugame::START_HEALTH);
    CHECK_FALSE(walker.lastHit().has_value());

    walker.update(pressing(Action::Weapon4), FRAME, world);
    run(walker, world, 2);
    REQUIRE(inHand(walker) == "shock-rifle");
    const auto shock = static_cast<std::size_t>(walker.body().weapon);

    Frame fire;
    fire.fire = true;
    bool drewALine = false;
    for (int i = 0; i < 6 && !walker.lastHit(); ++i) {
        walker.update(fire, FRAME, world);
        for (const Event& event : walker.events()) drewALine = drewALine || event.kind == Event::Kind::InstantHit;
    }
    REQUIRE(walker.lastHit().has_value());
    CHECK(drewALine);
    CHECK(walker.lastHit()->damage > 0);
    CHECK(walker.lastHit()->healthLeft == uta::ugame::START_HEALTH - walker.lastHit()->damage);
    CHECK(walker.game().bodies[1].health == walker.lastHit()->healthLeft);
    CHECK(walker.body().weapons[shock].ammo < stock().weapons[shock].ammoMax);
    CHECK(walker.body().health == uta::ugame::START_HEALTH); // an instant hit does not come back
}

TEST_CASE("UTA-0019 SS 4.8: a dummy killed is gone and its place is used again", "[client][weapons]") {
    const auto world = room();
    Walker walker = landed(world);
    walker.update(pressing(Action::DropDummy), FRAME, world);
    walker.update(pressing(Action::Weapon4), FRAME, world);
    run(walker, world, 2);
    StandIns standIns;
    REQUIRE(countOf(standIns.shapes(walker.game(), walker.set()), Shape::Kind::Cylinder) == 1);

    Frame fire;
    fire.fire = true;
    for (int i = 0; i < 600 && walker.game().bodies[1].alive(); ++i) walker.update(fire, FRAME, world);
    REQUIRE_FALSE(walker.game().bodies[1].alive());
    CHECK(walker.lastHit()->healthLeft <= 0);
    CHECK(countOf(standIns.shapes(walker.game(), walker.set()), Shape::Kind::Cylinder) == 0);

    walker.update(pressing(Action::DropDummy), FRAME, world);
    CHECK(walker.game().bodies.size() == 2);
    CHECK(walker.game().bodies[1].alive());
    CHECK(walker.game().bodies[1].health == uta::ugame::START_HEALTH);
}

TEST_CASE("UTA-0019 SS 4.8: no dummy is stood where a wall leaves no room", "[client][weapons]") {
    // The wall's face is 40 ahead of a body 17 wide: under three radii clear.
    const auto world = room(40 + 17);
    Walker walker = landed(world);
    walker.update(pressing(Action::DropDummy), FRAME, world);
    CHECK(walker.game().bodies.size() == 1);

    // Farther off, the dummy stands short of the wall rather than in it.
    const auto wide = room(150);
    Walker other = landed(wide);
    other.update(pressing(Action::DropDummy), FRAME, wide);
    REQUIRE(other.game().bodies.size() == 2);
    CHECK(other.game().bodies[1].move.location.y < 150 - 17);
    CHECK(other.game().bodies[1].move.location.y > 100);
}

TEST_CASE("UTA-0019 SS 4.8: a dummy is stood on ground that rises ahead", "[client][weapons]") {
    // A 12-unit step up from y = 60 on, as uneven ground has: a body pushed
    // along the floor would stop at it, with no room for a dummy.
    uta::ubundle::Collision collision;
    collision.level = solidBlocks(
        {box({-4000, -4000, -64}, {4000, 4000, 0}), box({-4000, 60, 0}, {4000, 4000, 12})});
    const auto world = uta::uworld::BodyWorld::build(collision, {}).value();
    Walker walker = landed(world);
    walker.update(pressing(Action::DropDummy), FRAME, world);
    run(walker, world, 1);
    REQUIRE(walker.game().bodies.size() == 2);
    const uta::ugame::Body& dummy = walker.game().bodies[1];
    CHECK(std::abs(dummy.move.location.y - Walker::DUMMY_AHEAD) < 1);
    CHECK(dummy.move.physics == uta::uworld::Physics::Walking);
    CHECK(std::abs(dummy.move.location.z - (12 + dummy.size.halfHeight)) < 1); // standing on the step
}

TEST_CASE("UTA-0019 SS 4.8: dummies past the most that stand at once are refused", "[client][weapons]") {
    const auto world = room();
    Walker walker = landed(world);
    for (std::size_t i = 0; i < Walker::MAX_DUMMIES + 5; ++i) walker.update(pressing(Action::DropDummy), FRAME, world);
    CHECK(walker.game().bodies.size() == 1 + Walker::MAX_DUMMIES);
}

TEST_CASE("UTA-0019 SS 4.8: a trigger pulled and let go between two ticks still fires", "[client][weapons]") {
    const auto world = room();
    Walker walker = landed(world);
    walker.update(pressing(Action::DropDummy), FRAME, world);
    walker.update(pressing(Action::Weapon4), FRAME, world);
    run(walker, world, 2);
    Frame fire;
    fire.fire = true;
    walker.update(fire, 0.0001, world); // too short for a tick
    CHECK_FALSE(walker.lastHit().has_value());
    run(walker, world, 0.1); // let go
    CHECK(walker.lastHit().has_value());
}

TEST_CASE("UTA-0019 SS 4.8: a weapon key pressed in a frame that runs no tick still switches", "[client][weapons]") {
    const auto world = room();
    Walker walker = landed(world);
    const std::string before = inHand(walker);
    walker.update(pressing(Action::Weapon0), 0.0001, world); // too short for a tick
    CHECK(inHand(walker) == before);
    run(walker, world, 2);
    CHECK(inHand(walker) == "sniper-rifle");

    walker.update(pressing(Action::PrevWeapon), FRAME, world);
    run(walker, world, 2);
    CHECK(inHand(walker) == "rocket-launcher");
    walker.update(pressing(Action::NextWeapon), FRAME, world);
    run(walker, world, 2);
    CHECK(inHand(walker) == "sniper-rifle");
}

TEST_CASE("UTA-0019 SS 4.8: alt fire zooms a weapon that has no alt fire", "[client][weapons]") {
    const auto world = room();
    Walker walker = landed(world);
    walker.update(pressing(Action::Weapon0), FRAME, world);
    run(walker, world, 2);
    REQUIRE(inHand(walker) == "sniper-rifle");
    const float wide = walker.view(90, 1920, 1080).verticalFovDegrees;
    CHECK(walker.zoom() == 1);

    Frame alt;
    alt.altFire = true;
    run(walker, world, 0.5, alt);
    const double held = walker.zoom();
    CHECK(std::abs(held - (1 - 0.5 * Walker::ZOOM_NARROWS)) < 0.02);
    CHECK(walker.view(90, 1920, 1080).verticalFovDegrees < wide * 0.6f);
    run(walker, world, 0.5); // released: the view holds
    CHECK(walker.zoom() == held);
    run(walker, world, 3, alt); // pressed again: the zoom ends, and holding does not start another
    CHECK(walker.zoom() == 1);
    run(walker, world, 0.1);
    run(walker, world, 3, alt); // and again: all the way in, and no further
    CHECK(std::abs(walker.zoom() - (1 - Walker::ZOOM_SECONDS * Walker::ZOOM_NARROWS)) < 1e-9);

    // A zoomed view turns as much slower as it is narrower.
    const auto yawBefore = walker.view(90, 1920, 1080).rotation[1];
    Frame turn = alt;
    turn.lookRight = 1000;
    walker.update(turn, FRAME, world);
    CHECK(std::abs((walker.view(90, 1920, 1080).rotation[1] - yawBefore) - 1000 * walker.zoom()) <= 1);

    // Another weapon in hand: no zoom, and its alt fire is its own.
    walker.update(pressing(Action::Weapon9), FRAME, world);
    run(walker, world, 2);
    REQUIRE(inHand(walker) == "rocket-launcher");
    CHECK(walker.zoom() == 1);
    run(walker, world, 0.5, alt);
    CHECK(walker.zoom() == 1);
}

TEST_CASE("UTA-0019 SS 4.5: a player who kills itself comes back at a spawn with the full set",
          "[client][weapons]") {
    const auto world = room();
    Walker walker = landed(world, {{{1000, 1000, 100}, 0}});
    walker.update(pressing(Action::Weapon9), FRAME, world);
    run(walker, world, 2);
    REQUIRE(inHand(walker) == "rocket-launcher");

    // Rockets at its own feet until it dies: nothing else moves it, so a
    // body at the spawn is one that came back.
    Frame down;
    down.lookUp = -16000;
    walker.update(down, FRAME, world);
    Frame fire;
    fire.fire = true;
    int frames = 0;
    for (; frames < 3600 && walker.state().location.x < 900; ++frames)
        walker.update(frames % 120 < 100 ? fire : Frame{}, FRAME, world);
    REQUIRE(frames < 3600);
    CHECK(walker.body().health == uta::ugame::START_HEALTH);
    CHECK(std::abs(walker.state().location.y - 1000) < 1);
    CHECK(walker.view(90, 1920, 1080).rotation[0] == 0); // looking level again
    const auto rockets = static_cast<std::size_t>(std::find_if(stock().weapons.begin(), stock().weapons.end(),
                                                               [](const auto& w) { return w.name == "rocket-launcher"; }) -
                                                  stock().weapons.begin());
    CHECK(walker.body().weapons[rockets].ammo == stock().weapons[rockets].ammoMax);
}

TEST_CASE("UTA-0019 SS 4.8: a shot is a sphere and a hit a line that fades and goes", "[client][weapons]") {
    uta::ugame::Game game;
    game.bodies.resize(1); // the player's own body is not drawn
    // The smallest kind in the set, which is too small to see at its own size.
    const auto smallest = std::min_element(stock().projectiles.begin(), stock().projectiles.end(),
                                           [](const auto& a, const auto& b) { return a.radius < b.radius; });
    REQUIRE(smallest->radius < 3);
    uta::ugame::Shot shot;
    shot.kind = static_cast<std::uint32_t>(smallest - stock().projectiles.begin());
    shot.location = {10, 20, 30};
    game.shots.push_back(shot);

    StandIns standIns;
    std::vector<Shape> shapes = standIns.shapes(game, stock());
    REQUIRE(shapes.size() == 1);
    CHECK(shapes[0].kind == Shape::Kind::Sphere);
    CHECK(shapes[0].a == std::array<float, 3>{10, 20, 30});
    CHECK(shapes[0].radius == 3);
    game.shots.clear();

    const std::vector<Event> events{{Event::Kind::InstantHit, {0, 0, 0}, {100, 0, 0}, 0},
                                    {Event::Kind::Explosion, {5, 6, 7}, {}, 200}};
    standIns.advance(FRAME, events);
    shapes = standIns.shapes(game, stock());
    REQUIRE(shapes.size() == 2); // each new one is drawn, whatever the frame took
    const auto line = std::find_if(shapes.begin(), shapes.end(), [](const Shape& s) { return s.kind == Shape::Kind::Line; });
    const auto blast = std::find_if(shapes.begin(), shapes.end(), [](const Shape& s) { return s.kind == Shape::Kind::Sphere; });
    REQUIRE(line != shapes.end());
    REQUIRE(blast != shapes.end());
    CHECK(line->b == std::array<float, 3>{100, 0, 0});
    CHECK(line->colour[3] == 1);
    CHECK(blast->a == std::array<float, 3>{5, 6, 7});
    const float firstRadius = blast->radius, firstAlpha = blast->colour[3];
    CHECK(firstRadius < 200);

    standIns.advance(StandIns::LINE_SECONDS * 0.5, {});
    shapes = standIns.shapes(game, stock());
    REQUIRE(shapes.size() == 2);
    for (const Shape& s : shapes) {
        if (s.kind == Shape::Kind::Line) CHECK(s.colour[3] < 1);
        if (s.kind == Shape::Kind::Sphere) {
            CHECK(s.radius > firstRadius); // growing
            CHECK(s.radius <= 200);
            CHECK(s.colour[3] < firstAlpha); // and fading
        }
    }
    standIns.advance(StandIns::LINE_SECONDS * 0.6, {});
    shapes = standIns.shapes(game, stock());
    REQUIRE(shapes.size() == 1); // the line has gone
    CHECK(shapes[0].kind == Shape::Kind::Sphere);
    standIns.advance(StandIns::BLAST_SECONDS, {});
    CHECK(standIns.shapes(game, stock()).empty());
}

TEST_CASE("UTA-0019 SS 4.8: the overlay names the weapon and its ammo and the health", "[client][weapons]") {
    uta::ugame::Body body = uta::ugame::bodyAt(stock(), {0, 0, 0}, 0);
    uta::ugame::giveAll(stock(), body);
    // The first weapon holds no ammo, so its line is left out.
    REQUIRE(stock().weapons[static_cast<std::size_t>(body.weapon)].ammoMax == 0);
    CHECK(uta::client::hudText(body, stock(), {}) ==
          stock().weapons[static_cast<std::size_t>(body.weapon)].name + "\nHEALTH 100");
    const auto held = static_cast<std::size_t>(
        std::find_if(stock().weapons.begin(), stock().weapons.end(), [](const auto& w) { return w.ammoMax > 0; }) -
        stock().weapons.begin());
    body.weapon = static_cast<int>(held);
    const std::string expected = stock().weapons[held].name + "\nAMMO " +
                                 std::to_string(stock().weapons[held].ammoMax) + "\nHEALTH 100";
    CHECK(uta::client::hudText(body, stock(), {}) == expected);
    CHECK(uta::client::hudText(body, stock(), Walker::Hit{45, 55}) == expected + "\nHIT 45 TARGET 55");
    CHECK(uta::client::hudText(body, stock(), Walker::Hit{45, -3}) == expected + "\nHIT 45 TARGET DOWN");
    body.health = 37.9;
    body.weapon = -1;
    CHECK(uta::client::hudText(body, stock(), {}) == "HEALTH 37");
}

TEST_CASE("UTA-0472: a walker made where a body does not fit is lifted clear and walks", "[client][weapons]") {
    // The camera at a PlayerStart is a body's centre, so a body hung an eye's
    // height under it starts 27 into the floor, where no move is allowed.
    const auto world = room();
    const std::array<double, 3> sunk{0, 0, 12};
    const std::array<double, 3> clear = uta::client::clearOf(world, sunk);
    CHECK(clear[0] == 0);
    CHECK(clear[1] == 0);
    CHECK(clear[2] >= 39);
    CHECK(clear[2] < 39 + 7); // a step clear of the floor, and no higher
    CHECK(uta::client::clearOf(world, clear) == clear); // a place that fits is left alone

    Walker walker(clear, 0, 16384, {}, stock());
    run(walker, world, 1);
    REQUIRE(walker.state().physics == uta::uworld::Physics::Walking);
    Frame forward;
    forward.forward = 1;
    run(walker, world, 1, forward);
    CHECK(walker.state().location.y > 300);

    // With nowhere above that fits, the place is returned as given.
    uta::ubundle::Collision solid;
    solid.level = solidBlocks({box({-4000, -4000, -4000}, {4000, 4000, 4000})});
    const auto rock = uta::uworld::BodyWorld::build(solid, {}).value();
    CHECK(uta::client::clearOf(rock, sunk) == sunk);
}
