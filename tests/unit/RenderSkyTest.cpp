// UTA-0163: the level's sky, the parts that need no device.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "urender/Sky.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>

using Catch::Matchers::WithinAbs;
namespace urender = uta::urender;
namespace ubundle = uta::ubundle;

namespace {

std::array<double, 4> transform(const urender::gpu::Mat4& m, const std::array<double, 4>& p) {
    std::array<double, 4> out{};
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column) out[row] += m[column * 4 + row] * p[column];
    return out;
}

ubundle::PropertyRecord locationOf(std::array<float, 3> where) {
    return {.name = "Location", .kind = ubundle::ValueKind::Vector, .value = where};
}

ubundle::PropertyRecord highDetail(bool on) {
    return {.name = "bHighDetail", .kind = ubundle::ValueKind::Bool, .value = on};
}

/// A bundle whose placements are `actors`, each of class 0 (a SkyZoneInfo
/// subclass, by ancestry) or class 1 (a plain ZoneInfo).
ubundle::Bundle levelWith(std::vector<ubundle::ActorPlacement> actors) {
    ubundle::Placements placements;
    placements.classes = {
        {.path = "mymod.cloudsky", .ancestry = {"engine.skyzoneinfo", "engine.zoneinfo", "engine.info"}},
        {.path = "engine.zoneinfo", .ancestry = {"engine.info"}}};
    for (std::size_t i = 0; i < actors.size(); ++i) actors[i].exportIndex = static_cast<std::uint32_t>(i + 1);
    placements.actors = std::move(actors);
    ubundle::Bundle bundle;
    bundle.placements = std::move(placements);
    return bundle;
}

} // namespace

TEST_CASE("UTA-0163: a level with no SkyZoneInfo has no sky", "[render]") {
    CHECK_FALSE(urender::skyViewOf(ubundle::Bundle{}).has_value());
    const auto bundle = levelWith({{.classIndex = 1, .properties = {locationOf({1, 2, 3})}}});
    CHECK_FALSE(urender::skyViewOf(bundle).has_value());
}

TEST_CASE("UTA-0163: the sky is the last SkyZoneInfo and then the last in high detail", "[render]") {
    SECTION("none in high detail: the last one") {
        const auto bundle = levelWith({{.classIndex = 0, .properties = {locationOf({1, 0, 0})}},
                                       {.classIndex = 1, .properties = {locationOf({9, 9, 9})}},
                                       {.classIndex = 0, .properties = {locationOf({2, 0, 0})}}});
        const auto sky = urender::skyViewOf(bundle);
        REQUIRE(sky.has_value());
        CHECK(sky->location == std::array<float, 3>{2, 0, 0});
    }
    SECTION("one in high detail: it wins over a later one") {
        const auto bundle = levelWith({{.classIndex = 0, .properties = {locationOf({1, 0, 0}), highDetail(true)}},
                                       {.classIndex = 0, .properties = {locationOf({2, 0, 0})}}});
        const auto sky = urender::skyViewOf(bundle);
        REQUIRE(sky.has_value());
        CHECK(sky->location == std::array<float, 3>{1, 0, 0});
    }
    SECTION("the class default counts when the actor does not set it") {
        auto bundle = levelWith({{.classIndex = 0, .properties = {locationOf({1, 0, 0})}},
                                 {.classIndex = 0, .properties = {locationOf({2, 0, 0}), highDetail(false)}}});
        bundle.placements->classes[0].defaults = {highDetail(true)};
        const auto sky = urender::skyViewOf(bundle);
        REQUIRE(sky.has_value());
        CHECK(sky->location == std::array<float, 3>{1, 0, 0});
    }
}

TEST_CASE("UTA-0163: each sky face looks along its own axis with up at the top", "[render]") {
    constexpr std::array<std::array<double, 3>, 6> AXES{
        {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    for (std::uint32_t face = 0; face < 6; ++face) {
        CAPTURE(face);
        const urender::gpu::ShadowFace sample = urender::skyFaceSample(face);
        const auto& axis = AXES[face];
        const auto centre = transform(sample.viewProj, {axis[0] * 100, axis[1] * 100, axis[2] * 100, 1});
        REQUIRE(centre[3] > 0);
        CHECK_THAT(centre[0] / centre[3], WithinAbs(0, 1e-4));
        CHECK_THAT(centre[1] / centre[3], WithinAbs(0, 1e-4));
        // 45 degrees off the axis is the square's edge: NDC 1.
        if (face < 4) {
            const auto above = transform(sample.viewProj, {axis[0] * 100, axis[1] * 100, 100, 1});
            CHECK_THAT(above[1] / above[3], WithinAbs(-1, 1e-4)); // Vulkan's +Y is down
        }
        // The face's rectangle is its cell of the three-by-two texture.
        CHECK_THAT(sample.atlasRect[0], WithinAbs(static_cast<float>(face % 3) / 3, 1e-6));
        CHECK_THAT(sample.atlasRect[1], WithinAbs(static_cast<float>(face / 3) / 2, 1e-6));
        CHECK_THAT(sample.atlasRect[2], WithinAbs(1.0 / 3, 1e-6));
        CHECK_THAT(sample.atlasRect[3], WithinAbs(0.5, 1e-6));
    }
}

TEST_CASE("UTA-0163: a face camera's shorter side spans 90 degrees", "[render]") {
    const urender::SkyView sky{{5, 6, 7}};
    SECTION("wide") {
        const urender::Camera camera = urender::skyFaceCamera(sky, 2, 1920, 1080);
        CHECK(camera.location == sky.location);
        CHECK(camera.rotation == urender::skyFaceRotation(2));
        CHECK_THAT(camera.verticalFovDegrees, WithinAbs(90, 1e-3));
    }
    SECTION("tall") {
        const urender::Camera camera = urender::skyFaceCamera(sky, 0, 1000, 2000);
        CHECK_THAT(camera.verticalFovDegrees, WithinAbs(2 * std::atan(2.0) * 180 / 3.14159265358979, 1e-3));
    }
}
