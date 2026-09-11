// UTA-0112's baker cases -- docs/specs/UTA-0112-baked-light-probes.md SS 4.4
// to SS 4.7: INV-5's albedo half and INV-6 to INV-10.
//
// THE ROOMS ARE BUILT IN MEMORY. tests/unit/LightFixture.h makes GEOM, and
// PathFixture.h's worldOf makes the matching COLL tree, one region per box.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "LightFixture.h"
#include "PathFixture.h"

#include "core/Jobs.h"
#include "ubake/LightModel.h"
#include "ubake/LightProbes.h"
#include "ubake/SurfaceRays.h"
#include "ubundle/Bundle.h"
#include "umat/Material.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

using uta::JobSystem;
using uta::test::light::Face;
using uta::test::light::geometryOf;
using uta::test::light::quad;
using uta::test::light::room;
using uta::test::light::Triangle;
using uta::test::paths::box;
using uta::test::paths::worldOf;
using uta::ubake::AlbedoLookup;
using uta::ubake::bakedLights;
using uta::ubake::bakeLightProbes;
using uta::ubake::cubeOf;
using uta::ubake::directions;
using uta::ubake::gatherProbe;
using uta::ubake::meanAlbedo;
using uta::ubake::Rgb;
using uta::ubake::SurfaceRays;
using uta::ubake::Vec3;
using uta::ubundle::ActorClass;
using uta::ubundle::ActorPlacement;
using uta::ubundle::Light;
using uta::ubundle::Placements;
using uta::ubundle::PropertyRecord;
using uta::ubundle::ValueKind;

namespace {

constexpr std::uint32_t PF_TRANSLUCENT = 0x04;
constexpr std::uint32_t PF_FAKE_BACKDROP = 0x80;
constexpr std::uint32_t PF_TWO_SIDED = 0x100;

using Cell = std::array<std::int32_t, 3>;

/// A steady white light of radius byte 64, a reach of 1625 units.
Light steadyLight(std::uint32_t exportIndex, const Vec3& at, std::uint8_t brightness = 255) {
    Light light;
    light.exportIndex = exportIndex;
    light.location = {static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z)};
    light.type = 1;
    light.brightness = brightness;
    light.saturation = 255;
    light.radius = 64;
    return light;
}

AlbedoLookup albedoOf(std::map<std::string, Rgb> table) {
    return [table = std::move(table)](std::string_view material) {
        const auto found = table.find(std::string(material));
        return found == table.end() ? Rgb{0.5, 0.5, 0.5} : found->second;
    };
}

double total(const std::array<Rgb, 6>& cube) {
    double sum = 0;
    for (const Rgb& face : cube) sum += face.r + face.g + face.b;
    return sum;
}

std::vector<Cell> cellsOf(const uta::ubundle::LightProbes& probes) {
    std::vector<Cell> out;
    for (const auto& probe : probes.probes) out.push_back(probe.cell);
    return out;
}

PropertyRecord staticFlag(bool value) {
    PropertyRecord record;
    record.name = "bStatic";
    record.kind = ValueKind::Bool;
    record.value = value;
    return record;
}

/// A floor quad at z 0 spanning -1000 to 1000, facing up.
std::vector<Triangle> floorQuad(const std::string& material, std::uint32_t polyFlags = 0) {
    return quad({-1000, -1000, 0}, {1000, -1000, 0}, {1000, 1000, 0}, {-1000, 1000, 0},
                {0, 0, 1}, material, polyFlags);
}

} // namespace

TEST_CASE("albedo", "[ubake][probes]") {
    uta::umat::Image image;
    image.width = 2;
    image.height = 1;
    image.channels = 4;
    // An opaque red pixel, and a blue one whose alpha is 0.
    image.pixels = {std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255},
                    std::byte{0},   std::byte{0}, std::byte{255}, std::byte{0}};
    const auto masked = meanAlbedo(image);
    REQUIRE(masked.has_value());
    CHECK(masked->r == 1.0);
    CHECK(masked->g == 0.0);
    CHECK(masked->b == 0.0);

    image.pixels[7] = std::byte{255};
    const auto opaque = meanAlbedo(image);
    REQUIRE(opaque.has_value());
    CHECK(opaque->r == 0.5);
    CHECK(opaque->g == 0.0);
    CHECK(opaque->b == 0.5);

    image.pixels[3] = std::byte{0};
    image.pixels[7] = std::byte{0};
    CHECK_FALSE(meanAlbedo(image).has_value());
}

