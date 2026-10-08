// ut-ref's tracer and scorer -- docs/specs/UTA-0292-reference-path-tracer.md
// SS 4.4, INV-8 to INV-11.
//
// THE SCENES ARE BUILT IN MEMORY, from LightFixture.h, as the probe bake's
// cases build theirs; no bundle and no install.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "LightFixture.h"

#include "ubake/LightModel.h"
#include "ut-ref/Reference.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace uta::test::light;
using uta::ref::Reference;
using uta::ubake::MaterialLight;
using uta::ubundle::Light;
namespace ref = uta::ref;

namespace {

constexpr std::uint32_t PF_FAKE_BACKDROP = 0x80;
constexpr std::uint32_t PF_UNLIT = 0x400000;

/// A steady white light of radius byte 64, as the probe cases' light.
Light steadyLight(const Vec3& at) {
    Light light;
    light.exportIndex = 1;
    light.location = {static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z)};
    light.type = 1;
    light.brightness = 255;
    light.saturation = 255;
    light.radius = 64;
    return light;
}

/// A floor at z 0 from -1000 to 1000, facing up.
std::vector<Triangle> floorAt(const std::string& material) {
    return quad({-1000, -1000, 0}, {1000, -1000, 0}, {1000, 1000, 0}, {-1000, 1000, 0}, {0, 0, 1}, material);
}

/// A square at height `z`, `half` across each way about (x, 0), facing down.
std::vector<Triangle> ceilingAt(double x, double z, double half, const std::string& material,
                                std::uint32_t polyFlags = 0) {
    return quad({x - half, -half, z}, {x - half, half, z}, {x + half, half, z}, {x + half, -half, z}, {0, 0, -1},
                material, polyFlags);
}

const Vec3 DOWN{0, 0, -1};

} // namespace

TEST_CASE("UTA-0292 INV-8: the reference's direct light is exact", "[ref]") {
    const Light light = steadyLight({0, 0, 100});
    const double expected = uta::ubake::shownLight(uta::ubake::lightAt(light, {0, 0, 0}, {0, 0, 1}).r);
    REQUIRE(expected > 0.01);

    SECTION("a light reaching the point unblocked") {
        const auto geometry = geometryOf(floorAt("floor"));
        const Reference reference(geometry, {light}, std::nullopt, {});
        const auto pixel = reference.pixel({0, 0, 30}, DOWN, 1, 1, 1);
        CHECK(pixel[ref::LIT] == 1.0F);
        // A white light: every channel alike, so the luma is one channel's.
        CHECK(pixel[ref::DIRECT] == Catch::Approx(expected).epsilon(1e-6));
    }

    SECTION("an occluder between them") {
        auto triangles = floorAt("floor");
        const auto blocker = ceilingAt(0, 60, 20, "blocker");
        triangles.insert(triangles.end(), blocker.begin(), blocker.end());
        const auto geometry = geometryOf(triangles);
        const Reference reference(geometry, {light}, std::nullopt, {});
        const auto pixel = reference.pixel({0, 0, 30}, DOWN, 1, 1, 1);
        CHECK(pixel[ref::LIT] == 1.0F);
        CHECK(pixel[ref::DIRECT] == 0.0F);
    }
}

