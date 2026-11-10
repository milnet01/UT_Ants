// UTA-0016: the client's free-flying camera, graded against urender's own view
// matrix rather than against a second statement of UT's axes. UTA-0158: its
// walls, over trees PathFixture builds in memory.
//
// NO TEST NAME CONTAINS A COMMA -- BakeCliTest.cpp says why.

#include "ut-ants/FlyCamera.h"

#include "PathFixture.h"
#include "urender/Placement.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <cstdint>

using Catch::Matchers::WithinAbs;
using uta::client::FlyCamera;
using uta::uinput::Frame;
using uta::urender::Camera;

namespace {

/// `point` in `camera`'s view space: +X right, +Y up, +Z ahead.
std::array<double, 3> inView(const Camera& camera, const std::array<float, 3>& point) {
    const uta::urender::gpu::Mat4 m = uta::urender::viewOf(camera);
    const std::array<double, 4> p{point[0], point[1], point[2], 1};
    std::array<double, 3> out{};
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 4; ++column) out[row] += m[column * 4 + row] * p[column];
    return out;
}

void checkNear(const std::array<double, 3>& actual, const std::array<double, 3>& expected) {
    for (std::size_t i = 0; i < 3; ++i) CHECK_THAT(actual[i], WithinAbs(expected[i], 0.05));
}

} // namespace

TEST_CASE("UTA-0016: flying forward moves straight ahead in urender's view", "[client]") {
    FlyCamera fly({100, 200, 300}, 4096, 16384); // tipped up, facing +Y
    const Camera before = fly.camera(1920, 1080);
    fly.update(Frame{.forward = 1}, 0.5);
    checkNear(inView(before, fly.camera(1920, 1080).location), {0, 0, FlyCamera::SPEED * 0.5});

    fly.update(Frame{.forward = -1}, 0.5);
    checkNear(inView(before, fly.camera(1920, 1080).location), {0, 0, 0});
}

TEST_CASE("UTA-0016: flying right moves to the right in urender's view", "[client]") {
    FlyCamera fly({-50, 25, 0}, 0, 40000);
    const Camera before = fly.camera(1920, 1080);
    fly.update(Frame{.right = 1}, 0.25);
    checkNear(inView(before, fly.camera(1920, 1080).location), {FlyCamera::SPEED * 0.25, 0, 0});
}

TEST_CASE("UTA-0016: rising follows the world's Z whatever the pitch", "[client]") {
    FlyCamera fly({0, 0, 0}, -9000, 1234);
    fly.update(Frame{.up = 1}, 1);
    const Camera after = fly.camera(1920, 1080);
    CHECK_THAT(after.location[0], WithinAbs(0, 1e-3));
    CHECK_THAT(after.location[1], WithinAbs(0, 1e-3));
    CHECK_THAT(after.location[2], WithinAbs(FlyCamera::SPEED, 1e-3));
}

TEST_CASE("UTA-0016: a diagonal is no faster and Shift is FAST times faster", "[client]") {
    FlyCamera fly;
    fly.update(Frame{.forward = 1, .right = 1}, 1);
    const auto location = fly.camera(1920, 1080).location;
    CHECK_THAT(std::hypot(location[0], location[1], location[2]), WithinAbs(FlyCamera::SPEED, 1e-2));

    FlyCamera fast;
    fast.update(Frame{.forward = 1, .walk = true}, 1);
    CHECK_THAT(fast.camera(1920, 1080).location[0], WithinAbs(FlyCamera::SPEED * FlyCamera::FAST, 1e-2));
}

TEST_CASE("UTA-0016: the mouse turns the view toward where it moves", "[client]") {
    const std::array<float, 3> toTheRight{0, 100, 0};
    const std::array<float, 3> above{100, 0, 100};

    FlyCamera turned;
    REQUIRE(inView(turned.camera(1920, 1080), toTheRight)[0] > 99); // right of the view before turning
    turned.update(Frame{.lookRight = 16384}, 0);
    checkNear(inView(turned.camera(1920, 1080), toTheRight), {0, 0, 100});

    FlyCamera tipped;
    tipped.update(Frame{.lookUp = 8192}, 0);
    checkNear(inView(tipped.camera(1920, 1080), above), {0, 0, std::hypot(100.0, 100.0)});
}