TEST_CASE("which lights bake", "[ubake][probes]") {
    Placements placements;
    placements.classes = {
        ActorClass{"engine.light", true, {"engine.actor"}, {}, "", {staticFlag(true)}},
        ActorClass{"engine.triggerlight", true, {"engine.light", "engine.actor"}, {}, "",
                   {staticFlag(false)}},
    };
    placements.actors = {
        ActorPlacement{10, "map.light0", 0, {}},
        ActorPlacement{11, "map.light1", 0, {staticFlag(false)}},
        ActorPlacement{12, "map.triggerlight0", 1, {}},
        ActorPlacement{13, "map.light2", 0, {}},
        ActorPlacement{14, "map.light3", 0, {}},
    };

    std::vector<Light> lights;
    lights.push_back(steadyLight(10, {0, 0, 0}));   // static by its class: kept
    lights.push_back(steadyLight(11, {0, 0, 0}));   // its own bStatic false wins
    lights.push_back(steadyLight(12, {0, 0, 0}));   // a TriggerLight, not static
    lights.push_back(steadyLight(13, {0, 0, 0}));
    lights.back().type = 6;                          // LT_BackdropLight
    lights.push_back(steadyLight(14, {0, 0, 0}));
    lights.back().specialLit = true;
    // No placement. Slot 9 sorts before every actor, so a search that lands on
    // the next slot finds 10, which is static: only the slot check drops it.
    lights.push_back(steadyLight(9, {0, 0, 0}));

    const std::vector<Light> baked = bakedLights(lights, placements);
    REQUIRE(baked.size() == 1);
    CHECK(baked[0].exportIndex == 10);
}

TEST_CASE("placement", "[ubake][probes]") {
    const AlbedoLookup grey = albedoOf({});
    JobSystem jobs(2);

    SECTION("a box room and a sky far from it") {
        std::vector<Triangle> triangles =
            room({16, 16, 16}, {400, 400, 400}, {Face{"wall"}, Face{"wall"}, Face{"wall"},
                                                  Face{"wall"}, Face{"wall"}, Face{"wall"}});
        // A sky triangle inside a second empty box, so its candidates would be
        // accepted by isEmpty if it seeded any.
        triangles.push_back(Triangle{{Vec3{1100, 1100, 1100}, Vec3{1300, 1100, 1100},
                                      Vec3{1100, 1300, 1100}},
                                     {0, 0, 1}, "sky", PF_FAKE_BACKDROP});
        const auto tree = worldOf({box({16, 16, 16}, {400, 400, 400}),
                                   box({1000, 1000, 1000}, {1400, 1400, 1400})},
                                  {0, 0, 0}, {1400, 1400, 1400});
        const auto baked = bakeLightProbes(geometryOf(triangles), tree, {}, grey, jobs);
        REQUIRE(baked.has_value());
        CHECK(baked->spacing == 128);
        // Every lattice point inside the room but its centre, which is more
        // than 128 from every face. No face lies on a lattice plane, so without
        // SS 4.6's growth there would be none at all.
        std::vector<Cell> expected;
        for (std::int32_t k = 1; k <= 3; ++k)
            for (std::int32_t j = 1; j <= 3; ++j)
                for (std::int32_t i = 1; i <= 3; ++i)
                    if (!(i == 2 && j == 2 && k == 2)) expected.push_back({i, j, k});
        CHECK(cellsOf(*baked) == expected);
    }

    SECTION("a sloped triangle in a room of sky") {
        std::vector<Triangle> triangles = room(
            {-1000, -1000, -1000}, {1000, 1000, 1000},
            {Face{"sky", PF_FAKE_BACKDROP}, Face{"sky", PF_FAKE_BACKDROP},
             Face{"sky", PF_FAKE_BACKDROP}, Face{"sky", PF_FAKE_BACKDROP},
             Face{"sky", PF_FAKE_BACKDROP}, Face{"sky", PF_FAKE_BACKDROP}});
        const double third = 1.0 / std::sqrt(3.0);
        triangles.push_back(Triangle{{Vec3{0, 0, 0}, Vec3{512, 0, 512}, Vec3{0, 512, 512}},
                                     {-third, -third, third}, "wall"});
        const auto tree = worldOf({box({-1000, -1000, -1000}, {1000, 1000, 1000})},
                                  {-1000, -1000, -1000}, {1000, 1000, 1000});
        const auto baked = bakeLightProbes(geometryOf(triangles), tree, {}, grey, jobs);
        REQUIRE(baked.has_value());
        // The grown box runs from -128 to 640 on each axis. The plane through
        // the origin has normal (-1, -1, 1) / sqrt(3), so a point is within 128
        // of it where |z - x - y| <= 128 sqrt(3), about 221.7 -- on this
        // lattice, where |k - i - j| <= 1.
        std::vector<Cell> expected;
        for (std::int32_t k = -1; k <= 5; ++k)
            for (std::int32_t j = -1; j <= 5; ++j)
                for (std::int32_t i = -1; i <= 5; ++i)
                    if (std::abs(k - i - j) <= 1) expected.push_back({i, j, k});
        CHECK(cellsOf(*baked) == expected);
    }

    SECTION("a level with no triangle") {
        const auto baked = bakeLightProbes({}, {}, {}, grey, jobs);
        REQUIRE(baked.has_value());
        CHECK(baked->spacing == 128);
        CHECK(baked->probes.empty());
    }
}

