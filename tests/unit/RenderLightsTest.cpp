// Which lights the direct term draws, and SS 4.9's scalar --
// docs/specs/UTA-0014-vulkan-draw-path.md SS 4.6 and SS 4.9.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "urender/Lights.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using uta::ubundle::Light;
using uta::urender::drawnLights;
using uta::urender::flickerOf;

namespace {

Light lightOfType(std::uint8_t type, std::uint32_t exportIndex) {
    Light light;
    light.exportIndex = exportIndex;
    light.type = type;
    light.brightness = 200;
    light.saturation = 255;
    light.radius = 32;
    return light;
}

} // namespace

TEST_CASE("every LITE light draws but backdrop and special-lit lights", "[render]") {
    uta::ubundle::Bundle bundle;
    bundle.lights.emplace();
    bundle.lights->push_back(lightOfType(1, 10));  // steady
    bundle.lights->push_back(lightOfType(6, 11));  // LT_BackdropLight: lights only the sky
    Light special = lightOfType(1, 12);
    special.specialLit = true;                     // lights only PF_SpecialLit surfaces
    bundle.lights->push_back(special);
    bundle.lights->push_back(lightOfType(2, 13));  // pulsing
    bundle.lights->push_back(lightOfType(200, 14)); // past the last ELightType

    const auto drawn = drawnLights(bundle, 0.0);
    REQUIRE(drawn.size() == 3u);
    CHECK(drawn[0].brightness == 200u);
    CHECK(drawn[1].shadowFace == -1);
}

TEST_CASE("a drawn light carries its LITE numbers unconverted", "[render]") {
    // SS 3 decision 5: the shader turns UT99's numbers into light, so nothing
    // on the CPU may have turned them into anything else first.
    Light light = lightOfType(1, 7);
    light.location = {12.5f, -8.0f, 300.25f};
    light.rotation = {1024, -2048, 4096};
    light.effect = 12;
    light.hue = 77;
    light.saturation = 91;
    light.brightness = 150;
    light.radius = 44;
    light.cone = 33;
    uta::ubundle::Bundle bundle;
    bundle.lights = std::vector{light};

    const auto drawn = drawnLights(bundle, 0.0);
    REQUIRE(drawn.size() == 1u);
    CHECK(drawn[0].location == light.location);
    CHECK(drawn[0].pitch == 1024);
    CHECK(drawn[0].yaw == -2048);
    CHECK(drawn[0].effect == 12u);
    CHECK(drawn[0].hue == 77u);
    CHECK(drawn[0].saturation == 91u);
    CHECK(drawn[0].brightness == 150u);
    CHECK(drawn[0].radius == 44u);
    CHECK(drawn[0].cone == 33u);
    CHECK(drawn[0].flicker == 1.0f);
}

TEST_CASE("SS 4.9: a steady light's scalar is exactly 1 at any time", "[render]") {
    // Steady, and the types this item does not vary: none, the palette types,
    // and a byte past the last ELightType.
    for (const std::uint8_t type : {1, 0, 8, 9, 200}) {
        CAPTURE(type);
        for (const double seconds : {0.0, 0.37, 12.5, 9999.0}) CHECK(flickerOf(lightOfType(type, 1), seconds) == 1.0f);
    }
}

TEST_CASE("SS 4.9: a varying light varies within 0 and 1", "[render]") {
    for (const std::uint8_t type : {2, 3, 4, 5, 7}) {
        CAPTURE(type);
        float lowest = 2, highest = -1;
        for (int step = 0; step < 400; ++step) {
            const float scalar = flickerOf(lightOfType(type, 3), step * 0.01);
            lowest = std::min(lowest, scalar);
            highest = std::max(highest, scalar);
        }
        CHECK(lowest >= 0.0f);
        CHECK(highest <= 1.0f);
        CHECK(highest - lowest > 0.05f);
    }
}
