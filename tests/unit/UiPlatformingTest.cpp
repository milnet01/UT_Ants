// docs/specs/UTA-0021-landing-marker.md INV-4: the take-off pitch tips the
// view only as far as brings the landing into frame, capped; and the marker's
// shape (SS 4.3).
//
// NO TEST NAME CONTAINS A COMMA -- BakeCliTest.cpp says why.

#include "uui/Platforming.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>

using Catch::Matchers::WithinAbs;
using uta::uui::easeTip;
using uta::uui::IN_FRAME;
using uta::uui::MAX_TIP_DEGREES;
using uta::uui::tipTarget;

namespace {

constexpr double FOV = 90, ASPECT = 16.0 / 9.0;
constexpr double QUARTER_TURN = 16384;

double radians(double degrees) { return degrees * std::numbers::pi / 180; }

/// The limit's angle below the aim: IN_FRAME of the way to the bottom edge.
double limitDegrees() {
    return std::atan(IN_FRAME * std::tan(radians(FOV / 2)) / ASPECT) * 180 / std::numbers::pi;
}

/// A landing straight ahead along +X, `degrees` below the eye at the origin.
std::array<double, 3> below(double degrees) { return {100, 0, -100 * std::tan(radians(degrees))}; }

} // namespace

TEST_CASE("UTA-0021 INV-4: no tip while the landing is in frame") {
    CHECK(tipTarget({0, 0, 0}, 0, 0, below(limitDegrees() - 0.5), FOV, ASPECT) == 0);
    CHECK(tipTarget({0, 0, 0}, 0, 0, {100, 0, 50}, FOV, ASPECT) == 0); // above the aim
}

TEST_CASE("UTA-0021 INV-4: below the frame the tip is the angle past the limit") {
    CHECK_THAT(tipTarget({0, 0, 0}, 0, 0, below(limitDegrees() + 5), FOV, ASPECT), WithinAbs(5, 1e-9));
    // Turned a quarter right (+Y) and lifted, the same landing gives the same tip.
    const std::array<double, 3> eye{10, 20, 30};
    const auto ahead = below(limitDegrees() + 5);
    CHECK_THAT(tipTarget(eye, 0, QUARTER_TURN, {eye[0] - ahead[1], eye[1] + ahead[0], eye[2] + ahead[2]}, FOV,
                         ASPECT),
               WithinAbs(5, 1e-9));
    // Looking 10 degrees down already, it needs 10 degrees less.
    CHECK_THAT(tipTarget({0, 0, 0}, -10 * 65536.0 / 360, 0, below(limitDegrees() + 12), FOV, ASPECT),
               WithinAbs(2, 1e-9));
}

TEST_CASE("UTA-0021 INV-4: the tip is capped") {
    CHECK(tipTarget({0, 0, 0}, 0, 0, below(limitDegrees() + 40), FOV, ASPECT) == MAX_TIP_DEGREES);
}

TEST_CASE("UTA-0021 INV-4: no tip for a landing behind or off to the side") {
    CHECK(tipTarget({0, 0, 0}, 0, 0, {-100, 0, -100}, FOV, ASPECT) == 0);
    CHECK(tipTarget({0, 0, 0}, 0, 0, {100, 300, -100}, FOV, ASPECT) == 0);
}

TEST_CASE("UTA-0021 INV-4: easing moves toward the target and never past it") {
    CHECK(easeTip(3, 10, 0) == 3);
    const double part = easeTip(0, 10, 0.05);
    CHECK(part > 0);
    CHECK(part < 10);
    CHECK(easeTip(0, 10, 10) == 10);
    CHECK(easeTip(10, 0, 10) == 0);
}

TEST_CASE("UTA-0021: the marker lies on the floor under the landing") {
    const auto shape = uta::uui::landingMarker({5, 6, 40}, 17, 39);
    CHECK(shape.kind == uta::urender::Shape::Kind::Cylinder);
    CHECK(shape.radius == 17);
    CHECK_THAT(shape.a[2] - shape.halfHeight, WithinAbs(1 + 0.5, 1e-6)); // its underside just above the feet at z = 1
    CHECK(shape.colour[3] < 1);
}
