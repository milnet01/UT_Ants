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
using uta::client::FlyInput;
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
    const Camera before = fly.camera();
    fly.update(FlyInput{.forward = 1}, 0.5);
    checkNear(inView(before, fly.camera().location), {0, 0, FlyCamera::SPEED * 0.5});

    fly.update(FlyInput{.forward = -1}, 0.5);
    checkNear(inView(before, fly.camera().location), {0, 0, 0});
}

TEST_CASE("UTA-0016: flying right moves to the right in urender's view", "[client]") {
    FlyCamera fly({-50, 25, 0}, 0, 40000);
    const Camera before = fly.camera();
    fly.update(FlyInput{.right = 1}, 0.25);
    checkNear(inView(before, fly.camera().location), {FlyCamera::SPEED * 0.25, 0, 0});
}

TEST_CASE("UTA-0016: rising follows the world's Z whatever the pitch", "[client]") {
    FlyCamera fly({0, 0, 0}, -9000, 1234);
    fly.update(FlyInput{.up = 1}, 1);
    const Camera after = fly.camera();
    CHECK_THAT(after.location[0], WithinAbs(0, 1e-3));
    CHECK_THAT(after.location[1], WithinAbs(0, 1e-3));
    CHECK_THAT(after.location[2], WithinAbs(FlyCamera::SPEED, 1e-3));
}

TEST_CASE("UTA-0016: a diagonal is no faster and Shift is FAST times faster", "[client]") {
    FlyCamera fly;
    fly.update(FlyInput{.forward = 1, .right = 1}, 1);
    const auto location = fly.camera().location;
    CHECK_THAT(std::hypot(location[0], location[1], location[2]), WithinAbs(FlyCamera::SPEED, 1e-2));

    FlyCamera fast;
    fast.update(FlyInput{.forward = 1, .fast = true}, 1);
    CHECK_THAT(fast.camera().location[0], WithinAbs(FlyCamera::SPEED * FlyCamera::FAST, 1e-2));
}

TEST_CASE("UTA-0016: the mouse turns the view toward where it moves", "[client]") {
    const std::array<float, 3> toTheRight{0, 100, 0};
    const std::array<float, 3> above{100, 0, 100};

    FlyCamera turned;
    REQUIRE(inView(turned.camera(), toTheRight)[0] > 99); // right of the view before turning
    turned.update(FlyInput{.lookRight = 16384 / FlyCamera::LOOK_UNITS_PER_PIXEL}, 0);
    checkNear(inView(turned.camera(), toTheRight), {0, 0, 100});

    FlyCamera tipped;
    tipped.update(FlyInput{.lookUp = 8192 / FlyCamera::LOOK_UNITS_PER_PIXEL}, 0);
    checkNear(inView(tipped.camera(), above), {0, 0, std::hypot(100.0, 100.0)});
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
    fly.update(FlyInput{.forward = 1}, 1, &level); // 800 units: would pass it
    const auto location = fly.camera().location;
    CHECK_THAT(location[0], WithinAbs(500 - FlyCamera::WALL_MARGIN, 0.5));
    CHECK_THAT(location[1], WithinAbs(0, 1e-3));
    CHECK_THAT(location[2], WithinAbs(0, 1e-3));

    fly.update(FlyInput{.forward = 1}, 1, &level); // pressed against it: stays
    CHECK_THAT(fly.camera().location[0], WithinAbs(500 - FlyCamera::WALL_MARGIN, 0.5));
}

TEST_CASE("UTA-0158: a move into a wall at an angle slides along it", "[client]") {
    const auto level = room();
    FlyCamera fly({300, 0, 0}, 0, 8192); // facing between +X and +Y
    fly.update(FlyInput{.forward = 1}, 0.5, &level); // 400 units: 283 on each axis
    const auto location = fly.camera().location;
    CHECK_THAT(location[0], WithinAbs(500 - FlyCamera::WALL_MARGIN, 0.5));
    // The part into the wall is lost and the part along it kept: more than the
    // stop alone reaches, never more than the whole move along +Y.
    CHECK(location[1] > 250);
    CHECK(location[1] <= 283);
}

TEST_CASE("UTA-0158: a camera starting in solid flies out freely", "[client]") {
    const auto level = room();
    FlyCamera fly({700, 0, 0}, 0, 32768); // outside the room, facing -X, toward it
    fly.update(FlyInput{.forward = 1}, 0.5, &level);
    CHECK_THAT(fly.camera().location[0], WithinAbs(300, 1e-2));
}

