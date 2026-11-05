// UTA-0341: the LodMesh reader on a fixture export.
//
// docs/specs/UTA-0341-mesh-decorations.md SS 4.2, INV-2. INV-1, which reads
// real meshes, is in tests/real/RealMeshTest.cpp.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "BakeFixture.h"

#include "upkg/Mesh.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <set>
#include <string_view>
#include <tuple>
#include <vector>

using namespace uta::test::bake;

namespace {

/// The export of the opened package named `name`.
const uta::upkg::ExportEntry& exportNamed(const uta::upkg::Package& package, std::string_view name) {
    for (const uta::upkg::ExportEntry& entry : package.exports())
        if (package.name(entry.objectName).value_or("") == name) return entry;
    FAIL("no export named " << name);
    return package.exports().front();
}

} // namespace

TEST_CASE("UTA-0341: the packed vertex keeps each field's sign", "[upkg][mesh]") {
    using uta::upkg::unpackMeshVertex;
    // X -1 (all eleven bits), Y 1023 (the largest), Z -512 (the smallest).
    const std::uint32_t packed = 0x7FFu | (0x3FFu << 11) | (0x200u << 22);
    const auto vertex = unpackMeshVertex(packed);
    CHECK(vertex[0] == -1);
    CHECK(vertex[1] == 1023);
    CHECK(vertex[2] == -512);
}

TEST_CASE("UTA-0341 INV-2: the reader reads a whole export and refuses any other", "[upkg][mesh]") {
    MapBuilder map;
    const std::int32_t texture = map.addTexture(TextureSpec{"Skin", "", picture(1), false});
    const MeshSpec cube = cubeMesh(texture);
    map.addLodMesh("Cube", cube);
    std::vector<std::uint8_t> padded = map.lodMeshBytes(cube);
    padded.push_back(0);
    map.addRawExport("Engine", "LodMesh", "Padded", padded);
    std::vector<std::uint8_t> cut = map.lodMeshBytes(cube);
    cut.pop_back();
    map.addRawExport("Engine", "LodMesh", "Cut", cut);
    const std::vector<std::byte> bytes = asByteVector(map.build());
    const auto package = uta::upkg::Package::open(bytes);
    REQUIRE(package.has_value());

    const auto mesh = uta::upkg::readLodMesh(*package, exportNamed(*package, "Cube"));
    if (!mesh.has_value()) FAIL(mesh.error().message());
    CHECK(mesh->frameVerts == 8);
    CHECK(mesh->animFrames == 1);
    CHECK(mesh->faces.size() == 12);
    CHECK(mesh->wedges.size() == 24);
    CHECK(mesh->materials.size() == 1);
    REQUIRE(mesh->textures.size() == 1);
    CHECK(mesh->textures[0].raw() == texture);
    REQUIRE(mesh->sequences.size() == 1);
    CHECK(mesh->sequences[0].name == "All");
    CHECK(mesh->vertices[7] == std::array<std::int16_t, 3>{16, 16, 16});

    CHECK_FALSE(uta::upkg::readLodMesh(*package, exportNamed(*package, "Padded")).has_value());
    CHECK_FALSE(uta::upkg::readLodMesh(*package, exportNamed(*package, "Cut")).has_value());
    CHECK_FALSE(uta::upkg::readLodMesh(*package, exportNamed(*package, "Skin")).has_value());
}

TEST_CASE("UTA-0341 INV-14: a Mesh reads as the same faces as a LodMesh", "[upkg][mesh]") {
    // UTA-0450: the older class keeps its triangles in Tris.
    MapBuilder map;
    const std::int32_t texture = map.addTexture(TextureSpec{"Skin", "", picture(1), false});
    const MeshSpec cube = cubeMesh(texture);
    map.addMesh("Old", cube);
    map.addLodMesh("New", cube);
    std::vector<std::uint8_t> padded = map.lodMeshBytes(cube, false);
    padded.push_back(0);
    map.addRawExport("Engine", "Mesh", "Padded", padded);
    const std::vector<std::byte> bytes = asByteVector(map.build());
    const auto package = uta::upkg::Package::open(bytes);
    REQUIRE(package.has_value());

    const auto old = uta::upkg::readLodMesh(*package, exportNamed(*package, "Old"));
    if (!old.has_value()) FAIL(old.error().message());
    const auto lod = uta::upkg::readLodMesh(*package, exportNamed(*package, "New"));
    REQUIRE(lod.has_value());
    CHECK(old->vertices == lod->vertices);
    REQUIRE(old->faces.size() == lod->faces.size());
    CHECK(old->materials.size() == 1);
    // The same corners, face by face: the wedges may be numbered apart.
    for (std::size_t f = 0; f < lod->faces.size(); ++f)
        for (std::size_t k = 0; k < 3; ++k) {
            const auto& a = old->wedges[old->faces[f].wedges[k]];
            const auto& b = lod->wedges[lod->faces[f].wedges[k]];
            CHECK(a.vertex == b.vertex);
            CHECK(a.u == b.u);
            CHECK(a.v == b.v);
        }
    // A corner shared by position and texture coordinate is one wedge: as
    // many as the cube has distinct ones, fewer than its 36 face corners.
    std::set<std::tuple<std::uint16_t, std::uint8_t, std::uint8_t>> distinct;
    for (const auto& wedge : cube.wedges) distinct.emplace(wedge.vertex, wedge.u, wedge.v);
    CHECK(old->wedges.size() == distinct.size());
    CHECK(old->wedges.size() < 36);
    CHECK_FALSE(uta::upkg::readLodMesh(*package, exportNamed(*package, "Padded")).has_value());
}
