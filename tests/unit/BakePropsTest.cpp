// UTA-0341: which placed actors become props, and where they stand.
//
// docs/specs/UTA-0341-mesh-decorations.md SS 4.1 to SS 4.5, INV-3 to INV-8.
// Each case bakes a fixture level placing actors of classes the map itself
// declares under Engine.Decoration, wearing the fixture's cube mesh.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "BakeFixture.h"

#include "core/Jobs.h"
#include "support/FCoordsPort.h"
#include "ubake/Bake.h"
#include "ubake/Geometry.h"
#include "ubake/Props.h"
#include "ubundle/Bundle.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

using namespace uta::test::bake;
using uta::JobSystem;
using uta::ubake::BakeResult;
namespace detail = uta::ubake::detail;
namespace fcoords = uta::test::fcoords;

namespace {

/// The fixture's Engine with Engine.Decoration added -- as UTA-0256's lamp
/// cases build it.
std::vector<std::uint8_t> engineWithDecoration() {
    Packer packer;
    const std::int32_t brush = packer.addClass("Brush", 0, {boolProperty("bStatic", true)});
    packer.addClass("Mover", brush, {boolProperty("bStatic", false)});
    packer.addExport(0, 0, "Actor", {});
    packer.addClass("Decoration");
    return packer.build();
}

/// A map with one floor, a skin texture and the cube mesh wearing it; `place`
/// adds the actors. Returns the bake, or fails the case on a refusal.
struct Scene {
    Fixture fixture;
    std::int32_t skin = 0;     ///< the mesh's own texture
    std::int32_t mesh = 0;     ///< "Cube"
    std::int32_t propClass = 0;
};

Scene scene(const MeshSpec* custom = nullptr) {
    Scene out;
    MapBuilder& map = out.fixture.map;
    const std::int32_t floor = map.addTexture(TextureSpec{"Floor", "", picture(2), false});
    map.addSurface(floor);
    out.skin = map.addTexture(TextureSpec{"Skin", "", picture(1), false});
    out.mesh = map.addLodMesh("Cube", custom != nullptr ? *custom : cubeMesh(out.skin));
    const std::int32_t decoration = map.importClass("Engine", "Decoration");
    out.propClass = map.addClass("Prop", decoration, {byteProperty("DrawType", 2), objectProperty("Mesh", out.mesh)});
    return out;
}

BakeResult bake(Scene& built) {
    MemoryPackages packages = memoryPackagesFor(built.fixture);
    packages.add("engine", engineWithDecoration());
    JobSystem jobs(2);
    const detail::CuratedLookup nothingCurated = [](std::uint64_t) -> const uta::umat::CuratedOverride* {
        return nullptr;
    };
    const std::vector<std::byte> bytes = asByteVector(built.fixture.map.build());
    const auto opened = uta::upkg::Package::open(bytes);
    REQUIRE(opened.has_value());
    auto baked = detail::bake(*opened, MAP_NAME, packages.resolver(), jobs, nothingCurated,
                              uta::umat::TEXTURE_BUDGET_BYTES);
    if (!baked.has_value()) FAIL("the bake was refused: " << baked.error().message());
    return std::move(*baked);
}

std::size_t trianglesOf(const BakeResult& result) {
    return result.bundle.props ? result.bundle.props->indices.size() / 3 : 0;
}

/// Every PROP corner, by position.
std::vector<std::array<float, 3>> cornersOf(const BakeResult& result) {
    std::vector<std::array<float, 3>> out;
    for (const std::uint32_t index : result.bundle.props->indices)
        out.push_back(result.bundle.props->vertices[index].position);
    return out;
}

} // namespace

TEST_CASE("UTA-0341 INV-3: only decorations drawn as a mesh are props", "[ubake][props]") {
    Scene built = scene();
    MapBuilder& map = built.fixture.map;
    const std::int32_t decoration = map.importClass("Engine", "Decoration");
    const std::int32_t sprite =
        map.addClass("Sprite", decoration, {byteProperty("DrawType", 1), objectProperty("Mesh", built.mesh)});
    const std::int32_t statue = map.addClass("Statue", 0, {byteProperty("DrawType", 2), objectProperty("Mesh", built.mesh)});
    map.addActor("Prop1", built.propClass, {vectorProperty("Location", 0, 0, 32)});
    map.addActor("Sprite2", sprite, {vectorProperty("Location", 64, 0, 32)});
    map.addActor("Hidden3", built.propClass, {vectorProperty("Location", 128, 0, 32), boolProperty("bHidden", true)});
    map.addActor("Statue4", statue, {vectorProperty("Location", 192, 0, 32)});
    const BakeResult result = bake(built);
    // The cube's twelve triangles, once: the first actor alone.
    CHECK(trianglesOf(result) == 12);
    for (const auto& corner : cornersOf(result)) {
        CHECK(std::abs(corner[0]) <= 16.0F);
        CHECK(std::abs(corner[2] - 32.0F) <= 16.0F);
    }
    CHECK(result.skippedProps.empty());
}

