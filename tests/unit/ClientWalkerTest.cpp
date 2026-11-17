// UTA-0017 SS 4.8: ut-ants' walking mode. A walker dropped above a floor
// lands with its eye EYE_HEIGHT above a standing body's centre, and the keys
// move it the way it looks. UTA-0020 INV-8: the weapon wheel in a walker.

#include "BlockFixture.h"

#include "ut-ants/Walker.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
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
