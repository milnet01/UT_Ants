// UTA-0338: the bake's sun -- INV-5, what a point sees of it, and INV-6, what
// baking it adds and leaves alone.
//
// docs/specs/UTA-0338-baked-sun.md SS 4.3 and SS 4.4.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "BakeFixture.h"

#include "core/Jobs.h"
#include "ubake/Bake.h"
#include "ubake/Geometry.h"
#include "ubake/LightModel.h"
#include "ubake/LightProbes.h"
#include "ubake/SurfaceRays.h"
#include "ubundle/Bundle.h"
#include "upkg/Package.h"
#include "urecipe/Recipe.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using namespace uta::test::bake;
using uta::JobSystem;
using uta::ubake::BakeResult;
using uta::ubake::Vec3;
using uta::urecipe::Recipe;
namespace detail = uta::ubake::detail;
namespace ubundle = uta::ubundle;

namespace {

using Square = std::array<std::array<float, 3>, 4>;

constexpr std::uint32_t PF_FAKE_BACKDROP = 0x80;

/// A level square at `z`, facing up or down.
Square level(float x0, float x1, float y0, float y1, float z, bool up) {
    if (up) return {{{x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}}};
    return {{{x0, y0, z}, {x0, y1, z}, {x1, y1, z}, {x1, y0, z}}};
}

/// Two floors at z = 20. Floor 0, x and y in [-60, 60], lies under a backdrop
/// square at z = 200 -- the roof's hole showing the sky. Floor 1, x in
/// [196, 316], lies under a solid roof at z = 200, with a static light
/// "Lamp0" at (256, 0, 100) between them. Probe (0, 0, 128) stands under the
/// hole, probe (256, 0, 128) under the roof.
struct Room {
    Fixture fixture;
    std::vector<std::byte> bytes;
};

Room room() {
    Room out;
    MapBuilder& map = out.fixture.map;
    const std::int32_t wall = map.addTexture(TextureSpec{"Wall", "", picture(3), false});
    const std::int32_t sky = map.addTexture(TextureSpec{"Sky", "", picture(4), false});
    map.addActorOfClass("ActorPkg", "Lamp",
                        {vectorProperty("Location", 256.0F, 0.0F, 100.0F), boolProperty("bStatic", true),
                         byteProperty("LightBrightness", 200), byteProperty("LightRadius", 12)});
    map.addSurface(wall).shapeSurface(level(-60, 60, -60, 60, 20, true), {0, 0, 1});
    map.addSurface(wall).shapeSurface(level(196, 316, -60, 60, 20, true), {0, 0, 1});
    map.addSurface(sky, PF_FAKE_BACKDROP).shapeSurface(level(-60, 60, -60, 60, 200, false), {0, 0, -1});
    map.addSurface(wall).shapeSurface(level(196, 316, -60, 60, 200, false), {0, 0, -1});
    out.bytes = asByteVector(map.build());
    return out;
}

/// The sun straight overhead, white and at full brightness.
Recipe sunRecipe() {
    Recipe recipe;
    recipe.map = std::string(MAP_NAME);
    recipe.mapDigest = std::array<std::byte, 32>{};
    recipe.sun = uta::urecipe::Sun{.yaw = 0, .pitch = 16384, .hue = 0, .saturation = 255, .brightness = 255};
    return recipe;
}

BakeResult bakedOk(const Room& room, const Recipe* recipe) {
    MemoryPackages packages = memoryPackagesFor(room.fixture);
    JobSystem jobs(2);
    const detail::CuratedLookup nothingCurated = [](std::uint64_t) -> const uta::umat::CuratedOverride* {
        return nullptr;
    };
    const detail::FlameLookup noFlame = [](std::uint64_t) { return false; };
    const auto opened = uta::upkg::Package::open(room.bytes);
    REQUIRE(opened.has_value());
    auto result = detail::bake(*opened, MAP_NAME, packages.resolver(), jobs, nothingCurated,
                               uta::umat::TEXTURE_BUDGET_BYTES, nullptr, noFlame, recipe);
    if (!result.has_value()) FAIL("the bake was refused: " << result.error().message());
    return std::move(*result);
}

/// The GEOM vertex nearest `p`, and the chart SMSK gives it.
std::uint32_t chartAt(const ubundle::Bundle& bundle, const std::array<float, 3>& p) {
    const auto& vertices = bundle.geometry->vertices;
    std::size_t best = 0;
    for (std::size_t v = 1; v < vertices.size(); ++v) {
        const auto distance = [&](std::size_t k) {
            double sum = 0;
            for (std::size_t axis = 0; axis < 3; ++axis)
                sum += (vertices[k].position[axis] - p[axis]) * (vertices[k].position[axis] - p[axis]);
            return sum;
        };
        if (distance(v) < distance(best)) best = v;
    }
    return bundle.shadowMask->vertexChart[best];
}

/// Each pair of a light below `below`, as its chart, light, and texels.
std::vector<std::tuple<std::size_t, std::uint32_t, std::vector<std::uint8_t>>> pairsBelow(
    const ubundle::ShadowMask& mask, std::uint32_t below) {
    std::vector<std::tuple<std::size_t, std::uint32_t, std::vector<std::uint8_t>>> out;
    for (std::size_t c = 0; c < mask.charts.size(); ++c) {
        const ubundle::MaskChart& chart = mask.charts[c];
        for (std::uint32_t k = chart.firstPair; k < chart.firstPair + chart.pairCount; ++k) {
            const ubundle::MaskPair& pair = mask.pairs[k];
            if (pair.light >= below) continue;
            std::vector<std::uint8_t> texels;
            if (pair.x != ubundle::MASK_ALL_LIT)
                for (std::uint32_t y = 0; y < chart.height; ++y)
                    for (std::uint32_t x = 0; x < chart.width; ++x)
                        texels.push_back(mask.texels[(pair.y + y) * mask.width + pair.x + x]);
            out.emplace_back(c, pair.light, std::move(texels));
        }
    }
    return out;
}

/// The probe at `cell`'s index, or none.
std::optional<std::size_t> probeAt(const ubundle::LightProbes& probes, const std::array<std::int32_t, 3>& cell) {
    for (std::size_t p = 0; p < probes.probes.size(); ++p)
        if (probes.probes[p].cell == cell) return p;
    return std::nullopt;
}

double sumOf(const ubundle::LightProbe& probe) {
    double sum = 0;
    for (const auto& face : probe.cube)
        for (const float channel : face) sum += channel;
    return sum;
}

/// The written bytes of a bundle holding only `bundle`'s LITE, LAMP, GEOM and AOCC.
std::vector<std::byte> unsunned(const ubundle::Bundle& bundle) {
    ubundle::Bundle part;
    part.header = bundle.header;
    part.lights = bundle.lights;
    part.lamps = bundle.lamps;
    part.geometry = bundle.geometry;
    part.occlusion = bundle.occlusion;
    auto written = ubundle::write(part);
    REQUIRE(written.has_value());
    return *written;
}

} // namespace

