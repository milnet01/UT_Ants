// Which lights the direct term draws, and SS 4.9's scalar --
// docs/specs/UTA-0014-vulkan-draw-path.md SS 4.6 and SS 4.9.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "urender/Lights.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

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
    light.volumeRadius = 13; // UTA-0015 SS 4.4
    light.volumeBrightness = 21;
    light.volumeFog = 5;
    uta::ubundle::Bundle bundle;
    bundle.lights = std::vector{light};

    const auto drawn = drawnLights(bundle, 0.0);
    CHECK(drawn.at(0).volumeRadius == 13u);
    CHECK(drawn.at(0).volumeBrightness == 21u);
    CHECK(drawn.at(0).volumeFog == 5u);
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

TEST_CASE("UTA-0162 INV-7: an absorbed light is not drawn and a leader draws its segment", "[render]") {
    Light leader = lightOfType(1, 10);
    leader.location = {500, 0, 100};
    leader.strip = uta::ubundle::STRIP_LEADER;
    leader.stripFrom = {0, 0, 100};
    leader.stripTo = {1000, -20, 100};
    Light absorbed = lightOfType(1, 11);
    absorbed.strip = uta::ubundle::STRIP_ABSORBED;
    uta::ubundle::Bundle bundle;
    bundle.lights = std::vector{leader, absorbed, lightOfType(1, 12)};

    CHECK(uta::urender::directLights(bundle).size() == 2u);
    const auto drawn = drawnLights(bundle, 0.0);
    REQUIRE(drawn.size() == 2u);
    CHECK(drawn[0].location == leader.stripFrom);
    CHECK(drawn[0].span == std::array<float, 3>{1000, -20, 0});
    CHECK(drawn[1].location == std::array<float, 3>{});
    CHECK(drawn[1].span == std::array<float, 3>{});
}

TEST_CASE("UTA-0169: a brightness-0 light is not drawn unless it holds a fog volume", "[render]") {
    Light dark = lightOfType(1, 10);
    dark.brightness = 0;
    Light fogOnly = lightOfType(1, 11);
    fogOnly.brightness = 0;
    fogOnly.volumeRadius = 16;
    fogOnly.volumeFog = 40;
    uta::ubundle::Bundle bundle;
    bundle.lights = std::vector{dark, fogOnly, lightOfType(1, 12)};

    const auto direct = uta::urender::directLights(bundle);
    REQUIRE(direct.size() == 2u);
    CHECK(direct[0].exportIndex == 11u);
    CHECK(direct[1].exportIndex == 12u);
    CHECK(drawnLights(bundle, 0.0).size() == 2u);
}

TEST_CASE("SS 4.9: a steady light's scalar is exactly 1 at any time", "[render]") {
    // Steady, and the types this item does not vary: none, the palette types,
    // and a byte past the last ELightType.
    for (const std::uint8_t type : {1, 0, 8, 9, 200}) {
        CAPTURE(type);
        for (const double seconds : {0.0, 0.37, 12.5, 9999.0}) CHECK(flickerOf(lightOfType(type, 1), seconds) == 1.0f);
    }
}

