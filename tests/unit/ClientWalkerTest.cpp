// UTA-0017 SS 4.8: ut-ants' walking mode. A walker dropped above a floor
// lands with its eye EYE_HEIGHT above a standing body's centre, and the keys
// move it the way it looks.

#include "BlockFixture.h"

#include "ut-ants/Walker.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using uta::client::FlyInput;
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

    FlyInput forward;
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
    FlyInput forward;
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
