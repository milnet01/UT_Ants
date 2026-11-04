// UTA-0293: where the bake plants grass.
//
// docs/specs/UTA-0293-grass.md SS 4.1, SS 4.2, INV-1 to INV-6.
//
// INV-1, INV-2, INV-3 and INV-6 bake a fixture level, so the name test and the
// greenness are read through the whole path a map takes. INV-4 and INV-5 hand
// plantGrass a bundle: a water zone and an inventory class are each a rule over
// what the bundle already says, and building either in a package would test
// the zone and class readers, which their own specs cover.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "BakeFixture.h"

#include "core/Jobs.h"
#include "ubake/Bake.h"
#include "ubake/Grass.h"
#include "ubundle/Bundle.h"
#include "umat/Material.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace uta::test::bake;
using uta::JobSystem;
using uta::ubake::BakeResult;
using uta::ubake::GreenMap;
using uta::ubake::PICKUP_CLEAR;
using uta::ubundle::Bundle;
using uta::ubundle::Grass;
namespace detail = uta::ubake::detail;

namespace {

using Square = std::array<std::array<float, 3>, 4>;
using Colour = std::array<std::uint8_t, 4>;

constexpr Colour GREEN = {60, 140, 40, 255};
constexpr Colour GREY = {128, 128, 128, 255};

/// A flat floor at z = 0 across [0, 512] on x and y, wound to face up.
constexpr Square FLOOR = {{{0, 0, 0}, {512, 0, 0}, {512, 512, 0}, {0, 512, 0}}};
constexpr std::array<float, 3> UP = {0, 0, 1};

/// A picture of one colour.
Picture plain(const Colour& colour) {
    Picture out;
    out.indices.assign(std::size_t{out.width} * out.height, 0);
    out.palette.assign(4, colour);
    return out;
}

/// The fixture level with one surface, `corners` facing `normal`, wearing a
/// texture of the map's own named `name` and painted `colour`.
BakeResult bakeOne(const std::string& name, const Colour& colour, const Square& corners,
                   const std::array<float, 3>& normal) {
    Fixture fixture;
    MapBuilder& map = fixture.map;
    const std::int32_t texture = map.addTexture(TextureSpec{name, "", plain(colour), false});
    map.addSurface(texture).shapeSurface(corners, normal);
    MemoryPackages packages = memoryPackagesFor(fixture);
    JobSystem jobs(2);
    const detail::CuratedLookup nothingCurated = [](std::uint64_t) -> const uta::umat::CuratedOverride* {
        return nullptr;
    };
    // A Package views its bytes, so they outlive it.
    const std::vector<std::byte> bytes = asByteVector(map.build());
    const auto opened = uta::upkg::Package::open(bytes);
    REQUIRE(opened.has_value());
    auto baked = detail::bake(*opened, MAP_NAME, packages.resolver(), jobs, nothingCurated,
                              uta::umat::TEXTURE_BUDGET_BYTES);
    if (!baked.has_value()) FAIL("the bake was refused: " << baked.error().message());
    return std::move(*baked);
}

std::size_t clumpsOf(const BakeResult& result) {
    return result.bundle.grass.has_value() ? result.bundle.grass->clumps.size() : 0;
}

/// The bundle plantGrass reads for INV-4 and INV-5: one material,
/// "pkg.grass1", on two triangles making FLOOR, in zone 0.
Bundle floorBundle() {
    Bundle bundle;
    bundle.materials = std::vector<uta::ubundle::MaterialRecord>{
        uta::ubundle::MaterialRecord{"pkg.grass1", false, 0, std::nullopt}};
    uta::ubundle::Geometry geometry;
    for (const auto& corner : FLOOR) {
        uta::ubundle::GeometryVertex vertex;
        vertex.position = corner;
        vertex.normal = UP;
        vertex.u = corner[0] / 512;
        vertex.v = corner[1] / 512;
        geometry.vertices.push_back(vertex);
    }
    geometry.indices = {0, 1, 2, 0, 2, 3};
    geometry.batches = {uta::ubundle::GeometryBatch{"pkg.grass1", 0, 0, 6, {}}};
    bundle.geometry = std::move(geometry);
    bundle.zones = std::vector<uta::ubundle::Zone>(1);
    return bundle;
}

/// Full greenness everywhere.
const GreenMap ALL_GREEN{1, 1, {255}};

uta::ubake::Planted plant(const Bundle& bundle) {
    return uta::ubake::plantGrass(bundle, [](std::string_view) { return &ALL_GREEN; });
}

} // namespace