TEST_CASE("what a ray sees", "[ubake][probes]") {
    const AlbedoLookup grey = albedoOf({});

    SECTION("a surface seen from behind") {
        // The probe is below the floor, so every ray that meets it meets its
        // back. Lit from the front, a one-sided floor adds nothing.
        const auto oneSided = geometryOf(floorQuad("grey"));
        const SurfaceRays oneSidedRays(oneSided);
        CHECK(total(gatherProbe({0, 0, -100}, oneSidedRays, oneSided,
                                {steadyLight(1, {0, 0, 100})}, grey))
              == 0.0);
        // Two-sided and lit from the probe's side, it does.
        const auto twoSided = geometryOf(floorQuad("grey", PF_TWO_SIDED));
        const SurfaceRays twoSidedRays(twoSided);
        CHECK(total(gatherProbe({0, 0, -100}, twoSidedRays, twoSided,
                                {steadyLight(1, {300, 0, -100})}, grey))
              > 0.0);
    }

    SECTION("an occluder between a light and the surface") {
        // A roof at z 200, wider than the floor, between the floor and a light
        // above it; the probe is between the floor and the roof.
        const auto withRoof = [](std::uint32_t roofFlags) {
            std::vector<Triangle> triangles = floorQuad("grey");
            const auto roof = quad({-3000, -3000, 200}, {3000, -3000, 200}, {3000, 3000, 200},
                                   {-3000, 3000, 200}, {0, 0, -1}, "grey", roofFlags);
            triangles.insert(triangles.end(), roof.begin(), roof.end());
            return geometryOf(triangles);
        };
        const std::vector<Light> light{steadyLight(1, {0, 0, 300})};
        const auto opaque = withRoof(0);
        const SurfaceRays opaqueRays(opaque);
        CHECK(total(gatherProbe({0, 0, 100}, opaqueRays, opaque, light, grey)) == 0.0);
        const auto clear = withRoof(PF_TRANSLUCENT);
        const SurfaceRays clearRays(clear);
        CHECK(total(gatherProbe({0, 0, 100}, clearRays, clear, light, grey)) > 0.0);
    }

    SECTION("a sky surface") {
        const std::vector<Light> light{steadyLight(1, {0, 0, 300})};
        const auto sky = geometryOf(floorQuad("grey", PF_FAKE_BACKDROP));
        const SurfaceRays skyRays(sky);
        CHECK(total(gatherProbe({0, 0, 100}, skyRays, sky, light, grey)) == 0.0);
        const auto plain = geometryOf(floorQuad("grey"));
        const SurfaceRays plainRays(plain);
        CHECK(total(gatherProbe({0, 0, 100}, plainRays, plain, light, grey)) > 0.0);
    }
}

