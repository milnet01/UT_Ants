// UTA-0110's real-asset case: buildActors over every map in the install.
//
// docs/specs/UTA-0110-lights-and-placements.md SS 7. It PRINTS actors placed,
// distinct classes, classes ending PackageMissing and ClassMissing, lights,
// and maps refused by reason, so those figures are an output of the suite
// rather than a transcription in the roadmap. It asserts that every light's
// exportIndex has a placement (INV-9).
//
// ONE Install PER MAP -- UTA-0011 SS 4.11: an Install keeps every package it
// opened, and one Install across the library held every map's closure at once.

#include "core/FileSystem.h"
#include "ubake/Actors.h"
#include "ubake/Install.h"
#include "ubake/Name.h"
#include "ubundle/Bundle.h"
#include "upkg/Level.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifndef UTA_UT_INSTALL_DIR
#error "UTA_UT_INSTALL_DIR must be defined for the real-asset tier"
#endif

namespace {

namespace fs = std::filesystem;
namespace detail = uta::ubake::detail;
using uta::ubundle::AncestryEnd;
using uta::upkg::Package;

struct Totals {
    std::size_t maps = 0;
    std::size_t built = 0;
    std::size_t actors = 0;
    std::size_t classEntries = 0;
    std::set<std::string> distinctClasses;
    std::size_t packageMissing = 0; ///< the class itself not found: no package
    std::size_t classMissing = 0;   ///< the class itself not found: package without it
    std::size_t parentMissing = 0;  ///< found, with a parent that is not
    std::size_t lights = 0;
    std::size_t lightsWithoutPlacement = 0;
    double buildSeconds = 0;
    /// UTA-0110 SS 4.5 step 1: maps naming one export in two actor slots, and
    /// how many slots each skips.
    std::map<std::string, std::size_t> repeatedSlots;
    std::size_t placementsNotDistinct = 0; ///< maps whose placements are not their distinct exports
    std::map<std::string, std::size_t> refused;
    std::vector<std::string> refusals; ///< the first few messages, verbatim
};

std::string classOf(const Package& package, const uta::upkg::ExportEntry& entry) {
    if (entry.objectClass.kind() == uta::upkg::ObjectReferenceKind::Null) return {};
    const auto name = package.objectName(entry.objectClass);
    return name.has_value() ? detail::fold(*name) : std::string{};
}

void censusOf(const fs::path& root, const fs::path& mapPath, Totals& totals) {
    ++totals.maps;
    const std::string mapName = detail::mapNameOf(mapPath);
    const auto bytes = uta::fs::readFile(mapPath);
    if (!bytes.has_value()) {
        ++totals.refused["the map does not read"];
        return;
    }
    const auto map = Package::open(*bytes);
    if (!map.has_value()) {
        ++totals.refused["the map does not open"];
        return;
    }

    const uta::upkg::ExportEntry* levelExport = nullptr;
    for (const auto& entry : map->exports())
        if (classOf(*map, entry) == "level") levelExport = &entry;
    if (levelExport == nullptr) {
        ++totals.refused["no Level export"];
        return;
    }
    const auto level = uta::upkg::readLevel(*map, *levelExport);
    if (!level.has_value()) {
        ++totals.refused["readLevel refuses"];
        return;
    }

    auto install = uta::ubake::Install::open(root);
    REQUIRE(install.has_value());

    const auto started = std::chrono::steady_clock::now();
    const auto actors = uta::ubake::buildActors(*map, mapName, *level, install->resolver());
    totals.buildSeconds +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (!actors.has_value()) {
        ++totals.refused["buildActors refuses"];
        if (totals.refusals.size() < 5)
            totals.refusals.push_back(mapName + ": " + std::string(actors.error().message()));
        return;
    }
    ++totals.built;

    const auto& placed = actors->placements.actors;
    totals.actors += placed.size();

    // Every slot names an export, or the bake was refused above; so a map's
    // placements are its distinct exports, and the rest are repeats.
    std::set<std::int32_t> distinct;
    for (const uta::upkg::ObjectReference slot : level->actors) distinct.insert(slot.raw());
    if (distinct.size() != placed.size()) ++totals.placementsNotDistinct;
    if (const std::size_t repeats = level->actors.size() - distinct.size(); repeats > 0)
        totals.repeatedSlots[mapName] = repeats;
    totals.classEntries += actors->placements.classes.size();
    for (const auto& actorClass : actors->placements.classes) {
        totals.distinctClasses.insert(actorClass.path);
        if (!actorClass.resolved) {
            if (actorClass.end == AncestryEnd::PackageMissing) ++totals.packageMissing;
            if (actorClass.end == AncestryEnd::ClassMissing) ++totals.classMissing;
        } else if (actorClass.end != AncestryEnd::Root) {
            ++totals.parentMissing;
        }
    }

    // INV-9. The placements are strictly ascending by export index.
    totals.lights += actors->lights.size();
    for (const auto& light : actors->lights) {
        const auto found = std::lower_bound(
            placed.begin(), placed.end(), light.exportIndex,
            [](const auto& placement, std::uint32_t index) { return placement.exportIndex < index; });
        if (found == placed.end() || found->exportIndex != light.exportIndex)
            ++totals.lightsWithoutPlacement;
    }
}

} // namespace

TEST_CASE("every map's actors and lights bake and the census prints", "[real-assets][actors]") {
    const fs::path root{UTA_UT_INSTALL_DIR};
    std::vector<fs::path> maps;
    for (const fs::directory_entry& entry : fs::directory_iterator(root / "Maps"))
        if (entry.is_regular_file() && detail::fold(entry.path().extension().string()) == ".unr")
            maps.push_back(entry.path());
    std::sort(maps.begin(), maps.end());
    REQUIRE_FALSE(maps.empty());

    Totals totals;
    for (const fs::path& map : maps) censusOf(root, map, totals);

    std::cout << "UTA-0110 actors census over " << totals.maps << " maps, " << totals.built
              << " built in " << totals.buildSeconds << " s of buildActors\n"
              << "  actors placed " << totals.actors << "; class entries " << totals.classEntries
              << ", distinct classes " << totals.distinctClasses.size() << "\n"
              << "  classes not found: PackageMissing " << totals.packageMissing
              << ", ClassMissing " << totals.classMissing << "; found with a parent missing "
              << totals.parentMissing << "\n"
              << "  lights " << totals.lights << ", without a placement "
              << totals.lightsWithoutPlacement << "\n";
    for (const auto& [reason, count] : totals.refused)
        std::cout << "  maps refused, " << reason << ": " << count << "\n";
    for (const std::string& message : totals.refusals) std::cout << "    " << message << "\n";
    std::cout << "  maps naming one export in two actor slots: " << totals.repeatedSlots.size() << "\n";
    for (const auto& [map, repeats] : totals.repeatedSlots)
        std::cout << "    " << map << ": " << repeats << " skipped\n";

    CHECK(totals.built > 0);
    CHECK(totals.lightsWithoutPlacement == 0);
    CHECK(totals.placementsNotDistinct == 0);
}
