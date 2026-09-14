// UTA-0162's row detection -- docs/specs/UTA-0162-strip-lights.md SS 4.2,
// INV-2, INV-3 and INV-4.
//
// EACH INV-3 SECTION STARTS FROM row(), which every rule accepts, and breaks
// exactly one rule, so that rule alone is what refuses it.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "ubake/Strips.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

using uta::ubake::markStrips;
using uta::ubundle::Light;
using uta::ubundle::STRIP_ABSORBED;
using uta::ubundle::STRIP_LEADER;
using uta::ubundle::STRIP_NONE;

namespace {

constexpr std::uint8_t LE_SPOTLIGHT = 12;
constexpr std::uint8_t LE_CYLINDER = 17;
constexpr std::array<float, 3> ZERO{};

/// A steady white LE_Cylinder light of radius byte 10, so R = 275 and the gap
/// bound is 412.5.
Light cylinder(std::uint32_t exportIndex, float x, float y) {
    Light light;
    light.exportIndex = exportIndex;
    light.location = {x, y, 300.0F};
    light.type = 1;
    light.effect = LE_CYLINDER;
    light.brightness = 160;
    light.saturation = 255;
    light.radius = 10;
    return light;
}

/// INV-2's row: gaps of 250 and 300 along x, the middle light 10 units off the
/// line through the ends.
std::vector<Light> row() {
    return {cylinder(3, 0, 0), cylinder(5, 250, 10), cylinder(7, 550, 0)};
}

} // namespace

TEST_CASE("INV-2: an uneven and slightly crooked row is one strip", "[ubake][strips]") {
    std::vector<Light> lights = row();
    const std::vector<Light> before = lights;
    markStrips(lights);

    CHECK(lights[0].strip == STRIP_LEADER);
    CHECK(lights[0].stripFrom == before[0].location);
    CHECK(lights[0].stripTo == before[2].location);
    for (const std::size_t i : {std::size_t{1}, std::size_t{2}}) {
        CAPTURE(i);
        CHECK(lights[i].strip == STRIP_ABSORBED);
        CHECK(lights[i].stripFrom == ZERO);
        CHECK(lights[i].stripTo == ZERO);
    }
    // No location moves.
    for (std::size_t i = 0; i < lights.size(); ++i) CHECK(lights[i].location == before[i].location);
}

TEST_CASE("INV-3: a row that breaks one rule is no strip", "[ubake][strips]") {
    std::vector<Light> lights = row();
    SECTION("two lights only") {
        lights.pop_back();
    }
    SECTION("a gap of 1.5 R plus 1") {
        lights[2].location[0] = 250.0F + 412.5F + 1.0F;
    }
    SECTION("the middle light 17 units off the line") {
        lights[1].location[1] = 17.0F;
    }
    SECTION("one light's brightness different") {
        lights[1].brightness = 161;
    }
    SECTION("a row of spotlights") {
        for (Light& light : lights) light.effect = LE_SPOTLIGHT;
    }
    markStrips(lights);
    for (const Light& light : lights) {
        CAPTURE(light.exportIndex);
        CHECK(light.strip == STRIP_NONE);
        CHECK(light.stripFrom == ZERO);
        CHECK(light.stripTo == ZERO);
    }
}

TEST_CASE("INV-4: a row of five is one strip and a shared light goes to the larger row", "[ubake][strips]") {
    SECTION("a row of five") {
        std::vector<Light> lights = {cylinder(1, 0, 0), cylinder(2, 250, 5), cylinder(3, 480, -5),
                                     cylinder(4, 760, 0), cylinder(5, 1000, 3)};
        markStrips(lights);
        CHECK(lights[0].strip == STRIP_LEADER);
        CHECK(lights[0].stripTo == lights[4].location);
        for (std::size_t i = 1; i < lights.size(); ++i) {
            CAPTURE(i);
            CHECK(lights[i].strip == STRIP_ABSORBED);
        }
    }
    SECTION("two rows sharing a light") {
        // Along x: exports 1, 2, 4 and 6. Along y from export 4: exports 7 and 8.
        std::vector<Light> lights = {cylinder(1, 0, 0),   cylinder(2, 250, 0),   cylinder(4, 500, 0),
                                     cylinder(6, 750, 0), cylinder(7, 500, 250), cylinder(8, 500, 500)};
        markStrips(lights);
        CHECK(lights[0].strip == STRIP_LEADER);
        CHECK(lights[0].stripTo == lights[3].location);
        for (const std::size_t i : {std::size_t{1}, std::size_t{2}, std::size_t{3}}) {
            CAPTURE(i);
            CHECK(lights[i].strip == STRIP_ABSORBED);
        }
        CHECK(lights[4].strip == STRIP_NONE);
        CHECK(lights[5].strip == STRIP_NONE);
    }
}
