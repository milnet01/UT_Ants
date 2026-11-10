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
