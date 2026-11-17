// docs/specs/UTA-0020-weapon-wheel.md INV-7: the weapon wheel highlights the
// item its pointer points at, counted clockwise from the top; a stick let go
// keeps the highlight. And the ring's layout (SS 4.6).
//
// NO TEST NAME CONTAINS A COMMA -- BakeCliTest.cpp says why.

#include "uui/Wheel.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using uta::uinput::Frame;
using uta::uui::Wheel;

namespace {

/// Four weapons, by their indices in a set: the wheel's top, right, bottom and left.
const std::vector<int> FOUR{10, 11, 12, 13};

Frame stick(double right, double down) {
    Frame frame;
    frame.pointRight = right;
    frame.pointDown = down;
    return frame;
}

Frame look(double right, double up) {
    Frame frame;
    frame.lookRight = right;
    frame.lookUp = up;
    return frame;
}

} // namespace

TEST_CASE("UTA-0020 INV-7: the stick highlights the item it points at clockwise from the top", "[uui]") {
    Wheel wheel;
    wheel.open(FOUR);
    CHECK(wheel.isOpen());
    CHECK(wheel.highlighted() == -1);
    wheel.point(stick(0, -1));
    CHECK(wheel.highlighted() == 10);
    wheel.point(stick(1, 0));
    CHECK(wheel.highlighted() == 11);
    wheel.point(stick(0, 1));
    CHECK(wheel.highlighted() == 12);
    wheel.point(stick(-1, 0));
    CHECK(wheel.highlighted() == 13);
    // Within half a sector: a little short of the top still picks it.
    wheel.point(stick(-0.6, -0.8));
    CHECK(wheel.highlighted() == 10);
    // A stick let go keeps the highlight, so a flick then a release picks.
    wheel.point(stick(-1, 0));
    wheel.point(Frame{});
    CHECK(wheel.highlighted() == 13);
    CHECK(wheel.close() == 13);
    CHECK_FALSE(wheel.isOpen());
}

TEST_CASE("UTA-0020 INV-7: mouse look moves the pointer to the rim and no further", "[uui]") {
    Wheel wheel;
    wheel.open(FOUR);
    // Short of PICKS_FROM, nothing.
    wheel.point(look(0.2 * uta::uui::MOUSE_REACH, 0));
    CHECK(wheel.highlighted() == -1);
    // Far past the rim to the right: held at it.
    wheel.point(look(5 * uta::uui::MOUSE_REACH, 0));
    CHECK(wheel.highlighted() == 11);
    CHECK_THAT(wheel.pointRight(), WithinAbs(1, 1e-9));
    // Then far up, across the rim: the pointer turns to the top.
    wheel.point(look(0, 50 * uta::uui::MOUSE_REACH));
    CHECK(wheel.highlighted() == 10);
    CHECK_THAT(std::hypot(wheel.pointRight(), wheel.pointDown()), WithinAbs(1, 1e-9));
    // Opened again, it starts over.
    wheel.open(FOUR);
    CHECK(wheel.highlighted() == -1);
    CHECK(wheel.pointRight() == 0);
}

TEST_CASE("UTA-0020 INV-7: an empty wheel closes to no pick", "[uui]") {
    Wheel wheel;
    wheel.open({});
    wheel.point(stick(1, 0));
    CHECK(wheel.highlighted() == -1);
    CHECK(wheel.close() == -1);
}

TEST_CASE("UTA-0020 SS 4.6: the ring lays its items out clockwise from the top then the pointer", "[uui]") {
    std::vector<std::string> names(14);
    for (std::size_t i = 10; i < 14; ++i) names[i] = "W" + std::to_string(i);
    Wheel wheel;
    CHECK(layOut(wheel, names).empty()); // shut, nothing
    wheel.open(FOUR);
    wheel.point(stick(1, 0));
    const auto labels = layOut(wheel, names);
    REQUIRE(labels.size() == 5);
    CHECK(labels[0].text == "W10");
    CHECK_THAT(labels[0].x, WithinAbs(0, 1e-9));
    CHECK_THAT(labels[0].y, WithinAbs(-uta::uui::RING_RADIUS, 1e-9));
    CHECK_THAT(labels[1].x, WithinAbs(uta::uui::RING_RADIUS, 1e-9));
    CHECK(labels[1].highlighted);
    CHECK_FALSE(labels[0].highlighted);
    CHECK(labels[4].text.empty()); // the pointer, at the rim's reach
    CHECK_THAT(labels[4].x, WithinAbs(uta::uui::POINTER_REACH, 1e-9));
}