TEST_CASE("UTA-0338 INV-5: a point sees the sun only through a backdrop surface", "[ubake][sun]") {
    const BakeResult result = bakedOk(room(), nullptr);
    const ubundle::Geometry& geometry = *result.bundle.geometry;
    const uta::ubake::SurfaceRays rays(geometry);
    const Vec3 up{0, 0, 1};
    // Under the hole: the first surface overhead is the backdrop.
    CHECK(uta::ubake::sunSeen(Vec3{0, 0, 21}, up, rays));
    CHECK(uta::ubake::sunSeen(Vec3{-50, 40, 100}, up, rays));
    // Under the roof: the first is a plain wall.
    CHECK_FALSE(uta::ubake::sunSeen(Vec3{256, 0, 21}, up, rays));
    // Beside both: nothing at all.
    CHECK_FALSE(uta::ubake::sunSeen(Vec3{120, 0, 21}, up, rays));
    // Away from the sun the backdrop is not met.
    CHECK_FALSE(uta::ubake::sunSeen(Vec3{0, 0, 21}, Vec3{0, 0, -1}, rays));
    // A set letting backdrops through meets nothing above the hole.
    const uta::ubake::SurfaceRays passing(geometry, uta::ubake::LIGHT_PASSES_FLAGS | PF_FAKE_BACKDROP);
    CHECK_FALSE(uta::ubake::sunSeen(Vec3{0, 0, 21}, up, passing));
    // A hole in the backdrop lets the ray through to nothing.
    const uta::ubake::SurfaceRays::Hole skyHoles = [&geometry](std::size_t triangle, double, double) {
        for (const ubundle::GeometryBatch& batch : geometry.batches)
            if (3 * triangle >= batch.firstIndex && 3 * triangle < batch.firstIndex + batch.indexCount)
                return (batch.polyFlags & PF_FAKE_BACKDROP) != 0;
        return false;
    };
    CHECK_FALSE(uta::ubake::sunSeen(Vec3{0, 0, 21}, up, rays, skyHoles));
    CHECK(uta::ubake::sunSeen(Vec3{0, 0, 21}, up, rays, [](std::size_t, double, double) { return false; }));

    // The light reaching a floor: the sun under the hole, none under the roof.
    const std::vector<ubundle::Light> sun{
        ubundle::lightOfSun(ubundle::Sun{.yaw = 0, .pitch = 16384, .saturation = 255, .brightness = 255})};
    const uta::ubake::Rgb open = uta::ubake::lightReaching(Vec3{0, 0, 20}, up, rays, sun);
    const uta::ubake::Rgb shut = uta::ubake::lightReaching(Vec3{256, 0, 20}, up, rays, sun);
    CHECK(open.r > 0.5);
    CHECK(shut.r == 0.0);

    // What a surface sends on: the sun's bounce, except through the sky view.
    const ubundle::GeometryBatch* wallBatch = nullptr;
    for (const ubundle::GeometryBatch& batch : geometry.batches)
        if ((batch.polyFlags & PF_FAKE_BACKDROP) == 0) wallBatch = &batch;
    REQUIRE(wallBatch != nullptr);
    const uta::ubake::AlbedoLookup grey = [](std::string_view) { return uta::ubake::Rgb{0.1, 0.1, 0.1}; };
    uta::ubake::SurfaceHit hit{Vec3{0, 0, 20}, up, wallBatch, false};
    CHECK(uta::ubake::sentFrom(hit, rays, sun, grey, {}).r > 0);
    hit.viaSky = true;
    CHECK(uta::ubake::sentFrom(hit, rays, sun, grey, {}).r == 0.0);

    // A probe's share: all of it under the hole, none under the roof.
    CHECK(uta::ubake::sunSeenFrom(Vec3{0, 0, 128}, up, rays) == 1.0f);
    CHECK(uta::ubake::sunSeenFrom(Vec3{256, 0, 128}, up, rays) == 0.0f);
}

