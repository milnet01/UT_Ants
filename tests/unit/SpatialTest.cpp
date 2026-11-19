// UTA-0480 INV-5: where a sound is heard from
// (docs/specs/UTA-0480-map-sounds.md SS 4.2).

#include "uaudio/Spatial.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <limits>

using Catch::Matchers::WithinAbs;
using uta::uaudio::Listener;
using uta::uaudio::place;

TEST_CASE("UTA-0480 INV-5: loudness falls in a straight line to nothing at the radius", "[uaudio]") {
    const Listener listener;
    CHECK_THAT(place(listener, {0, 0, 0}, 1000).gain, WithinAbs(1.0, 1e-6));
    CHECK_THAT(place(listener, {0, 500, 0}, 1000).gain, WithinAbs(0.5, 1e-6));
    CHECK_THAT(place(listener, {0, 0, 250}, 1000).gain, WithinAbs(0.75, 1e-6));
    CHECK(place(listener, {0, 1000, 0}, 1000).gain == 0.0f);
    CHECK(place(listener, {0, 1500, 0}, 1000).gain == 0.0f);
    // Distance is from the listener, not from the origin.
    CHECK_THAT(place({.position = {100, 0, 0}}, {600, 0, 0}, 1000).gain, WithinAbs(0.5, 1e-6));
    CHECK(place(listener, {0, 0, 0}, 0).gain == 0.0f);
    CHECK(place(listener, {0, 0, 0}, -5).gain == 0.0f);
    CHECK(place(listener, {std::numeric_limits<float>::infinity(), 0, 0}, 1000).gain == 0.0f);
    CHECK(place(listener, {0, 0, 0}, std::numeric_limits<float>::quiet_NaN()).gain == 0.0f);
}

TEST_CASE("UTA-0480 INV-5: the side follows the listener's right hand", "[uaudio]") {
    const Listener listener; // right is +X
    CHECK_THAT(place(listener, {500, 0, 0}, 1000).pan, WithinAbs(0.8, 1e-6));
    CHECK_THAT(place(listener, {-500, 0, 0}, 1000).pan, WithinAbs(-0.8, 1e-6));
    CHECK_THAT(place(listener, {0, 500, 0}, 1000).pan, WithinAbs(0.0, 1e-6));
    CHECK_THAT(place(listener, {0, 0, 500}, 1000).pan, WithinAbs(0.0, 1e-6));
    // Turned round, the same source is on the other side.
    const Listener turned{.position = {0, 0, 0}, .right = {-1, 0, 0}};
    CHECK_THAT(place(turned, {500, 0, 0}, 1000).pan, WithinAbs(-0.8, 1e-6));
    // A listener whose right is +Y hears +Y on the right, not +X.
    const Listener quarter{.position = {0, 0, 0}, .right = {0, 1, 0}};
    CHECK_THAT(place(quarter, {0, 500, 0}, 1000).pan, WithinAbs(0.8, 1e-6));
    CHECK_THAT(place(quarter, {500, 0, 0}, 1000).pan, WithinAbs(0.0, 1e-6));
}

TEST_CASE("UTA-0480 INV-5: close to the source the side fades to the centre", "[uaudio]") {
    const Listener listener;
    CHECK_THAT(place(listener, {32, 0, 0}, 1000).pan, WithinAbs(0.4, 1e-6));
    CHECK_THAT(place(listener, {64, 0, 0}, 1000).pan, WithinAbs(0.8, 1e-6));
    CHECK(place(listener, {0, 0, 0}, 1000).pan == 0.0f);
}
