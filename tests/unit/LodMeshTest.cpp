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
#include <string_view>
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
