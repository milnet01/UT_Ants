// UTA-0119's builder cases: which actors are movers, and their shapes.
//
// docs/specs/UTA-0119-mover-shapes.md SS 4.3 to SS 4.6, INV-3 to INV-9. The
// container cases are tests/unit/BundleMoversTest.cpp.
//
// Each case builds a map with tests/unit/BakeFixture.h and resolves it against
// enginePackage() -- Brush, and Mover under it -- and ActorPkg, built here: a
// class WeaponRemover that does not descend from Brush, and TallDoor, a Mover
// whose PostScale default is (1, 1, 2).
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "BakeFixture.h"

#include "core/Jobs.h"
#include "support/FCoordsPort.h"
#include "support/UnrealPackageBuilder.h"
#include "ubake/Actors.h"
#include "ubake/Bake.h"
#include "ubake/Movers.h"
#include "ubundle/Bundle.h"
#include "umat/Library.h"
#include "umat/Material.h"
#include "upkg/Geometry.h"
#include "upkg/Level.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace uta::test::bake;
using uta::ErrorCode;
using uta::test::asBytes;
using uta::ubake::buildActors;
using uta::ubake::buildMover;
using uta::ubake::findMovers;
using uta::ubake::MaterialLookup;
using uta::ubake::MoverSite;
using uta::ubake::SurfaceMaterial;
using uta::ubundle::MoverShape;
using uta::ubundle::Placements;
using uta::upkg::ObjectReferenceKind;
using uta::upkg::Package;

namespace fc = uta::test::fcoords;

namespace {

constexpr float HALF_ROOT_TWO = 0.70710677F;

/// A square tilted 45 degrees about Y. Its normal is (1, 0, 1) over root two,
/// which scales and divides to different directions (INV-4); its corners vary
/// in both x and z, so a SHEER_ZX shear moves them (INV-6).
BrushSpec tiltedSquare(std::int32_t texture = 0) {
    BrushSpec brush;
    brush.corners = {{{0, 0, 0}, {32, 0, -32}, {32, 64, -32}, {0, 64, 0}}};
    brush.normal = {HALF_ROOT_TWO, 0, HALF_ROOT_TWO};
    brush.texture = texture;
    return brush;
}

std::vector<std::uint8_t> actorPkg() {
    Packer pkg;
    pkg.addClass("WeaponRemover");
    pkg.addClass("TallDoor", pkg.importClass("Engine", "Mover"), {scaleProperty("PostScale", 1, 1, 2)});
    return pkg.build();
}

MemoryPackages installed() {
    MemoryPackages packages;
    packages.add("engine", enginePackage());
    packages.add("actorpkg", actorPkg());
    packages.add("core", tinyPackage("Object"));
    return packages;
}

/// Every texture wears one made material, 64 texels a repeat on each axis.
const MaterialLookup LOOKUP = [](uta::upkg::ObjectReference, bool) -> const SurfaceMaterial* {
    static const SurfaceMaterial made{"made", 64.0, 64.0};
    return &made;
};

struct Outcome {
    Placements placements;
    std::vector<std::size_t> movers; ///< each shape's placement index
    std::vector<MoverShape> shapes;
};

const uta::upkg::ExportEntry* levelOf(const Package& map) {
    for (const uta::upkg::ExportEntry& entry : map.exports()) {
        if (entry.objectClass.kind() == ObjectReferenceKind::Null) continue;
        const auto name = map.objectName(entry.objectClass);
        if (name.has_value() && *name == "Level") return &entry;
    }
    return nullptr;
}

/// SS 4.6 steps 5, 6 and 9 over the level `bytes` holds: the placements, the
/// movers found among them, and each one's shape.
uta::Result<Outcome> moversOf(const std::vector<std::uint8_t>& bytes, MemoryPackages& packages) {
    const auto map = Package::open(asBytes(bytes));
    REQUIRE(map.has_value());
    const uta::upkg::ExportEntry* const entry = levelOf(*map);
    REQUIRE(entry != nullptr);
    const auto level = uta::upkg::readLevel(*map, *entry);
    REQUIRE(level.has_value());

    UTA_TRY(uta::ubake::Actors actors, buildActors(*map, MAP_NAME, *level, packages.resolver()));
    UTA_TRY(const std::vector<MoverSite> sites, findMovers(*map, actors.placements));
    Outcome out;
    for (const MoverSite& site : sites) {
        UTA_TRY(const uta::upkg::Model model, uta::upkg::readModel(*map, *site.model));
        UTA_TRY(MoverShape shape, buildMover(site, model, actors.placements, LOOKUP));
        out.movers.push_back(site.placement);
        out.shapes.push_back(std::move(shape));
    }
    out.placements = std::move(actors.placements);
    return out;
}

/// One mover of Engine.Mover over the tilted square, carrying `properties`
/// beside its Brush.
Outcome oneMover(std::vector<PropertySpec> properties) {
    MapBuilder map;
    const std::int32_t brush = map.addBrushModel(tiltedSquare(map.importTexture("TexPkg", "", "Door")));
    properties.push_back(objectProperty("Brush", brush));
    map.addActor("Door0", map.importClass("Engine", "Mover"), std::move(properties));
    MemoryPackages packages = installed();
    const std::vector<std::uint8_t> bytes = map.build();
    auto outcome = moversOf(bytes, packages);
    REQUIRE(outcome.has_value());
    REQUIRE(outcome->shapes.size() == 1);
    return std::move(*outcome);
}

std::array<float, 3> positionOf(const MoverShape& shape, std::size_t vertex) {
    return shape.geometry.vertices[vertex].position;
}

void refusedFor(const std::vector<std::uint8_t>& bytes, std::string_view says) {
    MemoryPackages packages = installed();
    const auto outcome = moversOf(bytes, packages);
    REQUIRE_FALSE(outcome.has_value());
    CHECK(outcome.error().code() == ErrorCode::MalformedData);
    CHECK(outcome.error().message().find(says) != std::string_view::npos);
}

/// SS 4.5's placement formula, as the renderer is to compute it: location
/// plus postScale times Y P R q, with exact sine and cosine.
fc::Vec placed(fc::Vec q, fc::Vec location, const fc::Rotator& r, fc::Vec postScale) {
    const auto angle = [](std::int32_t units) { return units * 2.0 * 3.14159265358979323846 / 65536.0; };
    const double cy = std::cos(angle(r.yaw)), sy = std::sin(angle(r.yaw));
    const double cp = std::cos(angle(r.pitch)), sp = std::sin(angle(r.pitch));
    const double cr = std::cos(angle(r.roll)), sr = std::sin(angle(r.roll));
    const fc::Vec rolled{q.x, cr * q.y + sr * q.z, -sr * q.y + cr * q.z};
    const fc::Vec pitched{cp * rolled.x - sp * rolled.z, rolled.y, sp * rolled.x + cp * rolled.z};
    const fc::Vec yawed{cy * pitched.x - sy * pitched.y, sy * pitched.x + cy * pitched.y, pitched.z};
    return {location.x + postScale.x * yawed.x, location.y + postScale.y * yawed.y,
            location.z + postScale.z * yawed.z};
}

} // namespace