namespace {

/// One room, 1000 units a side and centred on the origin; everything else solid.
uta::ubundle::CollisionTree room() {
    using uta::test::paths::box;
    const uta::test::paths::Vec3 low{-500, -500, -500}, high{500, 500, 500};
    return uta::test::paths::worldOf({box(low, high)}, low, high);
}

} // namespace

TEST_CASE("UTA-0158: flying into a wall stops WALL_MARGIN short of it", "[client]") {
    const auto level = room();
    FlyCamera fly({0, 0, 0}, 0, 0); // facing +X, toward the wall at 500
    fly.update(Frame{.forward = 1}, 1, &level); // 800 units: would pass it
    const auto location = fly.camera(1920, 1080).location;
    CHECK_THAT(location[0], WithinAbs(500 - FlyCamera::WALL_MARGIN, 0.5));
    CHECK_THAT(location[1], WithinAbs(0, 1e-3));
    CHECK_THAT(location[2], WithinAbs(0, 1e-3));

    fly.update(Frame{.forward = 1}, 1, &level); // pressed against it: stays
    CHECK_THAT(fly.camera(1920, 1080).location[0], WithinAbs(500 - FlyCamera::WALL_MARGIN, 0.5));
}

TEST_CASE("UTA-0158: a move into a wall at an angle slides along it", "[client]") {
    const auto level = room();
    FlyCamera fly({300, 0, 0}, 0, 8192); // facing between +X and +Y
    fly.update(Frame{.forward = 1}, 0.5, &level); // 400 units: 283 on each axis
    const auto location = fly.camera(1920, 1080).location;
    CHECK_THAT(location[0], WithinAbs(500 - FlyCamera::WALL_MARGIN, 0.5));
    // The part into the wall is lost and the part along it kept: more than the
    // stop alone reaches, never more than the whole move along +Y.
    CHECK(location[1] > 250);
    CHECK(location[1] <= 283);
}

TEST_CASE("UTA-0174: flying past a corner keeps WALL_MARGIN from its side", "[client]") {
    // An L-shaped room: the quarter x > 0 and y > 0 is a solid block whose
    // corner stands at the origin. A move drifting along +X passes the corner
    // without entering the block, so no trace along the move stops short of
    // it, and it ends pressed against the block's face.
    using uta::test::paths::box;
    const uta::test::paths::Vec3 low{-500, -500, -500}, high{500, 500, 500};
    const auto level = uta::test::paths::worldOf(
        {box({-500, -500, -500}, {500, 0, 500}), box({-500, 0, -500}, {0, 500, 500})}, low, high);
    FlyCamera fly({-300, -20, 0}, 0, 0); // facing +X
    for (int frame = 0; frame < 60; ++frame) {
        // Drift toward the block's face while flying past its corner.
        fly.update(Frame{.forward = 1, .right = 0.05f}, 1.0 / 60, &level);
    }
    const auto location = fly.camera(1920, 1080).location;
    CHECK(location[0] > 0); // it did pass the corner
    CHECK(location[1] <= -FlyCamera::WALL_MARGIN + 0.5);
}

TEST_CASE("UTA-0174: a camera nearer a wall than WALL_MARGIN is eased out to it", "[client]") {
    const auto level = room();
    FlyCamera fly({498, 0, 0}, 0, 0); // 2 units from the wall at 500
    fly.update(Frame{}, 1.0 / 60, &level);
    CHECK_THAT(fly.camera(1920, 1080).location[0], WithinAbs(500 - FlyCamera::WALL_MARGIN, 0.5));
    fly.update(Frame{}, 1.0 / 60, &level); // and stays there
    CHECK_THAT(fly.camera(1920, 1080).location[0], WithinAbs(500 - FlyCamera::WALL_MARGIN, 0.5));

    FlyCamera low({0, 0, -499}, 0, 0); // 1 unit above the floor
    low.update(Frame{}, 1.0 / 60, &level);
    CHECK_THAT(low.camera(1920, 1080).location[2], WithinAbs(-500 + FlyCamera::WALL_MARGIN, 0.5));
}