TEST_CASE("UTA-0292 INV-9: light through the sky view counts as sky", "[ref]") {
    // The floor looks up at a wide sky. The sky view is far to the side, under
    // a glowing lava ceiling that only a ray leaving the sky view can reach --
    // the probe case "sky light"'s scene turned over. There are no lights.
    auto triangles = floorAt("floor");
    const auto sky = ceilingAt(0, 1000, 1e6, "sky", PF_FAKE_BACKDROP);
    const auto lava = ceilingAt(5e6, -5000, 1e6, "lava", PF_UNLIT);
    triangles.insert(triangles.end(), sky.begin(), sky.end());
    triangles.insert(triangles.end(), lava.begin(), lava.end());
    const auto geometry = geometryOf(triangles);
    std::vector<MaterialLight> materials(1);
    materials[0].id = "lava";
    materials[0].albedo = {0.2, 0.2, 0.2};
    materials[0].own.unlitGlows = true;

    SECTION("with a sky view") {
        const Reference reference(geometry, {}, Vec3{5e6, 0, -10000}, materials);
        const auto pixel = reference.pixel({0, 0, 30}, DOWN, 64, 1, 7);
        CHECK(pixel[ref::SKY] > 0.7F); // 4 times the lava's 0.2, on nearly every ray
        CHECK(pixel[ref::FIRST] == 0.0F);
    }

    SECTION("with no sky view") {
        const Reference reference(geometry, {}, std::nullopt, materials);
        const auto pixel = reference.pixel({0, 0, 30}, DOWN, 64, 1, 7);
        CHECK(pixel[ref::SKY] == 0.0F);
        CHECK(pixel[ref::FIRST] == 0.0F);
    }
}

TEST_CASE("UTA-0292 INV-10: a trace is the same at any worker count", "[ref]") {
    const auto geometry = geometryOf(room(
        {-512, -512, -512}, {512, 512, 512},
        {Face{"white"}, Face{"white"}, Face{"white"}, Face{"white"}, Face{"white"}, Face{"white"}}));
    const Reference reference(geometry, {steadyLight({0, 0, 400})}, std::nullopt, {});
    const uta::urender::Camera camera;
    const auto one = reference.trace(camera, 90, 24, 12, 4, 3, 1);
    const auto four = reference.trace(camera, 90, 24, 12, 4, 3, 4);
    // The room is lit and bounces, so the streams matter to every pixel.
    double later = 0;
    for (std::size_t i = ref::LATER; i < one.size(); i += ref::CHANNELS) later += one[i];
    REQUIRE(later > 0);
    CHECK(one == four);
}

TEST_CASE("UTA-0292 INV-11: the score reads an equal renderer as no gap", "[ref]") {
    constexpr std::uint32_t W = 16, H = 8; // two blocks
    std::vector<float> reference(std::size_t{W} * H * ref::CHANNELS, 0.0F);
    std::vector<float> terms(std::size_t{W} * H * 3, 0.0F);
    const auto fill = [&](float scale) {
        for (std::uint32_t y = 0; y < H; ++y)
            for (std::uint32_t x = 0; x < W; ++x) {
                const std::size_t at = std::size_t{y} * W + x;
                // The second block's top row is not lit: averaged over its
                // block, it would pull that block's mean light below 0.5.
                const bool lit = !(x >= 8 && y == 0);
                float* r = &reference[at * ref::CHANNELS];
                r[ref::DIRECT] = lit ? 0.3F : 0.0F;
                r[ref::FIRST] = lit ? 0.1F : 0.0F;
                r[ref::SKY] = lit ? 0.1F : 0.0F;
                r[ref::LIT] = lit ? 1.0F : 0.0F;
                terms[at * 3] = lit ? 0.3F * scale : 0.0F;
                terms[at * 3 + 1] = lit ? 0.2F * scale : 0.0F;
            }
    };

    fill(1);
    const ref::Score same = ref::scoreView(reference, terms, W, H);
    CHECK(same.blocks == 2u);
    // Each block's mean is over its lit pixels: 0.3 + 0.1 + 0.1 in both.
    CHECK(same.reference == Catch::Approx(0.5).epsilon(1e-6));
    CHECK(same.total == Catch::Approx(0).margin(1e-7));
    CHECK(same.direct == Catch::Approx(0).margin(1e-7));
    CHECK(same.indirect == Catch::Approx(0).margin(1e-7));
    CHECK(same.stops == Catch::Approx(0).margin(1e-7));
    CHECK(same.sky == Catch::Approx(0.2).epsilon(1e-6));

    fill(2);
    const ref::Score twice = ref::scoreView(reference, terms, W, H);
    CHECK(twice.total == Catch::Approx(1).epsilon(1e-6));
    CHECK(twice.stops == Catch::Approx(1).epsilon(1e-6));
}
