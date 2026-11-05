// UTA-0341 INV-1: the LodMesh layout reads real meshes.
//
// docs/specs/UTA-0341-mesh-decorations.md SS 4.2. The layout was derived by
// a scratch parser; this ships that measurement, so the spec's figure is
// re-derived on every run rather than transcribed.

#include "real/RealSupport.h"

#include "upkg/Mesh.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using namespace uta;
using namespace uta::test::real;

const std::filesystem::path SYSTEM = std::filesystem::path{UTA_UT_INSTALL_DIR} / "System";

} // namespace

TEST_CASE("UTA-0341 INV-1: Botpack's pipe reads inside its own box", "[real][mesh]") {
    const std::vector<std::byte> raw = readWhole(SYSTEM / "Botpack.u");
    const auto package = upkg::Package::open(viewOf(raw));
    REQUIRE(package.has_value());
    const upkg::ExportEntry* pipe = nullptr;
    for (const upkg::ExportEntry& entry : package->exports())
        if (package->name(entry.objectName).value_or("") == "pipeM") pipe = &entry;
    REQUIRE(pipe != nullptr);

    const auto mesh = upkg::readLodMesh(*package, *pipe);
    REQUIRE(mesh.has_value());
    CHECK(mesh->frameVerts == 62);
    CHECK(mesh->animFrames == 1);
    // Every vertex inside the stored box, and the box's every face touched:
    // a field read at the wrong width, or the packing decoded in the wrong
    // order, moves the vertices off it.
    std::array<bool, 6> touched{};
    const std::array<float, 3> low = {mesh->bounds.min.x, mesh->bounds.min.y, mesh->bounds.min.z};
    const std::array<float, 3> high = {mesh->bounds.max.x, mesh->bounds.max.y, mesh->bounds.max.z};
    for (const auto& vertex : mesh->vertices)
        for (std::size_t axis = 0; axis < 3; ++axis) {
            CHECK(vertex[axis] >= low[axis]);
            CHECK(vertex[axis] <= high[axis]);
            touched[axis] = touched[axis] || vertex[axis] == low[axis];
            touched[axis + 3] = touched[axis + 3] || vertex[axis] == high[axis];
        }
    for (const bool face : touched) CHECK(face);
}

TEST_CASE("UTA-0341 INV-1: every stock LodMesh reads", "[real][mesh]") {
    std::size_t read = 0;
    for (const char* name : {"Botpack.u", "UnrealShare.u", "UnrealI.u"}) {
        const std::vector<std::byte> raw = readWhole(SYSTEM / name);
        const auto package = upkg::Package::open(viewOf(raw));
        REQUIRE(package.has_value());
        for (const upkg::ExportEntry& entry : package->exports()) {
            if (package->objectName(entry.objectClass).value_or("") != "LodMesh") continue;
            const auto mesh = upkg::readLodMesh(*package, entry);
            if (!mesh.has_value())
                FAIL(name << " " << package->name(entry.objectName).value_or("?") << ": "
                          << mesh.error().message());
            ++read;
        }
    }
    INFO("LodMesh exports read: " << read);
    CHECK(read == 537); // SS 4.2's measurement, 2026-11-05
}

TEST_CASE("UTA-0341 INV-14: every Mesh of SPVale reads", "[real][mesh]") {
    // UTA-0450: MH-Vale_Evelyn's own package, whose props use the older class.
    const std::vector<std::byte> raw = readWhole(SYSTEM / "SPVale.u");
    const auto package = upkg::Package::open(viewOf(raw));
    REQUIRE(package.has_value());
    std::size_t read = 0;
    for (const upkg::ExportEntry& entry : package->exports()) {
        if (package->objectName(entry.objectClass).value_or("") != "Mesh") continue;
        const auto mesh = upkg::readLodMesh(*package, entry);
        if (!mesh.has_value())
            FAIL(package->name(entry.objectName).value_or("?") << ": " << mesh.error().message());
        CHECK_FALSE(mesh->faces.empty());
        ++read;
    }
    CHECK(read == 24); // UTA-0450's measurement, 2026-11-05
}