TEST_CASE("INV-3: an actor gets a shape exactly when the engine calls it a moving brush",
          "[ubake][movers]") {
    MapBuilder map;
    const std::int32_t brush = map.addBrushModel(tiltedSquare());
    const std::int32_t mover = map.importClass("Engine", "Mover");
    const std::int32_t brushClass = map.importClass("Engine", "Brush");
    map.addActor("Mover0", mover, {objectProperty("Brush", brush)});
    map.addActor("Brush1", brushClass, {objectProperty("Brush", brush), boolProperty("bStatic", false)});
    map.addActor("Mover2", mover, {objectProperty("Brush", brush), boolProperty("bStatic", true)});
    map.addActor("Brush3", brushClass, {objectProperty("Brush", brush)});
    map.addActor("Remover4", map.importClass("ActorPkg", "WeaponRemover"),
                 {objectProperty("Brush", brush)});
    map.addActor("Door5", map.importClass("NoSuchPkg", "SlidingDoor"), {objectProperty("Brush", brush)});
    map.addActor("Mover6", mover, {objectProperty("Brush", 0)});
    MemoryPackages packages = installed();

    const std::vector<std::uint8_t> bytes = map.build();
    const auto outcome = moversOf(bytes, packages);
    REQUIRE(outcome.has_value());
    REQUIRE(outcome->shapes.size() == 2);
    const auto& placed = outcome->placements.actors;
    CHECK(placed[outcome->movers[0]].path == "dm-fixture.mover0");
    CHECK(placed[outcome->movers[1]].path == "dm-fixture.brush1");
    for (std::size_t i = 0; i < 2; ++i)
        CHECK(outcome->shapes[i].exportIndex == placed[outcome->movers[i]].exportIndex);
}