TEST_CASE("UTA-0338 INV-6: the sun adds its pairs and probes and moves nothing else", "[ubake][sun]") {
    const Room fixture = room();
    const Recipe recipe = sunRecipe();
    const BakeResult without = bakedOk(fixture, nullptr);
    const BakeResult with = bakedOk(fixture, &recipe);
    const ubundle::Bundle& plain = without.bundle;
    const ubundle::Bundle& sunny = with.bundle;

    REQUIRE_FALSE(plain.sun.has_value());
    REQUIRE(sunny.sun.has_value());
    CHECK(sunny.sun->pitch == 16384);
    CHECK(unsunned(sunny) == unsunned(plain));

    // SMSK: every other light's pairs are the same; the sun's index follows LITE's.
    REQUIRE(plain.shadowMask.has_value());
    REQUIRE(sunny.shadowMask.has_value());
    const auto sunIndex = static_cast<std::uint32_t>(sunny.lights->size());
    CHECK(pairsBelow(*sunny.shadowMask, sunIndex) == pairsBelow(*plain.shadowMask, sunIndex));
    std::vector<std::uint32_t> sunCharts;
    for (std::size_t c = 0; c < sunny.shadowMask->charts.size(); ++c) {
        const ubundle::MaskChart& chart = sunny.shadowMask->charts[c];
        for (std::uint32_t k = chart.firstPair; k < chart.firstPair + chart.pairCount; ++k)
            if (sunny.shadowMask->pairs[k].light == sunIndex) {
                sunCharts.push_back(static_cast<std::uint32_t>(c));
                CHECK(sunny.shadowMask->pairs[k].moverReach == 0);
            }
    }
    // The floor under the hole has a sun pair; the roofed floor has none.
    const std::uint32_t litFloor = chartAt(sunny, {0, 0, 20});
    const std::uint32_t roofedFloor = chartAt(sunny, {256, 0, 20});
    CHECK(std::ranges::find(sunCharts, litFloor) != sunCharts.end());
    CHECK(std::ranges::find(sunCharts, roofedFloor) == sunCharts.end());

    // LPRB: visibility only with the sun, seen under the hole and not under the roof.
    CHECK(plain.lightProbes->sunSeen.empty());
    REQUIRE(sunny.lightProbes->sunSeen.size() == sunny.lightProbes->probes.size());
    REQUIRE(sunny.lightProbes->probes.size() == plain.lightProbes->probes.size());
    const auto under = probeAt(*sunny.lightProbes, {0, 0, 1});
    const auto roofed = probeAt(*sunny.lightProbes, {2, 0, 1});
    REQUIRE(under.has_value());
    REQUIRE(roofed.has_value());
    CHECK(sunny.lightProbes->sunSeen[*under] > 0.0f);
    CHECK(sunny.lightProbes->sunSeen[*roofed] == 0.0f);
    // The base layer takes the sun's bounce, and the added layer never does.
    CHECK(sumOf(sunny.lightProbes->probes[*under]) > sumOf(plain.lightProbes->probes[*under]));
    CHECK(sunny.lightProbes->added.empty());
}
