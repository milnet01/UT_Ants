// UTA-0017 SS 4.8: ut-ants' walking mode. A walker dropped above a floor
// lands with its eye EYE_HEIGHT above a standing body's centre, and the keys
// move it the way it looks. UTA-0020 INV-8: the weapon wheel in a walker.

#include "BlockFixture.h"

#include "ut-ants/Walker.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <utility>
#include <vector>

using uta::uinput::Frame;
using uta::client::Walker;
using uta::test::blocks::box;
using uta::test::blocks::solidBlocks;

TEST_CASE("UTA-0017: a walker lands on the floor and walks the way it looks") {
    uta::ubundle::Collision collision;
    collision.level = solidBlocks({box({-2000, -2000, -64}, {2000, 2000, 0})});
    auto world = uta::uworld::BodyWorld::build(collision, {});
    REQUIRE(world.has_value());

    Walker walker({0, 0, 200}, 0, 16384); // facing +Y
    for (int frame = 0; frame < 120; ++frame) walker.update({}, 1.0 / 60, *world);
    REQUIRE(walker.state().physics == uta::uworld::Physics::Walking);
    const double standing = uta::uworld::BodySize{}.halfHeight + uta::uworld::SKIN;
    const auto eye = walker.eye(90).camera(1920, 1080);
    CHECK(std::abs(eye.location[2] - (standing + Walker::EYE_HEIGHT)) < 0.01);

    Frame forward;
    forward.forward = 1;
    for (int frame = 0; frame < 60; ++frame) walker.update(forward, 1.0 / 60, *world);
    CHECK(walker.state().location.y > 300); // a second at up to 400 a second
    CHECK(std::abs(walker.state().location.x) < 0.01);
}

TEST_CASE("UTA-0017 SS 4.8: the eye eases up a stair step instead of jumping with the body") {
    // A floor at z = 0 and a 20-unit step from y = 200 on.
    uta::ubundle::Collision collision;
    collision.level = solidBlocks(
        {box({-2000, -2000, -64}, {2000, 2000, 0}), box({-2000, 200, 0}, {2000, 2000, 20})});
    auto world = uta::uworld::BodyWorld::build(collision, {});
    REQUIRE(world.has_value());

    Walker walker({0, 0, 100}, 0, 16384); // facing +Y, towards the step
    for (int frame = 0; frame < 120; ++frame) walker.update({}, 1.0 / 85, *world);
    REQUIRE(walker.state().physics == uta::uworld::Physics::Walking);

    // A frame of 1/85 s runs one tick and leaves a blend of 0, so the view
    // shows the tick before the last.
    Frame forward;
    forward.forward = 1;
    double bodyBefore = walker.state().location.z;
    bool stepped = false;
    for (int frame = 0; frame < 120 && !stepped; ++frame) {
        bodyBefore = walker.state().location.z;
        walker.update(forward, 1.0 / 85, *world);
        stepped = walker.state().location.z > bodyBefore + 10;
    }
    REQUIRE(stepped);
    const double steppedZ = walker.state().location.z;
    const double rise = steppedZ - bodyBefore;
    walker.update({}, 1.0 / 85, *world); // the view now shows the step's tick
    const double eyeOverBody = walker.eye(90).camera(1920, 1080).location[2] - steppedZ;
    // UpdateEyeHeight keeps 1 - 10/85 of the step's lag on its first tick.
    CHECK(std::abs(eyeOverBody - (Walker::EYE_HEIGHT - rise * (1 - 10.0 / 85))) < 0.01);

    // A second later the eye is back at its height above the body.
    for (int frame = 0; frame < 85; ++frame) walker.update({}, 1.0 / 85, *world);
    const double settled = walker.eye(90).camera(1920, 1080).location[2] - walker.state().location.z;
    CHECK(std::abs(settled - Walker::EYE_HEIGHT) < 0.01);
}