TEST_CASE("UTA-0158: without a level the camera flies through walls as before", "[client]") {
    const auto level = room();
    FlyCamera walled({0, 0, 0}, 0, 0), free({0, 0, 0}, 0, 0);
    walled.update(FlyInput{.forward = 1}, 1, &level);
    free.update(FlyInput{.forward = 1}, 1);
    CHECK_THAT(free.camera().location[0], WithinAbs(FlyCamera::SPEED, 1e-2));
    CHECK(walled.camera().location[0] < free.camera().location[0]);
}

TEST_CASE("UTA-0016: pitch stops short of straight up and straight down", "[client]") {
    FlyCamera fly;
    fly.update(FlyInput{.lookUp = 100000}, 0);
    CHECK(fly.camera().rotation[0] == FlyCamera::PITCH_LIMIT);
    fly.update(FlyInput{.lookUp = -100000}, 0);
    CHECK(fly.camera().rotation[0] == -FlyCamera::PITCH_LIMIT);
    CHECK(FlyCamera({0, 0, 0}, 20000, 0).camera().rotation[0] == FlyCamera::PITCH_LIMIT);
}

// UTA-0167: a gamepad's sticks and triggers, mapped to the same input the
// keyboard and mouse give.

using uta::client::addPad;
using uta::client::PAD_DEAD_ZONE;
using uta::client::PAD_LOOK_RATE;
using uta::client::PadInput;

TEST_CASE("UTA-0167: a stick resting inside the dead zone moves nothing", "[client]") {
    const float inside = PAD_DEAD_ZONE * 0.9f;
    FlyInput input;
    addPad(input, PadInput{.leftX = inside, .leftY = 0, .rightX = 0, .rightY = -inside, .rise = inside}, 1);
    CHECK(input.forward == 0);
    CHECK(input.right == 0);
    CHECK(input.up == 0);
    CHECK(input.lookRight == 0);
    CHECK(input.lookUp == 0);
}

TEST_CASE("UTA-0167: the left stick flies as W A S and D do", "[client]") {
    FlyInput forward;
    addPad(forward, PadInput{.leftY = -1}, 1); // pushed away from the player
    CHECK_THAT(forward.forward, WithinAbs(1, 1e-6));
    CHECK_THAT(forward.right, WithinAbs(0, 1e-6));

    FlyInput right;
    addPad(right, PadInput{.leftX = 1}, 1);
    CHECK_THAT(right.right, WithinAbs(1, 1e-6));

    // Past the dead zone the move grows from nothing rather than jumping.
    FlyInput halfway;
    addPad(halfway, PadInput{.leftY = (1 + PAD_DEAD_ZONE) / 2}, 1);
    CHECK_THAT(halfway.forward, WithinAbs(-0.5, 1e-6));

    // A diagonal pushed into the stick's square corner is no longer than straight.
    FlyInput corner;
    addPad(corner, PadInput{.leftX = 1, .leftY = -1}, 1);
    CHECK_THAT(std::hypot(corner.forward, corner.right), WithinAbs(1, 1e-6));
}

TEST_CASE("UTA-0167: the right stick turns at PAD_LOOK_RATE and slower with its square", "[client]") {
    FlyCamera full;
    FlyInput turn;
    addPad(turn, PadInput{.rightX = 1}, 0.25);
    full.update(turn, 0);
    CHECK(full.camera().rotation[1] == static_cast<std::int32_t>(PAD_LOOK_RATE * 0.25));

    FlyCamera tipped;
    FlyInput tip;
    addPad(tip, PadInput{.rightY = -1}, 0.25); // pushed away: the view tips up
    tipped.update(tip, 0);
    CHECK(tipped.camera().rotation[0] == static_cast<std::int32_t>(PAD_LOOK_RATE * 0.25));

    FlyInput half;
    addPad(half, PadInput{.rightX = (1 + PAD_DEAD_ZONE) / 2}, 1);
    CHECK_THAT(half.lookRight, WithinAbs(turn.lookRight, 1e-3)); // a quarter of the rate, four times as long
}

TEST_CASE("UTA-0167: the triggers rise and sink and the stick press flies fast", "[client]") {
    FlyInput rise;
    addPad(rise, PadInput{.rise = 1}, 1);
    CHECK_THAT(rise.up, WithinAbs(1, 1e-6));

    FlyInput both;
    addPad(both, PadInput{.rise = 1, .sink = 1}, 1);
    CHECK_THAT(both.up, WithinAbs(0, 1e-6));

    FlyInput fast;
    addPad(fast, PadInput{.fast = true}, 1);
    CHECK(fast.fast);

    // A pad at rest leaves the keyboard's input as it was.
    FlyInput keys{.forward = 1, .up = -1, .fast = true};
    addPad(keys, PadInput{}, 1);
    CHECK(keys.forward == 1);
    CHECK(keys.up == -1);
    CHECK(keys.fast);
}
