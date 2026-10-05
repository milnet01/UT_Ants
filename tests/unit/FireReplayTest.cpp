// UTA-0286: the parts of the FireTexture animation only the renderer's replay
// uses -- the turning spark types, the colouring and the step rate.
//
// NO TEST NAME CONTAINS A COMMA -- BakeCliTest.cpp says why.

#include "ufire/Fire.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

using uta::ufire::Fire;
using uta::ufire::Spark;
using uta::ufire::Turning;

namespace {

constexpr std::uint32_t SIZE = 64;
constexpr int SPARK_AT = 32;

/// The farthest heated texel from the spark, in texels along either axis.
int reach(const Fire& fire) {
    int farthest = -1;
    for (std::uint32_t y = 0; y < SIZE; ++y)
        for (std::uint32_t x = 0; x < SIZE; ++x)
            if (fire.heat()[y * SIZE + x] > 0)
                farthest = std::max({farthest, std::abs(int(x) - SPARK_AT), std::abs(int(y) - SPARK_AT)});
    return farthest;
}

// RenderHeat 239 cools by exactly nothing beyond the blur, so a cold texel
// stays cold and any heat far from the spark was put there.
constexpr uta::ufire::Settings STILL_AIR{.renderHeat = 239, .rising = false, .sparksLimit = 16};

} // namespace

TEST_CASE("UTA-0286 INV-4: sphere lightning draws a line only when modelled", "[ufire]") {
    const std::vector<Spark> lightning{
        {.type = 25, .heat = 255, .x = SPARK_AT, .y = SPARK_AT, .byteC = 40, .byteD = 0}};
    Fire modelled(SIZE, SIZE, lightning, STILL_AIR, Turning::Model);
    Fire scattered(SIZE, SIZE, lightning, STILL_AIR, Turning::Scatter);
    modelled.step();
    scattered.step();
    CHECK(reach(modelled) >= 8);
    CHECK(reach(scattered) >= 0);
    CHECK(reach(scattered) <= 2);
}

TEST_CASE("UTA-0286 INV-4: a wheel's twirl walks away from its spark only when modelled", "[ufire]") {
    // Angle 0 and no turn: each twirl walks straight down the texture, half a
    // texel a step for its 20 steps. Scattered, byteB is a scatter range
    // instead, so the two fields must differ.
    const std::vector<Spark> wheel{
        {.type = 26, .heat = 255, .x = SPARK_AT, .y = SPARK_AT, .byteA = 0, .byteB = 20, .byteC = 0, .byteD = 0}};
    Fire modelled(SIZE, SIZE, wheel, STILL_AIR, Turning::Model);
    Fire scattered(SIZE, SIZE, wheel, STILL_AIR, Turning::Scatter);
    for (int i = 0; i < 16; ++i) {
        modelled.step();
        scattered.step();
    }
    int lowest = -1;
    for (std::uint32_t y = 0; y < SIZE; ++y)
        if (modelled.heat()[y * SIZE + SPARK_AT] > 0) lowest = int(y);
    CHECK(lowest >= SPARK_AT + 6);
    CHECK(!std::ranges::equal(modelled.heat(), scattered.heat()));
}

TEST_CASE("UTA-0286 INV-5: a texel takes its heat's palette entry and only masked heat 0 is see-through", "[ufire]") {
    std::array<std::array<std::uint8_t, 3>, 256> palette{};
    for (int i = 0; i < 256; ++i)
        palette[i] = {std::uint8_t(i), std::uint8_t(255 - i), std::uint8_t(7)};
    const std::array<std::uint8_t, 3> heat{0, 1, 255};
    std::array<std::byte, 12> masked{}, opaque{};
    uta::ufire::colour(heat, palette, true, masked);
    uta::ufire::colour(heat, palette, false, opaque);
    for (std::size_t i = 0; i < heat.size(); ++i) {
        INFO("texel " << i);
        CHECK(std::to_integer<int>(masked[i * 4 + 0]) == heat[i]);
        CHECK(std::to_integer<int>(masked[i * 4 + 1]) == 255 - heat[i]);
        CHECK(std::to_integer<int>(masked[i * 4 + 2]) == 7);
        CHECK(std::to_integer<int>(opaque[i * 4 + 3]) == 255);
    }
    CHECK(std::to_integer<int>(masked[3]) == 0);
    CHECK(std::to_integer<int>(masked[7]) == 255);
    CHECK(std::to_integer<int>(masked[11]) == 255);
}

TEST_CASE("UTA-0286 INV-6: a fire steps at its MaxFrameRate up to 60 and at 30 when it states none", "[ufire]") {
    CHECK(uta::ufire::stepsPerSecond(0.0f) == 30.0);
    CHECK(uta::ufire::stepsPerSecond(25.0f) == 25.0);
    CHECK(uta::ufire::stepsPerSecond(60.0f) == 60.0);
    CHECK(uta::ufire::stepsPerSecond(100.0f) == 60.0);
    CHECK(uta::ufire::stepsPerSecond(-1.0f) == 30.0);
    CHECK(uta::ufire::stepsPerSecond(std::numeric_limits<float>::quiet_NaN()) == 30.0);
}
