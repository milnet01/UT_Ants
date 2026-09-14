// UTA-0016: the client's free-flying camera, graded against urender's own view
// matrix rather than against a second statement of UT's axes.
//
// NO TEST NAME CONTAINS A COMMA -- BakeCliTest.cpp says why.

#include "ut-ants/FlyCamera.h"

#include "urender/Placement.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>

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

TEST_CASE("UTA-0016: pitch stops short of straight up and straight down", "[client]") {
    FlyCamera fly;
    fly.update(FlyInput{.lookUp = 100000}, 0);
    CHECK(fly.camera().rotation[0] == FlyCamera::PITCH_LIMIT);
    fly.update(FlyInput{.lookUp = -100000}, 0);
    CHECK(fly.camera().rotation[0] == -FlyCamera::PITCH_LIMIT);
    CHECK(FlyCamera({0, 0, 0}, 20000, 0).camera().rotation[0] == FlyCamera::PITCH_LIMIT);
}
