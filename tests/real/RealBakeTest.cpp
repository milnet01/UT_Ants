// The real-asset tier's baker case -- UTA-0011, split out of
// RealInstallTest.cpp by UTA-0103.

#include "core/Jobs.h"
#include "core/Sha256.h"
#include "ubake/Bake.h"
#include "ubake/Install.h"
#include "ubake/Name.h"
#include "ubundle/Bundle.h"
#include "upkg/Level.h"
#include "upkg/Package.h"
#include "real/RealSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace uta::test::real;

// --- UTA-0011: the baker over the install -----------------------------------
//
// docs/specs/UTA-0011-map-baker.md SS 7's real-asset case. Every map in the
// install's Maps is named, which runs SS 4.4's closure walk over real import
// tables. Two figures SS 10 marks Partial are printed: how many maps hold
// more than one Level export, and how many levels name a Model other than the
// largest. Then one stock map is baked twice and the bytes compared.

TEST_CASE("every map takes a bake name and a stock map bakes the same twice",
          "[real-assets][ubake]") {
    const fs::path root{UTA_UT_INSTALL_DIR};
    auto install = uta::ubake::Install::open(root);
    REQUIRE(install.has_value());

    long long named = 0;
    long long unnamed = 0;
    long long noLevel = 0;
    long long manyLevels = 0;
    long long notLargest = 0;
    std::vector<std::string> unnamedExamples;
    for (const fs::path& map : sortedPackages(root / "Maps", ".unr")) {
        // One Install per map, as bakeToDirectory opens one per request. An
        // Install keeps every package it opened (SS 13), so one shared across
        // the whole library held every map's closure and ran this machine out
        // of memory on 2026-09-10.
        auto perMap = uta::ubake::Install::open(root);
        REQUIRE(perMap.has_value());
        const auto name = uta::ubake::bakeName(map, *perMap);
        if (!name.has_value()) {
            ++unnamed;
            if (unnamedExamples.size() < 5)
                unnamedExamples.push_back(map.filename().string() + ": "
                                          + std::string(name.error().message()));
            continue;
        }
        ++named;

        const std::vector<std::byte> raw = readWhole(map);
        const auto package = uta::upkg::Package::open(viewOf(raw));
        if (!package.has_value()) continue;
        std::vector<const uta::upkg::ExportEntry*> levels;
        std::optional<std::uint32_t> largest;
        for (std::uint32_t i = 0; i < package->exports().size(); ++i) {
            const uta::upkg::ExportEntry& entry = package->exports()[i];
            const auto className = package->objectName(entry.objectClass);
            if (!className.has_value()) continue;
            if (*className == "Level") levels.push_back(&entry);
            if (*className == "Model"
                && (!largest.has_value()
                    || entry.serialSize > package->exports()[*largest].serialSize))
                largest = i;
        }
        if (levels.empty()) {
            ++noLevel;
            continue;
        }
        if (levels.size() > 1) {
            ++manyLevels;
            continue;
        }
        const auto level = uta::upkg::readLevel(*package, *levels.front());
        if (level.has_value() && largest.has_value()
            && level->model.kind() == uta::upkg::ObjectReferenceKind::Export
            && level->model.index() != *largest)
            ++notLargest;
    }
    WARN("UTA-0011 SS 10 -- maps named " << named << ", not named " << unnamed
                                        << "; with no Level " << noLevel << ", with more than one "
                                        << manyLevels << "; levels naming a Model other than "
                                        << "the largest " << notLargest);
    for (const std::string& example : unnamedExamples) WARN("not named: " << example);
    REQUIRE(named > 0);

    const fs::path stock = root / "Maps" / "DM-Deck16][.unr";
    REQUIRE(fs::is_regular_file(stock));
    const std::vector<std::byte> raw = readWhole(stock);
    const auto package = uta::upkg::Package::open(viewOf(raw));
    REQUIRE(package.has_value());

    // Digests, not the bundles: two whole bundles held at once is memory the
    // comparison does not need.
    uta::JobSystem jobs;
    std::vector<std::string> digests;
    for (int run = 0; run < 2; ++run) {
        const auto result = uta::ubake::bake(*package, uta::ubake::detail::mapNameOf(stock),
                                             *install, jobs);
        if (!result.has_value()) FAIL("DM-Deck16][ was refused: " << result.error().message());
        if (run == 0)
            WARN("DM-Deck16][ -- materials " << result->bundle.materials->size() << ", skipped "
                                            << result->skipped.size() << ", rooms "
                                            << result->bundle.rooms->rooms.size()
                                            << " of which without a footprint "
                                            << result->rooms.roomsWithoutFootprint.size()
                                            << ", nav nodes " << result->bundle.nav->nodes.size()
                                            << ", working set " << result->budget.workingSetBytes
                                            << " of " << result->budget.budgetBytes);
        const auto bytes = uta::ubundle::write(result->bundle);
        REQUIRE(bytes.has_value());
        digests.push_back(uta::ubake::detail::hex(uta::sha256(*bytes)));
    }
    CHECK(digests[0] == digests[1]);
}
