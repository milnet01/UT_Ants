// UTA-0452: the viewer's frame-rate counter (apps/ut-ants/FrameRate.h).
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "ut-ants/FrameRate.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using uta::client::FrameRate;

TEST_CASE("UTA-0452: the counter says nothing until its window closes", "[client]") {
    FrameRate rate;
    CHECK_FALSE(rate.add(0.1));
    CHECK(rate.text().empty());
}

TEST_CASE("UTA-0452: thirty frames of 1/60 s read 60 FPS 16.7 MS", "[client]") {
    FrameRate rate;
    bool changed = false;
    for (int i = 0; i < 30; ++i) changed = rate.add(1.0 / 60.0) || changed;
    // Thirty sixtieths may land a hair under the window; one more closes it.
    if (!changed) changed = rate.add(1.0 / 60.0);
    CHECK(changed);
    CHECK(rate.text() == "60 FPS 16.7 MS");
}

TEST_CASE("UTA-0452: the window starts again after it closes", "[client]") {
    FrameRate rate;
    CHECK(rate.add(0.5));
    CHECK(rate.text() == "2 FPS 500.0 MS");
    CHECK_FALSE(rate.add(0.25)); // half a window: the old text stands
    CHECK(rate.text() == "2 FPS 500.0 MS");
    CHECK(rate.add(0.25));
    CHECK(rate.text() == "4 FPS 250.0 MS");
}

TEST_CASE("UTA-0452: a negative or non-finite frame time counts nothing", "[client]") {
    FrameRate rate;
    CHECK_FALSE(rate.add(-1));
    CHECK_FALSE(rate.add(std::numeric_limits<double>::quiet_NaN()));
    CHECK_FALSE(rate.add(std::numeric_limits<double>::infinity()));
    CHECK(rate.add(0.5));
    CHECK(rate.text() == "2 FPS 500.0 MS"); // one frame: the bad ones were not counted
}
