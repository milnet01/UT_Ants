// UTA-0176: a FireTexture's still picture, simulated from its sparks.
//
// NO TEST NAME CONTAINS A COMMA -- BakeCliTest.cpp says why.

#include "ubake/FireStill.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

using uta::ubake::FireSettings;
using uta::ubake::fireStill;
using uta::upkg::Spark;

namespace {

constexpr std::uint32_t SIZE = 64;

/// The heat of row `y`, summed across it.
long rowHeat(const std::vector<std::byte>& field, std::uint32_t y) {
    long sum = 0;
    for (std::uint32_t x = 0; x < SIZE; ++x) sum += std::to_integer<int>(field[y * SIZE + x]);
    return sum;
}

/// The heat-weighted mean row: smaller is higher up the texture.
double heatCentre(const std::vector<std::byte>& field) {
    double weighted = 0, total = 0;
    for (std::uint32_t y = 0; y < SIZE; ++y) {
        weighted += double(y) * double(rowHeat(field, y));
        total += double(rowHeat(field, y));
    }
    return total > 0 ? weighted / total : -1;
}

long totalHeat(const std::vector<std::byte>& field) {
    long sum = 0;
    for (std::uint32_t y = 0; y < SIZE; ++y) sum += rowHeat(field, y);
    return sum;
}

} // namespace

TEST_CASE("UTA-0176: a fire still is one palette index a pixel and the same every time", "[ubake][fire]") {
    const std::vector<Spark> sparks{{.type = 4, .heat = 220, .x = 32, .y = 50}}; // Blaze
    const FireSettings settings{.renderHeat = 226, .rising = true, .sparksLimit = 1024};
    const auto first = fireStill(SIZE, SIZE, sparks, settings);
    const auto second = fireStill(SIZE, SIZE, sparks, settings);
    CHECK(first.size() == SIZE * SIZE);
    CHECK(first == second);
    CHECK(totalHeat(first) > 0);
    CHECK(fireStill(0, SIZE, sparks, settings).empty());
}

TEST_CASE("UTA-0176: a fire with no sparks is cold", "[ubake][fire]") {
    const auto field = fireStill(SIZE, SIZE, {}, {.renderHeat = 226, .rising = true, .sparksLimit = 1024});
    CHECK(totalHeat(field) == 0);
}

TEST_CASE("UTA-0176: a spark that releases particles needs room under SparksLimit", "[ubake][fire]") {
    const std::vector<Spark> blaze{{.type = 4, .heat = 220, .x = 32, .y = 50}};
    // A Blaze heats nothing itself: all its heat is in its particles.
    CHECK(totalHeat(fireStill(SIZE, SIZE, blaze, {.renderHeat = 226, .rising = true, .sparksLimit = 1})) == 0);
    CHECK(totalHeat(fireStill(SIZE, SIZE, blaze, {.renderHeat = 226, .rising = true, .sparksLimit = 1024})) > 0);
}

TEST_CASE("UTA-0176: a rising fire's heat sits above its spark and a still one's lower", "[ubake][fire]") {
    const std::vector<Spark> burn{{.type = 0, .heat = 255, .x = 32, .y = 50}}; // Burn, at (32, 50)
    const auto rising = fireStill(SIZE, SIZE, burn, {.renderHeat = 240, .rising = true, .sparksLimit = 0});
    const auto still = fireStill(SIZE, SIZE, burn, {.renderHeat = 240, .rising = false, .sparksLimit = 0});
    REQUIRE(totalHeat(rising) > 0);
    REQUIRE(totalHeat(still) > 0);
    CHECK(heatCentre(rising) < 50);
    CHECK(heatCentre(rising) < heatCentre(still));
}

TEST_CASE("UTA-0176: a lower RenderHeat cools the fire faster", "[ubake][fire]") {
    const std::vector<Spark> burn{{.type = 0, .heat = 255, .x = 32, .y = 50}};
    const long hot = totalHeat(fireStill(SIZE, SIZE, burn, {.renderHeat = 250, .rising = true, .sparksLimit = 0}));
    const long cool = totalHeat(fireStill(SIZE, SIZE, burn, {.renderHeat = 200, .rising = true, .sparksLimit = 0}));
    CHECK(hot > cool);
}