TEST_CASE("UTA-0018: a Dodge press dodges the way the move points and needs a move") {
    uta::ubundle::Collision collision;
    collision.level = solidBlocks({box({-4000, -4000, -64}, {4000, 4000, 0})});
    auto world = uta::uworld::BodyWorld::build(collision, {});
    REQUIRE(world.has_value());
    Walker walker({0, 0, 100}, 0, 16384); // facing +Y
    for (int frame = 0; frame < 120; ++frame) walker.update({}, 1.0 / 60, *world);
    REQUIRE(walker.state().physics == uta::uworld::Physics::Walking);

    // Standing still, the press is dropped: no direction to dodge in.
    Frame still;
    still.pressed.set(static_cast<std::size_t>(uta::uinput::Action::Dodge));
    walker.update(still, 1.0 / 60, *world);
    CHECK(walker.state().dodge == uta::uworld::DodgeState::None);

    // Pushed mostly right, it dodges right: off the floor, faster than a run.
    Frame right = still;
    right.right = 1;
    right.forward = 0.3f;
    walker.update(right, 1.0 / 60, *world);
    CHECK(walker.state().dodge == uta::uworld::DodgeState::Active);
    // Facing +Y, its right is -X: a quarter turn of yaw further round.
    CHECK(walker.state().velocity.x < -500);
}

TEST_CASE("UTA-0020 INV-8: the open wheel holds the view and the triggers and a release picks on a tick") {
    uta::ubundle::Collision collision;
    collision.level = solidBlocks({box({-2000, -2000, -64}, {2000, 2000, 0})});
    auto world = uta::uworld::BodyWorld::build(collision, {});
    REQUIRE(world.has_value());
    auto set = uta::ugame::load(uta::ugame::shippedDirectory() / "stock.weapons");
    REQUIRE(set.has_value());
    Walker walker({0, 0, 100}, 0, 16384, {}, *set);
    for (int frame = 0; frame < 60; ++frame) walker.update({}, 1.0 / 60, *world);
    REQUIRE(walker.body().human);
    // The item a quarter turn round the ring, which the stick pushed right points at.
    const std::vector<int> ring = uta::ugame::wheelWeapons(walker.set(), walker.body());
    const int right = ring.at(static_cast<std::size_t>(std::lround(static_cast<double>(ring.size()) / 4)));
    REQUIRE(right != walker.body().weapon);

    // The right stick over to the right, which would also turn the view, and both triggers held.
    Frame pointing;
    pointing.wheel = true;
    pointing.pointRight = 1;
    pointing.lookRight = 3000;
    pointing.lookUp = 1000;
    pointing.fire = pointing.altFire = true;
    const std::uint64_t ticksBefore = walker.game().ticks;
    for (int frame = 0; frame < 30; ++frame) walker.update(pointing, 1.0 / 60, *world);
    // Half a second: about 42 ticks at full speed, and a fifth of that slowed
    // after the first frame, which runs at the last tick's scale.
    CHECK(walker.game().ticks - ticksBefore <= 12);
    CHECK(walker.wheel().isOpen());
    CHECK(walker.wheel().highlighted() == right);
    CHECK(walker.game().timeScale == uta::ugame::WHEEL_TIME_SCALE); // the ticks carried wheelOpen
    CHECK(walker.gameSeconds() == 1.0 / 60 * uta::ugame::WHEEL_TIME_SCALE);
    CHECK(walker.body().yaw == 16384);
    CHECK(walker.body().pitch == 0);
    CHECK_FALSE(walker.body().fireHeld);
    CHECK_FALSE(walker.body().altHeld);

    // Let go: the pick is asked for on the next tick, and time runs again.
    bool picked = false;
    for (int frame = 0; frame < 10 && !picked; ++frame) {
        walker.update({}, 1.0 / 60, *world);
        picked = walker.body().pending == right || walker.body().weapon == right;
    }
    CHECK(picked);
    CHECK_FALSE(walker.wheel().isOpen());
    CHECK(walker.game().timeScale == 1);
}