TEST_CASE("UTA-0217: a flicker at a negative or huge time stays within 0 and 1", "[render]") {
    // ut-shot --light-time and pinLightSeconds take any finite time. Run under
    // UBSan: the flicker's step was a negative double cast to an unsigned one.
    const Light flicker = lightOfType(4, 3);
    float lowest = 2, highest = -1;
    for (int step = 0; step < 400; ++step) {
        const float scalar = flickerOf(flicker, -5.0 - step * 0.01);
        lowest = std::min(lowest, scalar);
        highest = std::max(highest, scalar);
    }
    CHECK(lowest >= 0.0f);
    CHECK(highest <= 1.0f);
    CHECK(highest - lowest > 0.05f);
    for (const double seconds : {1e300, -1e300}) {
        const float scalar = flickerOf(flicker, seconds);
        CHECK(scalar >= 0.0f);
        CHECK(scalar <= 1.0f);
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

TEST_CASE("UTA-0263 INV-5: a steady light a flame names flickers with it and no other does", "[render][flames]") {
    // docs/specs/UTA-0263-shader-flames.md SS 4.5. Light 0 is steady and a
    // flame names it; light 1 is steady and none does; light 2 pulses and a
    // flame names it; light 3 is steady and named only by the second of two
    // flames, whose seed it must follow -- the lower-indexed record wins.
    uta::ubundle::Bundle bundle;
    bundle.lights.emplace();
    bundle.lights->push_back(lightOfType(1, 10));
    bundle.lights->push_back(lightOfType(1, 11));
    bundle.lights->push_back(lightOfType(2, 12));
    bundle.lights->push_back(lightOfType(1, 13));
    bundle.flames.emplace();
    const auto flame = [](std::uint32_t seed, std::int32_t light) {
        uta::ubundle::Flame out;
        out.width = 32;
        out.height = 64;
        out.seed = seed;
        out.light = light;
        return out;
    };
    bundle.flames->push_back(flame(5, 0));
    bundle.flames->push_back(flame(6, 2));
    bundle.flames->push_back(flame(7, 3));
    bundle.flames->push_back(flame(8, 3));

    float lowest = 1;
    float highest = 0;
    for (int step = 0; step < 400; ++step) {
        const double seconds = step * 0.01;
        const auto drawn = drawnLights(bundle, seconds);
        REQUIRE(drawn.size() == 4u);
        CHECK(drawn[0].flicker >= 0.8f);
        CHECK(drawn[0].flicker <= 1.0f);
        lowest = std::min(lowest, drawn[0].flicker);
        highest = std::max(highest, drawn[0].flicker);
        CHECK(drawn[1].flicker == 1.0f);
        CHECK(drawn[2].flicker == flickerOf((*bundle.lights)[2], seconds));
        CHECK(drawn[3].flicker == uta::urender::flameFlickerOf(7, seconds));
    }
    // It breathes: over four seconds it moves by more than a twentieth.
    CHECK(highest - lowest > 0.05f);
}

TEST_CASE("UTA-0263: a flame's flicker is smooth and its own", "[render][flames]") {
    // SS 4.5: smooth noise, not LT_FLICKER's twenty jumps a second. At 100
    // samples a second no step moves by more than a fiftieth, and two seeds
    // do not move together: over ten seconds their correlation is weak.
    using uta::urender::flameFlickerOf;
    float largestStep = 0;
    double sumA = 0, sumB = 0, sumAA = 0, sumBB = 0, sumAB = 0;
    constexpr int SAMPLES = 1000;
    for (int step = 1; step <= SAMPLES; ++step) {
        const double seconds = step * 0.01;
        largestStep = std::max(largestStep, std::abs(flameFlickerOf(5, seconds) - flameFlickerOf(5, seconds - 0.01)));
        const double a = flameFlickerOf(5, seconds);
        const double b = flameFlickerOf(6, seconds);
        sumA += a, sumB += b, sumAA += a * a, sumBB += b * b, sumAB += a * b;
    }
    const double covariance = sumAB / SAMPLES - (sumA / SAMPLES) * (sumB / SAMPLES);
    const double spreadA = std::sqrt(sumAA / SAMPLES - (sumA / SAMPLES) * (sumA / SAMPLES));
    const double spreadB = std::sqrt(sumBB / SAMPLES - (sumB / SAMPLES) * (sumB / SAMPLES));
    const double correlation = covariance / (spreadA * spreadB);
    INFO("correlation of seeds 5 and 6: " << correlation);
    CHECK(largestStep < 0.02f);
    CHECK(std::abs(correlation) < 0.4);
    // A pinned time may be negative or huge (UTA-0217).
    for (const double seconds : {-5.0, 1e300, -1e300})
        for (const std::uint32_t seed : {0u, 5u, 0xffffffffu}) {
            const float value = flameFlickerOf(seed, seconds);
            CHECK(value >= 0.8f);
            CHECK(value <= 1.0f);
        }
}