TEST_CASE("INV-4: a shape is its Model pivoted and scaled with its winding kept front-facing",
          "[ubake][movers]") {
    const Outcome outcome = oneMover(
        {vectorProperty("PrePivot", 8, 0, 0), scaleProperty("MainScale", 2, -1, 1)});
    const MoverShape& shape = outcome.shapes[0];
    REQUIRE(shape.geometry.vertices.size() == 4);

    // MainScale times (p - PrePivot), corner by corner.
    CHECK(positionOf(shape, 0) == std::array<float, 3>{-16, 0, 0});
    CHECK(positionOf(shape, 1) == std::array<float, 3>{48, 0, -32});
    CHECK(positionOf(shape, 2) == std::array<float, 3>{48, -64, -32});
    CHECK(positionOf(shape, 3) == std::array<float, 3>{-16, -64, 0});

    // n divided by MainScale, normalised: (1/2, 0, 1) over its length. A
    // normal scaled instead would lean the other way, (2, 0, 1).
    const auto& normal = shape.geometry.vertices[0].normal;
    CHECK(std::abs(normal[0] - 0.4472136F) < 1e-5F);
    CHECK(std::abs(normal[1]) < 1e-5F);
    CHECK(std::abs(normal[2] - 0.8944272F) < 1e-5F);

    // u and v as buildGeometry made them, from the brush's own space.
    CHECK(shape.geometry.vertices[1].u == 0.5F);
    CHECK(shape.geometry.vertices[2].v == 1.0F);

    // MainScale's product is negative, so each triangle's last two swap.
    CHECK(shape.geometry.indices == std::vector<std::uint32_t>{0, 2, 1, 0, 3, 2});
}

TEST_CASE("INV-5: a mover's numbers are its own else its class's else the defaults",
          "[ubake][movers]") {
    MapBuilder map;
    const std::int32_t brush = map.addBrushModel(tiltedSquare());
    const std::int32_t tall = map.importClass("ActorPkg", "TallDoor");
    map.addActor("Door0", tall,
                 {objectProperty("Brush", brush), vectorProperty("Location", 10, 20, 30),
                  rotatorProperty("Rotation", 1, 2, 3), scaleProperty("MainScale", 2, 2, 2)});
    map.addActor("Door1", tall, {objectProperty("Brush", brush)});
    MemoryPackages packages = installed();

    const std::vector<std::uint8_t> bytes = map.build();
    const auto outcome = moversOf(bytes, packages);
    REQUIRE(outcome.has_value());
    REQUIRE(outcome->shapes.size() == 2);
    const MoverShape& own = outcome->shapes[0];
    const MoverShape& bare = outcome->shapes[1];

    CHECK(own.location == std::array<float, 3>{10, 20, 30});
    CHECK(own.rotation == std::array<std::int32_t, 3>{1, 2, 3});
    CHECK(own.postScale == std::array<float, 3>{1, 1, 2});
    CHECK(positionOf(own, 1) == std::array<float, 3>{64, 0, -64});

    CHECK(bare.location == std::array<float, 3>{0, 0, 0});
    CHECK(bare.rotation == std::array<std::int32_t, 3>{0, 0, 0});
    CHECK(bare.postScale == std::array<float, 3>{1, 1, 2});
    CHECK(positionOf(bare, 1) == std::array<float, 3>{32, 0, -32});
}

TEST_CASE("INV-6: a shear on MainScale is not applied", "[ubake][movers]") {
    const Outcome plain = oneMover({scaleProperty("MainScale", 1, 1, 1)});
    // SHEER_ZX is 5; FSheerSnap keeps 0.8 at 0.65, which would move a corner
    // with x in z.
    const Outcome sheared = oneMover({scaleProperty("MainScale", 1, 1, 1, 0.8F, 5)});
    REQUIRE(plain.shapes[0].geometry.vertices.size() == sheared.shapes[0].geometry.vertices.size());
    for (std::size_t v = 0; v < plain.shapes[0].geometry.vertices.size(); ++v)
        CHECK(positionOf(plain.shapes[0], v) == positionOf(sheared.shapes[0], v));
}

TEST_CASE("INV-7: the placement formula is the engine's ToWorld without shear",
          "[ubake][movers]") {
    // This checks the document's formula against the engine's operators; no
    // code of this item computes the formula (SS 5 INV-7).
    const std::array<fc::Rotator, 5> rotations = {fc::Rotator{0, 0, 0}, fc::Rotator{4097, 0, 0},
                                                  fc::Rotator{0, 12345, 0}, fc::Rotator{0, 0, -7001},
                                                  fc::Rotator{1234, -5678, 9101}};
    const std::array<fc::Vec, 2> pivots = {fc::Vec{0, 0, 0}, fc::Vec{8, -3, 5}};
    const std::array<fc::Vec, 2> mainScales = {fc::Vec{1, 1, 1}, fc::Vec{2, -1, 0.5}};
    const std::array<fc::Vec, 2> postScales = {fc::Vec{1, 1, 2}, fc::Vec{-1, 1, 1}};
    const fc::Vec location{100, -200, 300};
    const std::array<fc::Vec, 4> points = {fc::Vec{64, 64, 64}, fc::Vec{-64, 64, -64},
                                           fc::Vec{64, -64, 0}, fc::Vec{-64, -64, 64}};

    double worst = 0;
    for (const fc::Rotator& rotation : rotations)
        for (const fc::Vec& pivot : pivots)
            for (const fc::Vec& main : mainScales)
                for (const fc::Vec& post : postScales) {
                    const fc::Coords world = fc::toWorld(location, rotation, pivot, fc::Scale{main},
                                                         fc::Scale{post}, fc::Trig::Exact);
                    for (const fc::Vec& p : points) {
                        const fc::Vec engine = fc::transformPointBy(p, world);
                        const fc::Vec q{main.x * (p.x - pivot.x), main.y * (p.y - pivot.y),
                                        main.z * (p.z - pivot.z)};
                        const fc::Vec formula = placed(q, location, rotation, post);
                        const fc::Vec d = engine - formula;
                        worst = std::max({worst, std::abs(d.x), std::abs(d.y), std::abs(d.z)});
                    }
                }
    CHECK(worst < 0.001);
}

