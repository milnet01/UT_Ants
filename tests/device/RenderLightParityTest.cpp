// UTA-0014 INV-6 -- docs/specs/UTA-0014-vulkan-draw-path.md SS 4.6.
//
// The shading pass's direct light equals ubake::lightAt within SS 4.6's fixed
// 1e-3, over a case table covering each light effect the model distinguishes
// -- LE_None, LE_StaticSpot (8), LE_Spotlight (12), LE_NonIncidence (13) --
// and the d == 0 case UTA-0112 SS 4.3 singles out.
//
// THE REAL SHADER. The kernel includes shaders/light.glsl, the file the shading
// pass includes, and the lights reach it through urender::drawnLights, the
// conversion the renderer uploads with. No C++ copy of the model exists to
// grade instead (SS 3 decision 5).
//
// 1e-3 IS A CEILING SET BY THE SPEC, NOT CALIBRATED HERE. A measured deviation
// above it fails, and raising it is a finding rather than a fix.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/ComputeFixture.h"
#include "device/DeviceFixture.h"
#include "ubake/LightModel.h"
#include "urender/Lights.h"
#include "urender/ShaderTypes.h"

#include "light_parity.comp.spv.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <map>
#include <vector>

using uta::ubake::Vec3;
using uta::ubundle::Light;
namespace gpu = uta::urender::gpu;

namespace {

struct LightCase {
    gpu::Light light;
    std::array<float, 4> x;
    std::array<float, 4> n;
};
static_assert(sizeof(LightCase) == 96);
static_assert(offsetof(LightCase, x) == 64);
static_assert(offsetof(LightCase, n) == 80);

constexpr double TOLERANCE = 1e-3; // SS 4.6

std::array<float, 3> normalised(double x, double y, double z) {
    const double length = std::sqrt(x * x + y * y + z * z);
    return {static_cast<float>(x / length), static_cast<float>(y / length), static_cast<float>(z / length)};
}

} // namespace

TEST_CASE("INV-6: the shading pass's light equals ubake's lightAt within 1e-3", "[device]") {
    uta::test::render::removeDisplay();

    const std::array<std::uint8_t, 4> effects = {0, 8, 12, 13};
    const std::array<std::uint8_t, 6> hues = {0, 43, 100, 170, 213, 255};
    const std::array<std::uint8_t, 3> saturations = {0, 90, 255};
    const std::array<std::uint8_t, 4> cones = {0, 32, 128, 250};
    const std::array<std::array<std::int32_t, 3>, 4> rotations = {{{0, 0, 0}, {4096, 12000, 0}, {-8000, 40000, 900},
                                                                   {16384, 0, 0}}};
    // How far along the radius the point sits: at the light, inside, near the
    // edge, and beyond it.
    const std::array<double, 5> reach = {0.0, 0.1, 0.5, 0.97, 1.2};

    uta::ubundle::Bundle bundle;
    bundle.lights.emplace();
    std::vector<std::array<float, 3>> points;
    std::vector<std::array<float, 3>> normals;
    std::size_t index = 0;
    for (const std::uint8_t effect : effects) {
        for (std::size_t variant = 0; variant < 60; ++variant, ++index) {
            Light light;
            light.exportIndex = static_cast<std::uint32_t>(index);
            light.type = 1; // steady: drawnLights gives it a scalar of exactly 1
            light.effect = effect;
            light.hue = hues[variant % hues.size()];
            light.saturation = saturations[(variant / 2) % saturations.size()];
            light.brightness = variant % 3 == 0 ? 255 : static_cast<std::uint8_t>(40 + variant);
            light.radius = static_cast<std::uint8_t>(variant % 2 == 0 ? 12 : 200);
            light.cone = cones[(variant / 3) % cones.size()];
            light.rotation = rotations[(variant / 5) % rotations.size()];
            light.location = {static_cast<float>(100 + 13 * variant), static_cast<float>(-250 + 7 * variant), 64.0f};

            // A direction from the light toward the point, and a normal
            // facing it, across it, or away from it.
            const double radius = uta::ubake::lightRadius(light.radius);
            const std::array<float, 3> away = normalised(std::cos(variant * 0.7), std::sin(variant * 0.7),
                                                         std::sin(variant * 1.3) * 0.6);
            const double distance = reach[variant % reach.size()] * radius;
            points.push_back({static_cast<float>(light.location[0] + away[0] * distance),
                              static_cast<float>(light.location[1] + away[1] * distance),
                              static_cast<float>(light.location[2] + away[2] * distance)});
            switch (variant % 3) {
            case 0: normals.push_back(normalised(-away[0], -away[1], -away[2])); break;
            case 1: normals.push_back(normalised(-away[1], away[0], 0.3)); break;
            default: normals.push_back(normalised(away[0], away[1], away[2])); break;
            }
            bundle.lights->push_back(light);
        }
    }

    const std::vector<gpu::Light> uploaded = uta::urender::drawnLights(bundle, 0.0);
    REQUIRE(uploaded.size() == bundle.lights->size());
    std::vector<LightCase> cases;
    for (std::size_t i = 0; i < uploaded.size(); ++i)
        cases.push_back({uploaded[i], {points[i][0], points[i][1], points[i][2], 1}, {normals[i][0], normals[i][1],
                                                                                      normals[i][2], 0}});

    const std::vector<std::byte> output = uta::test::render::runCompute(
        light_parity_comp_spv, {std::as_bytes(std::span(cases))}, cases.size() * 4 * sizeof(float),
        static_cast<std::uint32_t>(cases.size()));

    double worst = 0;
    std::map<std::uint8_t, std::size_t> lit; // per effect, cases the light reached
    std::size_t atTheLight = 0;
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const Light& light = (*bundle.lights)[i];
        // The CPU sees exactly the floats the GPU saw.
        const Vec3 x{points[i][0], points[i][1], points[i][2]};
        const Vec3 n{normals[i][0], normals[i][1], normals[i][2]};
        const uta::ubake::Rgb expected = uta::ubake::lightAt(light, x, n);
        std::array<float, 4> actual{};
        std::memcpy(actual.data(), output.data() + i * sizeof(actual), sizeof(actual));

        // As ints: Catch2 prints a uint8_t as a character, not a number.
        CAPTURE(i, int(light.effect), int(light.hue), int(light.saturation), int(light.brightness),
                int(light.radius), int(light.cone));
        CAPTURE(expected.r, expected.g, expected.b, actual[0], actual[1], actual[2]);
        CHECK(std::abs(actual[0] - expected.r) <= TOLERANCE);
        CHECK(std::abs(actual[1] - expected.g) <= TOLERANCE);
        CHECK(std::abs(actual[2] - expected.b) <= TOLERANCE);
        worst = std::max({worst, std::abs(actual[0] - expected.r), std::abs(actual[1] - expected.g),
                          std::abs(actual[2] - expected.b)});
        if (expected.r + expected.g + expected.b > 0) ++lit[light.effect];
        if (points[i] == light.location) ++atTheLight;
    }
    INFO("worst deviation " << worst);

    // The table is not vacuous: every effect lit something, and d == 0 is in it.
    for (const std::uint8_t effect : effects) {
        CAPTURE(int(effect));
        CHECK(lit[effect] >= 10);
    }
    CHECK(atTheLight >= 4);
}
