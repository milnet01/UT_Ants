// UTA-0112's light model -- docs/specs/UTA-0112-baked-light-probes.md SS 4.3,
// INV-2, INV-3, INV-4 and INV-5's sRGB half.
//
// THE static_asserts BELOW ARE INV-4's GRADER FOR THE SINE. Clang will not
// evaluate a library std::sin in a constant expression, so a sineOf, cosineOf
// or directionOf that calls one fails to compile on the Clang leg. GCC
// evaluates it as an extension, so the GCC leg does not see it.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "ubake/LightModel.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>

using uta::ubake::cosineOf;
using uta::ubake::directionOf;
using uta::ubake::falloff;
using uta::ubake::lightAt;
using uta::ubake::lightColour;
using uta::ubake::lightRadius;
using uta::ubake::linearOf;
using uta::ubake::Rgb;
using uta::ubake::sineOf;
using uta::ubake::Vec3;
using uta::ubundle::Light;

namespace {

constexpr std::uint8_t LE_STATIC_SPOT = 8;
constexpr std::uint8_t LE_SPOTLIGHT = 12;
constexpr std::uint8_t LE_NON_INCIDENCE = 13;

/// A steady white light of radius byte 64 at the origin.
Light whiteLight(std::uint8_t brightness) {
    Light light;
    light.type = 1;
    light.brightness = brightness;
    light.saturation = 255;
    light.radius = 64;
    return light;
}

void sameRgb(const Rgb& actual, const Rgb& expected) {
    CHECK(actual.r == expected.r);
    CHECK(actual.g == expected.g);
    CHECK(actual.b == expected.b);
}

void sameVec(const Vec3& actual, const Vec3& expected) {
    CHECK(actual.x == expected.x);
    CHECK(actual.y == expected.y);
    CHECK(actual.z == expected.z);
}

} // namespace

// INV-4's compile-time half.
static_assert(sineOf(0) == 0.0);
static_assert(cosineOf(0) == 1.0);
static_assert(sineOf(16384) == 1.0);
static_assert(sineOf(-16384) == -1.0);
static_assert(directionOf({0, 0, 0}).x == 1.0);
static_assert(directionOf({16384, 0, 0}).z == 1.0);

TEST_CASE("light colour", "[ubake][lightmodel]") {
    sameRgb(lightColour(0, 0), {1, 0, 0});
    sameRgb(lightColour(64, 0), {0.5, 1, 0});
    sameRgb(lightColour(128, 0), {0, 1, 1});
    for (int hue = 0; hue < 256; ++hue) sameRgb(lightColour(static_cast<std::uint8_t>(hue), 255), {1, 1, 1});
}

TEST_CASE("radius and falloff", "[ubake][lightmodel]") {
    CHECK(lightRadius(0) == 25.0);
    CHECK(lightRadius(64) == 1625.0);
    CHECK(falloff(0, 1625) == 1.0);
    CHECK(falloff(812.5, 1625) == 0.5625);
    CHECK(falloff(1625, 1625) == 0.0);
    CHECK(falloff(2000, 1625) == 0.0);
}

TEST_CASE("intensity incidence and spot", "[ubake][lightmodel]") {
    SECTION("a white light at its own location") {
        sameRgb(lightAt(whiteLight(255), {0, 0, 0}, {0, 0, 1}), {1, 1, 1});
        const double fifth = 51.0 / 255.0;
        sameRgb(lightAt(whiteLight(51), {0, 0, 0}, {0, 0, 1}), {fifth, fifth, fifth});
    }
    SECTION("incidence") {
        const Light light = whiteLight(255);
        const Rgb facing = lightAt(light, {100, 0, 0}, {-1, 0, 0});
        CHECK(facing.r > 0);
        sameRgb(lightAt(light, {100, 0, 0}, {1, 0, 0}), {0, 0, 0});
        Light plain = light;
        plain.effect = LE_NON_INCIDENCE;
        sameRgb(lightAt(plain, {100, 0, 0}, {1, 0, 0}), facing);
    }
    SECTION("spot") {
        for (const std::uint8_t effect : {LE_SPOTLIGHT, LE_STATIC_SPOT}) {
            Light spot = whiteLight(255);
            spot.effect = effect;
            spot.cone = 128;
            // Rotation zero points along +X.
            CHECK(lightAt(spot, {100, 0, 0}, {-1, 0, 0}).r > 0);
            sameRgb(lightAt(spot, {-100, 0, 0}, {1, 0, 0}), {0, 0, 0});
            // Off the axis by more than the cone: 45 degrees off, where the cone
            // byte 50 sets a cosine threshold of 1 - 50/256, above cos 45.
            spot.cone = 50;
            sameRgb(lightAt(spot, {100, 100, 0}, {-1, 0, 0}), {0, 0, 0});
            spot.cone = 0;
            sameRgb(lightAt(spot, {100, 0, 0}, {-1, 0, 0}), {0, 0, 0});
        }
    }
    SECTION("direction") {
        sameVec(directionOf({0, 0, 0}), {1, 0, 0});
        sameVec(directionOf({0, 16384, 0}), {0, 1, 0});
        sameVec(directionOf({16384, 0, 0}), {0, 0, 1});
        sameVec(directionOf({16384, 0, 12345}), {0, 0, 1});
        sameVec(directionOf({0, 16384, -7000}), {0, 1, 0});
    }
    SECTION("sine") {
        // Recorded from sineOf itself, under GCC and Clang alike, so a changed
        // polynomial fails here. Each leg of the matrix must agree with them,
        // which is the numeric contract doing its job.
        constexpr std::array<std::pair<std::int32_t, std::uint64_t>, 12> RECORDED{{
            {1, 0x3f1921fb539ecf31ULL},
            {1000, 0x3fb8819069d580a6ULL},
            {8191, 0x3fe6a01038a7c38aULL},
            {8192, 0x3fe6a09e667f3c84ULL},
            {8193, 0x3fe6a12c90d96951ULL},
            {12345, 0x3feda1709c896780ULL},
            {16383, 0x3feffffffd885867ULL},
            {20000, 0x3fee18a02fdc66d9ULL},
            {30000, 0x3fd0c91bda4f158eULL},
            {40000, 0xbfe473b51b987363ULL},
            {50000, 0xbfefe4f0e31d7a4aULL},
            {65535, 0xbf1921fb539ecf31ULL},
        }};
        for (const auto& [angle, bits] : RECORDED) CHECK(std::bit_cast<std::uint64_t>(sineOf(angle)) == bits);
        double worst = 0;
        for (std::int32_t angle = 0; angle < 65536; ++angle) {
            const double exact = std::sin(angle * 2.0 * std::numbers::pi / 65536.0);
            worst = std::max(worst, std::abs(sineOf(angle) - exact));
        }
        CHECK(worst < 1e-12);
    }
}

TEST_CASE("sRGB", "[ubake][lightmodel]") {
    for (int byte = 0; byte < 256; ++byte) {
        const double c = byte / 255.0;
        const double expected = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
        CHECK(std::abs(linearOf(static_cast<std::uint8_t>(byte)) - expected) <= 1e-15);
    }
}