TEST_CASE("INV-8: a texture only a mover wears gets its material made", "[ubake][movers]") {
    Fixture fixture = standardFixture();
    fixture.packageTextures.push_back(TextureSpec{"Hatch", "Metal", picture(5), false});
    MapBuilder& map = fixture.map;
    const std::int32_t hatch = map.importTexture("TexPkg", "Metal", "Hatch");
    map.addActor("Hatch0", map.importClass("Engine", "Mover"),
                 {objectProperty("Brush", map.addBrushModel(tiltedSquare(hatch)))});
    MemoryPackages packages = memoryPackagesFor(fixture);
    packages.add("engine", enginePackage());
    const std::vector<std::uint8_t> bytes = map.build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    uta::JobSystem jobs(2);
    const auto result = uta::ubake::detail::bake(*package, MAP_NAME, packages.resolver(), jobs,
                                                 &uta::umat::curated, uta::umat::TEXTURE_BUDGET_BYTES);
    REQUIRE(result.has_value());
    REQUIRE(result->bundle.materials.has_value());
    REQUIRE(result->bundle.movers.has_value());

    std::set<std::string> ids;
    for (const auto& record : *result->bundle.materials) ids.insert(record.id);
    CHECK(ids.contains("texpkg.metal.hatch"));
    bool wearsHatch = false;
    for (const MoverShape& shape : *result->bundle.movers)
        for (const auto& batch : shape.geometry.batches) {
            CHECK((batch.material.empty() || ids.contains(batch.material)));
            wearsHatch = wearsHatch || batch.material == "texpkg.metal.hatch";
        }
    CHECK(wearsHatch);
}

TEST_CASE("INV-9: a mover whose Brush names no Model refuses the bake", "[ubake][movers]") {
    MapBuilder map;
    const std::int32_t texture = map.addTexture(TextureSpec{"Wall", "", picture(1), false});
    map.addActor("Door0", map.importClass("Engine", "Mover"), {objectProperty("Brush", texture)});
    refusedFor(map.build(), "dm-fixture.door0");
}

TEST_CASE("INV-9: a mover whose Brush names an import refuses the bake", "[ubake][movers]") {
    MapBuilder map;
    const std::int32_t imported = map.importTexture("TexPkg", "Metal", "Plate");
    map.addActor("Door0", map.importClass("Engine", "Mover"), {objectProperty("Brush", imported)});
    refusedFor(map.build(), "dm-fixture.door0");
}

TEST_CASE("INV-9: a mover whose Model does not read refuses the bake", "[ubake][movers]") {
    MapBuilder map;
    const std::int32_t broken = map.addRawExport("Engine", "Model", "Broken", {1, 2, 3});
    map.addActor("Door0", map.importClass("Engine", "Mover"), {objectProperty("Brush", broken)});
    refusedFor(map.build(), "");
}

TEST_CASE("INV-9: a mover whose geometry does not read refuses the bake", "[ubake][movers]") {
    MapBuilder map;
    BrushSpec brush = tiltedSquare();
    brush.iSurf = 9; // past the Model's one surface
    map.addActor("Door0", map.importClass("Engine", "Mover"),
                 {objectProperty("Brush", map.addBrushModel(brush))});
    refusedFor(map.build(), "dm-fixture.door0");
}

TEST_CASE("INV-9: a mover whose MainScale has a zero component refuses the bake",
          "[ubake][movers]") {
    MapBuilder map;
    map.addActor("Door0", map.importClass("Engine", "Mover"),
                 {objectProperty("Brush", map.addBrushModel(tiltedSquare())),
                  scaleProperty("MainScale", 1, 0, 1)});
    refusedFor(map.build(), "dm-fixture.door0");
}