TEST_CASE("faces", "[ubake][probes]") {
    const auto redFloorRoom = geometryOf(room(
        {0, 0, 0}, {512, 512, 512},
        {Face{"white"}, Face{"white"}, Face{"white"}, Face{"white"}, Face{"red"}, Face{"white"}}));
    const AlbedoLookup albedo = albedoOf({{"red", {0.8, 0.1, 0.1}}, {"white", {0.8, 0.8, 0.8}}});
    const SurfaceRays rays(redFloorRoom);
    const Vec3 probe{256, 256, 256};

    SECTION("the floor's colour reaches the face rays cast downward") {
        const auto cube = gatherProbe(probe, rays, redFloorRoom,
                                      {steadyLight(1, {256, 256, 480}, 64)}, albedo);
        // Face 5 is -Z, gathered from rays cast down at the red floor; face 4
        // is +Z, gathered from rays cast up at the white ceiling.
        REQUIRE(cube[5].g > 0);
        REQUIRE(cube[4].g > 0);
        CHECK(cube[5].r / cube[5].g > cube[4].r / cube[4].g);
    }

    SECTION("doubling the brightness doubles every value exactly") {
        const auto once = gatherProbe(probe, rays, redFloorRoom,
                                      {steadyLight(1, {256, 256, 480}, 64)}, albedo);
        const auto twice = gatherProbe(probe, rays, redFloorRoom,
                                       {steadyLight(1, {256, 256, 480}, 128)}, albedo);
        for (std::size_t face = 0; face < 6; ++face) {
            CHECK(twice[face].r == 2 * once[face].r);
            CHECK(twice[face].g == 2 * once[face].g);
            CHECK(twice[face].b == 2 * once[face].b);
        }
    }

    SECTION("equal radiance everywhere gives that radiance on every face") {
        const std::vector<Rgb> even(directions().size(), Rgb{0.3, 0.3, 0.3});
        for (const Rgb& face : cubeOf(even)) {
            CHECK(std::abs(face.r - 0.3) < 1e-12);
            CHECK(std::abs(face.g - 0.3) < 1e-12);
            CHECK(std::abs(face.b - 0.3) < 1e-12);
        }
    }

    SECTION("the directions") {
        const auto& all = directions();
        REQUIRE(all.size() == 162);
        for (std::size_t i = 0; i < all.size(); ++i) {
            CHECK(std::abs(uta::ubake::length(all[i]) - 1.0) < 1e-15);
            if (i > 0) {
                const Vec3& a = all[i - 1];
                const Vec3& b = all[i];
                CHECK((a.z < b.z || (a.z == b.z && (a.y < b.y || (a.y == b.y && a.x < b.x)))));
            }
        }
    }
}

TEST_CASE("workers", "[ubake][probes]") {
    const auto geometry = geometryOf(room(
        {0, 0, 0}, {512, 512, 512},
        {Face{"white"}, Face{"white"}, Face{"white"}, Face{"white"}, Face{"red"}, Face{"white"}}));
    const auto tree = worldOf({box({0, 0, 0}, {512, 512, 512})}, {0, 0, 0}, {512, 512, 512});
    const AlbedoLookup albedo = albedoOf({{"red", {0.8, 0.1, 0.1}}, {"white", {0.8, 0.8, 0.8}}});
    const std::vector<Light> lights{steadyLight(1, {256, 256, 480}, 64)};

    const auto bytesWith = [&](unsigned workers) {
        JobSystem jobs(workers);
        const auto baked = bakeLightProbes(geometry, tree, lights, albedo, jobs);
        REQUIRE(baked.has_value());
        REQUIRE_FALSE(baked->probes.empty());
        uta::ubundle::Bundle bundle;
        bundle.lightProbes = *baked;
        const auto written = uta::ubundle::write(bundle);
        REQUIRE(written.has_value());
        return *written;
    };
    CHECK(bytesWith(1) == bytesWith(4));
}