TEST_CASE("UTA-0293 INV-1: grass grows only on ground-cover names", "[ubake][grass]") {
    const BakeResult grass = bakeOne("Grass1", GREEN, FLOOR, UP);
    const BakeResult tile = bakeOne("Tile1", GREEN, FLOOR, UP);
    // 1024 grid cells; a green picture keeps every one.
    CHECK(clumpsOf(grass) > 900);
    CHECK(clumpsOf(tile) == 0);
    CHECK_FALSE(tile.bundle.grass.has_value());

    // Each clump stands on the floor and names its material.
    REQUIRE(grass.bundle.grass.has_value());
    for (const auto& clump : grass.bundle.grass->clumps) {
        CHECK(clump.base[2] == 0.0f);
        CHECK(clump.base[0] >= 0.0f);
        CHECK(clump.base[0] <= 512.0f);
        CHECK((*grass.bundle.materials)[clump.material].id == "dm-fixture.grass1");
    }
}

TEST_CASE("UTA-0293 INV-1: the name test reads groups and drops the package", "[ubake][grass]") {
    using uta::ubake::isGroundCover;
    CHECK(isGroundCover("genearth.grass.mgrass7"));
    CHECK(isGroundCover("pkg.lawns.tile1"));
    CHECK(isGroundCover("pkg.dirt2#masked"));
    CHECK_FALSE(isGroundCover("dm-grassy.tile1"));
    CHECK_FALSE(isGroundCover("pkg.tile1"));
    CHECK_FALSE(isGroundCover("pkg.tile1#grass"));
}

TEST_CASE("UTA-0293 INV-2: grass grows only where the picture is green", "[ubake][grass]") {
    CHECK(clumpsOf(bakeOne("Grass1", GREY, FLOOR, UP)) == 0);

    // Half green, half grey across u: clumps only on the green half.
    Picture split = plain(GREY);
    split.palette[1] = GREEN;
    for (std::uint32_t y = 0; y < split.height; ++y)
        for (std::uint32_t x = 0; x < split.width; ++x) split.indices[y * split.width + x] = x < 2 ? 1 : 0;
    const auto map = uta::ubake::greenMapOf(
        uta::umat::Image{4, 4, 4,
                         [&split] {
                             std::vector<std::byte> pixels;
                             for (const std::uint8_t index : split.indices)
                                 for (const std::uint8_t c : split.palette[index]) pixels.push_back(std::byte{c});
                             return pixels;
                         }()},
        1.0);
    REQUIRE(map.has_value());
    REQUIRE(map->width == 2);
    CHECK(map->green[0] == 255);
    CHECK(map->green[1] == 0);
    const auto planted = uta::ubake::plantGrass(floorBundle(), [&map](std::string_view) { return &*map; });
    REQUIRE(planted.grass.has_value());
    for (const auto& clump : planted.grass->clumps) CHECK(clump.base[0] < 256.0f);
}

TEST_CASE("UTA-0293 INV-3: grass grows only on floors", "[ubake][grass]") {
    const Square wall = {{{0, 0, 0}, {512, 0, 0}, {512, 0, 512}, {0, 0, 512}}};
    CHECK(clumpsOf(bakeOne("Grass1", GREEN, wall, {0, -1, 0})) == 0);

    // Tilted 40 degrees about y: its normal's up component is cos 40, 0.77.
    const float s = std::sin(40.0f * 3.14159265f / 180), c = std::cos(40.0f * 3.14159265f / 180);
    const float drop = -256 * s / c;
    const Square tilted = {{{0, 0, 0}, {256, 0, drop}, {256, 256, drop}, {0, 256, 0}}};
    CHECK(clumpsOf(bakeOne("Grass1", GREEN, tilted, {s, 0, c})) > 0);
}