// docs/specs/UTA-0021-landing-marker.md INV-3, INV-5 and INV-6: the landing
// marker and the take-off pitch, on a walker running off a shelf 600 high.
namespace {

uta::uworld::BodyWorld shelfWorld() {
    uta::ubundle::Collision collision;
    collision.level = solidBlocks({box({-2000, -2000, -64}, {2000, 2000, 0}), box({-2000, -2000, 0}, {0, 2000, 600})});
    auto world = uta::uworld::BodyWorld::build(collision, {});
    REQUIRE(world.has_value());
    return std::move(*world);
}

/// A walker on the shelf, facing its edge (+X), standing.
Walker onTheShelf(const uta::uworld::BodyWorld& world, Walker::Assists assists) {
    auto set = uta::ugame::load(uta::ugame::shippedDirectory() / "stock.weapons");
    REQUIRE(set.has_value());
    Walker walker({-60, 0, 700}, 0, 0, {}, *set);
    walker.setAssists(assists);
    for (int frame = 0; frame < 60; ++frame) walker.update({}, 1.0 / 60, world);
    REQUIRE(walker.state().physics == uta::uworld::Physics::Walking);
    return walker;
}

/// Frame `n` of the run: forward, a jump on frame 3, fire held, a turn now and then.
Frame runFrame(int n) {
    Frame frame;
    frame.forward = 1;
    frame.up = n == 3 ? 1.0f : 0.0f;
    frame.fire = true;
    frame.lookRight = n % 20 == 0 ? 300 : 0;
    return frame;
}

} // namespace

TEST_CASE("UTA-0021 INV-3: the assists change no game state") {
    const auto world = shelfWorld();
    Walker on = onTheShelf(world, {});
    Walker off = onTheShelf(world, {.landingMarker = false, .takeOffPitch = false});
    REQUIRE(uta::ugame::stateHash(on.game()) == uta::ugame::stateHash(off.game()));
    double mostTip = 0;
    for (int n = 0; n < 170; ++n) {
        on.update(runFrame(n), 1.0 / 85, world);
        off.update(runFrame(n), 1.0 / 85, world);
        mostTip = std::max(mostTip, on.tip());
        REQUIRE(uta::ugame::stateHash(on.game()) == uta::ugame::stateHash(off.game()));
        // The aim each command carried: a shot that hits nothing leaves no trace in the hash.
        REQUIRE(on.body().pitch == off.body().pitch);
        REQUIRE(on.body().yaw == off.body().yaw);
    }
    CHECK(mostTip > 5); // the run did tip the view while it fired
}

TEST_CASE("UTA-0021 INV-5: the crosshair sits on the aim ray of a tipped view") {
    const auto world = shelfWorld();
    Walker walker = onTheShelf(world, {});
    walker.setViewShape(90, 1920.0 / 1080);
    int n = 0;
    while (walker.tip() < 5 && n < 170) walker.update(runFrame(n++), 1.0 / 85, world);
    REQUIRE(walker.tip() >= 5);
    CHECK(walker.body().pitch == 0); // the aim stays level

    const auto camera = walker.view(90, 1920, 1080);
    const double aim = walker.body().pitch * 2 * std::numbers::pi / 65536;
    const double drawn = camera.rotation[0] * 2 * std::numbers::pi / 65536;
    REQUIRE(drawn < aim);
    // The aim's direction in the drawn camera's frame, projected.
    const double ahead = std::cos(aim) * std::cos(drawn) + std::sin(aim) * std::sin(drawn);
    const double up = -std::cos(aim) * std::sin(drawn) + std::sin(aim) * std::cos(drawn);
    const double halfHeight = camera.verticalFovDegrees * std::numbers::pi / 360;
    CHECK(std::abs(walker.crosshairUp(90, 1920, 1080) - up / ahead / std::tan(halfHeight)) < 1e-6);
    CHECK(walker.crosshairUp(90, 1920, 1080) > 0.1);
}

