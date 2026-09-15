// UTA-0015's device-free cases: which volumetric lights glow, and the
// flashlight -- docs/specs/UTA-0015-volumetric-fog.md SS 4.4, SS 4.5, INV-3 and
// INV-4. What the pixels show is tests/device/RenderFogTest.cpp's.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "urender/Fog.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using uta::ubundle::Zone;
using uta::urender::Camera;
using uta::urender::volumeLights;
namespace gpu = uta::urender::gpu;

namespace {

gpu::Light glowing(float x, std::uint32_t volumeRadius) {
    gpu::Light light{};
    light.location = {x, 0.0f, 0.0f};
    light.volumeRadius = volumeRadius;
    return light;
}

const std::vector<Zone> CLEAR_THEN_FOG{Zone{0, 0, 0, 0}, Zone{0, 0, 0, 1}, Zone{0, 0, 0, 1}};
constexpr std::array<float, 3> EYE{0.0f, 0.0f, 0.0f};

} // namespace

TEST_CASE("UTA-0015 INV-3: no light glows while the camera is outside a fog zone", "[render]") {
    const std::vector<gpu::Light> lights{glowing(100, 5), glowing(200, 5)};
    const std::vector<std::uint8_t> zones{1, 2};
    const auto choice = volumeLights(lights, zones, 0, CLEAR_THEN_FOG, EYE);
    CHECK(choice.indices.empty());
    CHECK(choice.dropped == 0u);
    CHECK(volumeLights(lights, zones, 1, CLEAR_THEN_FOG, EYE).indices.size() == 2u);
}

TEST_CASE("UTA-0015 INV-3: in a fog zone the volumetric lights in fog zones glow nearest first", "[render]") {
    const std::vector<gpu::Light> lights{glowing(300, 5), glowing(100, 5), glowing(50, 5), glowing(10, 0)};
    // Light 2 is nearest but in the clear zone; light 3 is nearer still but has no volume radius.
    const std::vector<std::uint8_t> zones{1, 2, 0, 1};
    const auto choice = volumeLights(lights, zones, 1, CLEAR_THEN_FOG, EYE);
    CHECK(choice.indices == std::vector<std::uint32_t>{1, 0});
    CHECK(choice.dropped == 0u);
}

TEST_CASE("UTA-0015 INV-3: a zone index past the zones reads as zone 0", "[render]") {
    const std::vector<Zone> fogThenClear{Zone{0, 0, 0, 1}, Zone{0, 0, 0, 0}};
    const std::vector<gpu::Light> lights{glowing(100, 5), glowing(200, 5)};
    // The camera's zone 7 and light 0's zone 9 are both zone 0, which has fog;
    // light 1 has no zone at all, which is zone 0 too.
    const std::vector<std::uint8_t> zones{9};
    const auto choice = volumeLights(lights, zones, 7, fogThenClear, EYE);
    CHECK(choice.indices == std::vector<std::uint32_t>{0, 1});
    CHECK(volumeLights(lights, zones, 1, fogThenClear, EYE).indices.empty());
}

TEST_CASE("UTA-0015 INV-3: past the capacity the furthest lights are dropped", "[render]") {
    std::vector<gpu::Light> lights;
    std::vector<std::uint8_t> zones;
    // Furthest first, so keeping the first 64 in order would keep the wrong ones.
    for (int i = 69; i >= 0; --i) {
        lights.push_back(glowing(static_cast<float>(10 * (i + 1)), 5));
        zones.push_back(1);
    }
    const auto choice = volumeLights(lights, zones, 1, CLEAR_THEN_FOG, EYE);
    REQUIRE(choice.indices.size() == uta::urender::VOLUME_LIGHT_CAPACITY);
    CHECK(choice.dropped == 6u);
    CHECK(choice.indices.front() == 69u); // the light at 10 units
    CHECK(choice.indices.back() == 6u);   // the light at 640 units
}

TEST_CASE("UTA-0015 INV-4: the flashlight is a spotlight from the eye in FlashLightBeam's colour", "[render]") {
    Camera camera;
    camera.location = {1.5f, -2.0f, 30.0f};
    camera.rotation = {1024, -2048, 512};
    const gpu::Light light = uta::urender::flashlightOf(camera);
    CHECK(light.location == camera.location);
    CHECK(light.pitch == 1024);
    CHECK(light.yaw == -2048);
    CHECK(light.effect == 12u);
    CHECK(light.hue == 32u);
    CHECK(light.saturation == 142u);
    CHECK(light.brightness == 250u);
    CHECK(light.radius == 255u);
    CHECK(light.cone == 18u);
    CHECK(light.flicker == 1.0f);
    CHECK(light.shadowFaceCount == 0u);
    CHECK(light.volumeRadius == 0u);
    CHECK(light.volumeBrightness == 0u);
    CHECK(light.volumeFog == 0u);
}