TEST_CASE("UTA-0341 INV-4: a prop stands where UT99 places it", "[ubake][props]") {
    Scene built0 = scene();
    MeshSpec mesh = cubeMesh(built0.skin);
    mesh.scale = {1.0F, 2.0F, 0.5F};
    mesh.origin = {2.0F, 0.0F, -1.0F};
    mesh.rotOrigin = {0, 16384, 0};
    Scene built = scene(&mesh);
    const std::array<float, 3> location = {100, 200, 50};
    const std::array<float, 3> prePivot = {3, -4, 5};
    const std::array<std::int32_t, 3> rotation = {2000, 12000, -3000};
    const float drawScale = 1.5F;
    built.fixture.map.addActor(
        "Prop1", built.propClass,
        {vectorProperty("Location", location[0], location[1], location[2]),
         vectorProperty("PrePivot", prePivot[0], prePivot[1], prePivot[2]),
         rotatorProperty("Rotation", rotation[0], rotation[1], rotation[2]), floatProperty("DrawScale", drawScale)});
    const BakeResult result = bake(built);
    REQUIRE(trianglesOf(result) == 12);

    // UMesh::GetFrame's coordinates, through the engine's own FCoords algebra:
    // (Location + PrePivot) * Rotation * RotOrigin * FScale(Scale * DrawScale),
    // applied to V - Origin.
    fcoords::Coords coords;
    fcoords::multiply(coords, fcoords::Vec{location[0] + prePivot[0], location[1] + prePivot[1],
                                           location[2] + prePivot[2]});
    fcoords::multiply(coords, fcoords::Rotator{rotation[0], rotation[1], rotation[2]}, fcoords::Trig::Exact);
    fcoords::multiply(coords, fcoords::Rotator{mesh.rotOrigin[0], mesh.rotOrigin[1], mesh.rotOrigin[2]},
                      fcoords::Trig::Exact);
    fcoords::multiply(coords, fcoords::Scale{{mesh.scale[0] * drawScale, mesh.scale[1] * drawScale,
                                              mesh.scale[2] * drawScale}});
    std::vector<fcoords::Vec> expected;
    for (const auto& v : mesh.vertices)
        expected.push_back(fcoords::transformPointBy(
            {v[0] - mesh.origin[0], v[1] - mesh.origin[1], v[2] - mesh.origin[2]}, coords));

    const auto distance = [](const std::array<float, 3>& a, const fcoords::Vec& b) {
        return std::hypot(a[0] - b.x, a[1] - b.y, a[2] - b.z);
    };
    std::vector<bool> hit(expected.size(), false);
    for (const auto& corner : cornersOf(result)) {
        double nearest = std::numeric_limits<double>::max();
        std::size_t which = 0;
        for (std::size_t i = 0; i < expected.size(); ++i)
            if (const double d = distance(corner, expected[i]); d < nearest) {
                nearest = d;
                which = i;
            }
        CHECK(nearest < 0.01);
        hit[which] = true;
    }
    for (const bool each : hit) CHECK(each);
}

TEST_CASE("UTA-0341 INV-5: a mirrored prop still faces out", "[ubake][props]") {
    Scene built0 = scene();
    MeshSpec mesh = cubeMesh(built0.skin);
    mesh.scale = {-1.0F, 1.0F, 1.0F};
    Scene built = scene(&mesh);
    built.fixture.map.addActor("Prop1", built.propClass, {vectorProperty("Location", 0, 0, 64)});
    const BakeResult result = bake(built);
    REQUIRE(trianglesOf(result) == 12);
    const std::vector<std::array<float, 3>> corners = cornersOf(result);
    for (std::size_t t = 0; t + 2 < corners.size(); t += 3) {
        const auto& a = corners[t];
        const auto& b = corners[t + 1];
        const auto& c = corners[t + 2];
        const std::array<double, 3> e1 = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const std::array<double, 3> e2 = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const std::array<double, 3> n = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                                         e1[0] * e2[1] - e1[1] * e2[0]};
        // From the cube's centre, (0, 0, 64), out through the triangle.
        const std::array<double, 3> out = {(a[0] + b[0] + c[0]) / 3, (a[1] + b[1] + c[1]) / 3,
                                           (a[2] + b[2] + c[2]) / 3 - 64};
        CHECK(n[0] * out[0] + n[1] * out[1] + n[2] * out[2] > 0);
    }
}