TEST_CASE("UTA-0158: a camera starting in solid flies out freely", "[client]") {
    const auto level = room();
    FlyCamera fly({700, 0, 0}, 0, 32768); // outside the room, facing -X, toward it
    fly.update(Frame{.forward = 1}, 0.5, &level);
    CHECK_THAT(fly.camera(1920, 1080).location[0], WithinAbs(300, 1e-2));
}

TEST_CASE("UTA-0158: without a level the camera flies through walls as before", "[client]") {
    const auto level = room();
    FlyCamera walled({0, 0, 0}, 0, 0), free({0, 0, 0}, 0, 0);
    walled.update(Frame{.forward = 1}, 1, &level);
    free.update(Frame{.forward = 1}, 1);
    CHECK_THAT(free.camera(1920, 1080).location[0], WithinAbs(FlyCamera::SPEED, 1e-2));
    CHECK(walled.camera(1920, 1080).location[0] < free.camera(1920, 1080).location[0]);
}

TEST_CASE("UTA-0016: pitch stops short of straight up and straight down", "[client]") {
    FlyCamera fly;
    fly.update(Frame{.lookUp = 100000}, 0);
    CHECK(fly.camera(1920, 1080).rotation[0] == FlyCamera::PITCH_LIMIT);
    fly.update(Frame{.lookUp = -100000}, 0);
    CHECK(fly.camera(1920, 1080).rotation[0] == -FlyCamera::PITCH_LIMIT);
    CHECK(FlyCamera({0, 0, 0}, 20000, 0).camera(1920, 1080).rotation[0] == FlyCamera::PITCH_LIMIT);
}

TEST_CASE("UTA-0190: a pose line is a camera line ut-shot reads", "[client]") {
    Camera camera;
    camera.location = {1.5f, -2.25f, 3};
    camera.rotation = {100, -200, 0};
    // The viewer's 90 degrees are vertical, and ut-shot's field is horizontal.
    CHECK(uta::client::poseLine(camera, 1920, 1080) == "1.50 -2.25 3.00 100 -200 0 121.28");
    CHECK(uta::client::poseLine(camera, 1000, 1000) == "1.50 -2.25 3.00 100 -200 0 90.00");
}

TEST_CASE("UTA-0357: the camera opens at UT99's FOV 90, across the screen's width", "[client]") {
    const FlyCamera fly;
    CHECK(fly.fov() == FlyCamera::DEFAULT_FOV);
    CHECK(FlyCamera::DEFAULT_FOV == 90);
    // UT469 draws FOV 90 on a 16:9 screen as 90 degrees across it, as on 4:3.
    CHECK(uta::client::poseLine(fly.camera(1440, 1080), 1440, 1080) == "0.00 0.00 0.00 0 0 0 90.00");
    CHECK(uta::client::poseLine(fly.camera(1920, 1080), 1920, 1080) == "0.00 0.00 0.00 0 0 0 90.00");
}

TEST_CASE("UTA-0357: the FOV is the player's to change, within limits", "[client]") {
    FlyCamera fly;
    fly.setFov(110);
    CHECK(fly.fov() == 110);
    CHECK(uta::client::poseLine(fly.camera(1440, 1080), 1440, 1080) == "0.00 0.00 0.00 0 0 0 110.00");
    fly.setFov(FlyCamera::MIN_FOV - 1);
    CHECK(fly.fov() == FlyCamera::MIN_FOV);
    fly.setFov(FlyCamera::MAX_FOV + 1);
    CHECK(fly.fov() == FlyCamera::MAX_FOV);
    fly.setFov(std::numeric_limits<double>::quiet_NaN());
    CHECK(fly.fov() == FlyCamera::MAX_FOV);
}

TEST_CASE("a start's pitch is read as a turn, so 63488 is slightly down", "[client]") {
    // review-code 2026-09-26: a rotator is stored in 0..65535 as often as
    // signed. Clamped raw, a start tilted down looked almost straight up.
    const FlyCamera down({0, 0, 0}, 63488, 0);
    CHECK(down.camera(1920, 1080).rotation[0] == -2048);
    const FlyCamera up({0, 0, 0}, 2048, 0);
    CHECK(up.camera(1920, 1080).rotation[0] == 2048);
}