TEST_CASE("UTA-0021 INV-6: each assist turns off alone") {
    const auto world = shelfWorld();
    Walker both = onTheShelf(world, {});
    Walker noMarker = onTheShelf(world, {.landingMarker = false, .takeOffPitch = true});
    Walker noPitch = onTheShelf(world, {.landingMarker = true, .takeOffPitch = false});
    bool landed = false, markerSeen = false, tipped = false;
    for (int n = 0; n < 170; ++n) {
        both.update(runFrame(n), 1.0 / 85, world);
        noMarker.update(runFrame(n), 1.0 / 85, world);
        noPitch.update(runFrame(n), 1.0 / 85, world);
        landed = landed || both.landing().has_value();
        CHECK_FALSE(noMarker.landing().has_value());
        CHECK(noPitch.tip() == 0);
        markerSeen = markerSeen || noPitch.landing().has_value();
        tipped = tipped || noMarker.tip() > 1;
    }
    CHECK(landed);
    CHECK(markerSeen); // the pitch off leaves the marker
    CHECK(tipped);     // the marker off leaves the pitch

    // Turned off mid-air, the view is straight at once, not eased back.
    Walker late = onTheShelf(world, {});
    int n = 0;
    while (late.tip() < 5 && n < 170) late.update(runFrame(n++), 1.0 / 85, world);
    REQUIRE(late.tip() >= 5);
    late.setAssists({.landingMarker = true, .takeOffPitch = false});
    late.update(runFrame(n), 1.0 / 85, world);
    CHECK(late.tip() == 0);
}

TEST_CASE("UTA-0022: the physical assists are off until set, each turning on its own rule", "[client]") {
    Walker walker({0, 0, 100}, 0, 0);
    const auto off = [](const uta::uworld::MoveRules& r) {
        return r.coyoteTime == 0 && r.mantleHeight == 0 && r.extraStepHeight == 0;
    };
    CHECK(off(walker.rules()));
    walker.setPhysicalAssists({.coyoteTime = true});
    CHECK(walker.rules().coyoteTime == uta::uworld::COYOTE_TIME_ON);
    CHECK(walker.rules().mantleHeight == 0);
    walker.setPhysicalAssists({.mantle = true});
    CHECK(walker.rules().coyoteTime == 0);
    CHECK(walker.rules().mantleHeight == uta::uworld::MANTLE_HEIGHT_ON);
    CHECK(walker.rules().extraStepHeight == 0);
    walker.setPhysicalAssists({.highStep = true});
    CHECK(walker.rules().extraStepHeight == uta::uworld::EXTRA_STEP_HEIGHT_ON);
    walker.setPhysicalAssists({});
    CHECK(off(walker.rules()));
    CHECK(walker.rules().airJumps == 1); // the double jump is not one of them
}

TEST_CASE("UTA-0493: a start is lifted clear, and one outside the level is never used") {
    // A floor whose top is z = 0; everything below z = -5 is the null zone.
    uta::ubundle::Collision collision;
    collision.level = solidBlocks({box({-2000, -2000, -64}, {2000, 2000, 0})});
    auto world = uta::uworld::BodyWorld::build(collision, {});
    REQUIRE(world.has_value());
    uta::umap::RoomMap rooms;
    uta::umap::RoomMap::Node node;
    node.normal = uta::umap::Point3{0, 0, 1};
    node.w = -5;
    node.iZone[1] = 1;
    node.iZone[0] = 0;
    rooms.nodes.push_back(node);
    rooms.roomForZone = {uta::umap::NO_ROOM, 0};
    rooms.rooms.resize(1);
    rooms.rooms[0].zoneIndex = 1;
    const std::vector<uta::ubundle::Zone> zones(2);
    const uta::uworld::ZoneWorld lookup(rooms, zones);

    // Sunk into the floor and below the plane: outside as placed, inside once
    // lifted, so only a check made after the lift keeps it.
    const std::vector<Walker::Spawn> starts{{{0, 0, -20}, 1}, {{0, 0, -1000}, 2}, {{100, 0, 100}, 3}};
    const std::vector<Walker::Spawn> kept = uta::client::standable(*world, &lookup, starts);
    REQUIRE(kept.size() == 2);
    CHECK(kept[0].yaw == 1);
    CHECK(kept[0].location[2] > uta::uworld::BodySize{}.halfHeight);
    CHECK(kept[1].yaw == 3);
    CHECK(kept[1].location[2] == 100);

    CHECK(uta::client::standable(*world, nullptr, starts).size() == 3);
}