TEST_CASE("UTA-0341 INV-6: MultiSkins then Skin then the mesh's own", "[ubake][props]") {
    const auto materialOf = [](const std::function<void(Scene&, std::int32_t, std::int32_t)>& place) {
        Scene built = scene();
        MapBuilder& map = built.fixture.map;
        const std::int32_t multi = map.addTexture(TextureSpec{"Multi", "", picture(3), false});
        const std::int32_t skin = map.addTexture(TextureSpec{"Worn", "", picture(4), false});
        place(built, multi, skin);
        const BakeResult result = bake(built);
        REQUIRE(result.bundle.props.has_value());
        REQUIRE(result.bundle.props->batches.size() == 1);
        return result.bundle.props->batches[0].material;
    };
    CHECK(materialOf([](Scene& s, std::int32_t multi, std::int32_t skin) {
              s.fixture.map.addActor("Prop1", s.propClass,
                                     {objectProperty("MultiSkins", multi), objectProperty("Skin", skin)});
          }) == "dm-fixture.multi");
    CHECK(materialOf([](Scene& s, std::int32_t, std::int32_t skin) {
              s.fixture.map.addActor("Prop1", s.propClass, {objectProperty("Skin", skin)});
          }) == "dm-fixture.worn");
    CHECK(materialOf([](Scene& s, std::int32_t, std::int32_t) { s.fixture.map.addActor("Prop1", s.propClass); })
          == "dm-fixture.skin");
}

TEST_CASE("UTA-0341 INV-7: Style and bUnlit reach the flags", "[ubake][props]") {
    Scene built = scene();
    built.fixture.map.addActor("Prop1", built.propClass, {byteProperty("Style", 4), boolProperty("bUnlit", true)});
    const BakeResult result = bake(built);
    REQUIRE(result.bundle.props.has_value());
    REQUIRE_FALSE(result.bundle.props->batches.empty());
    for (const auto& batch : result.bundle.props->batches) {
        CHECK((batch.polyFlags & uta::ubake::PF_TRANSLUCENT) != 0);
        CHECK((batch.polyFlags & uta::ubake::PF_UNLIT) != 0);
    }
}

TEST_CASE("UTA-0341 INV-8: a prop that does not read never refuses the map", "[ubake][props]") {
    Scene built = scene();
    MapBuilder& map = built.fixture.map;
    const std::int32_t missing = map.importTexture("NoSuchPkg", "", "Gone", "LodMesh");
    map.addActor("Prop1", built.propClass, {objectProperty("Mesh", missing)});
    const BakeResult result = bake(built);
    CHECK_FALSE(result.bundle.props.has_value());
    REQUIRE(result.skippedProps.size() == 1);
    CHECK(result.skippedProps[0].actor.find("prop1") != std::string::npos);
}

TEST_CASE("UTA-0341 INV-13: a skin only props wear is enlarged at most PROP_UPSCALE", "[ubake][props]") {
    // The floor's picture is worn by a surface; the cube's skin by the prop
    // alone. Each picture is 4 texels square.
    Scene built = scene();
    built.fixture.map.addActor("Prop1", built.propClass);
    const BakeResult result = bake(built);
    REQUIRE(result.bundle.textures.has_value());
    const auto factorOf = [&result](const std::string& name) -> std::uint32_t {
        for (const auto& texture : *result.bundle.textures)
            if (texture.name == name) return texture.width / texture.sourceWidth;
        FAIL("no texture " << name);
        return 0;
    };
    CHECK(factorOf("dm-fixture.skin:base") <= uta::ubake::PROP_UPSCALE);
    // The cap is the prop's alone: the floor is enlarged past it.
    CHECK(factorOf("dm-fixture.floor:base") > uta::ubake::PROP_UPSCALE);
}

TEST_CASE("UTA-0341 INV-14: a prop drawn with the older Mesh class is a prop", "[ubake][props]") {
    Scene built = scene();
    MapBuilder& map = built.fixture.map;
    const std::int32_t old = map.addMesh("OldCube", cubeMesh(built.skin));
    map.addActor("Prop1", built.propClass, {objectProperty("Mesh", old)});
    const BakeResult result = bake(built);
    CHECK(trianglesOf(result) == 12);
    CHECK(result.skippedProps.empty());
}