TEST_CASE("UTA-0293 INV-4: grass never grows under water", "[ubake][grass]") {
    Bundle bundle = floorBundle();
    REQUIRE(plant(bundle).grass.has_value());
    (*bundle.zones)[0].water = 1;
    CHECK_FALSE(plant(bundle).grass.has_value());
}

TEST_CASE("UTA-0293 INV-5: grass never hides a pickup", "[ubake][grass]") {
    using uta::ubundle::ValueKind;
    Bundle bundle = floorBundle();
    uta::ubundle::Placements placements;
    uta::ubundle::ActorClass health;
    health.path = "botpack.healthpack";
    health.resolved = true;
    health.ancestry = {"botpack.tournamenthealth", "engine.pickup", "engine.inventory", "engine.actor"};
    placements.classes = {health};
    uta::ubundle::ActorPlacement actor;
    // High above the floor: the clearance is horizontal.
    actor.properties = {uta::ubundle::PropertyRecord{"Location", 0, ValueKind::Vector,
                                                     std::array<float, 3>{256, 256, 500}}};
    placements.actors = {actor};
    bundle.placements = std::move(placements);

    const auto planted = plant(bundle);
    REQUIRE(planted.grass.has_value());
    std::size_t beyond = 0;
    for (const auto& clump : planted.grass->clumps) {
        const double d = std::hypot(clump.base[0] - 256.0, clump.base[1] - 256.0);
        CHECK(d >= PICKUP_CLEAR);
        if (d < 2 * PICKUP_CLEAR) ++beyond;
    }
    CHECK(beyond > 0);
}

TEST_CASE("UTA-0293 INV-6: the same map plants the same grass", "[ubake][grass]") {
    const BakeResult first = bakeOne("Grass1", GREEN, FLOOR, UP);
    const BakeResult second = bakeOne("Grass1", GREEN, FLOOR, UP);
    const auto vegeOf = [](const BakeResult& result) {
        Bundle only;
        only.materials = result.bundle.materials;
        only.grass = result.bundle.grass;
        const auto written = uta::ubundle::write(only);
        REQUIRE(written.has_value());
        return *written;
    };
    REQUIRE(first.bundle.grass.has_value());
    CHECK(vegeOf(first) == vegeOf(second));
}

TEST_CASE("UTA-0293: VEGE's cells hold every clump once inside their bounds", "[ubake][grass]") {
    // A floor wider than one cell.
    Bundle bundle = floorBundle();
    for (auto& vertex : bundle.geometry->vertices) {
        vertex.position[0] *= 6;
        vertex.position[1] *= 6;
    }
    const auto planted = plant(bundle);
    REQUIRE(planted.grass.has_value());
    const Grass& grass = *planted.grass;
    CHECK(grass.cells.size() > 4);
    std::size_t next = 0;
    for (const auto& cell : grass.cells) {
        CHECK(cell.firstClump == next);
        next += cell.clumpCount;
        for (std::uint32_t k = cell.firstClump; k < cell.firstClump + cell.clumpCount; ++k)
            for (std::size_t axis = 0; axis < 3; ++axis) {
                CHECK(grass.clumps[k].base[axis] >= cell.low[axis]);
                CHECK(grass.clumps[k].base[axis] <= cell.high[axis]);
            }
    }
    CHECK(next == grass.clumps.size());
}

TEST_CASE("UTA-0293 INV-1: plantGrass makes its own name test", "[ubake][grass]") {
    // The bake makes green maps for ground-cover names only; planting still
    // refuses another name handed one.
    Bundle bundle = floorBundle();
    (*bundle.materials)[0].id = "pkg.tile1";
    bundle.geometry->batches[0].material = "pkg.tile1";
    CHECK_FALSE(plant(bundle).grass.has_value());
}

TEST_CASE("UTA-0293 INV-3: a slope steeper than GRASS_SLOPE grows none", "[ubake][grass]") {
    // A wall has no area in the plan, so it plants nothing whatever the slope
    // test says; a 60-degree slope has area there, and an up component of 0.5.
    Bundle bundle = floorBundle();
    for (auto& vertex : bundle.geometry->vertices) vertex.normal = {0.866f, 0, 0.5f};
    CHECK_FALSE(plant(bundle).grass.has_value());
}
