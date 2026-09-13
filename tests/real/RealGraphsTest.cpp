// The real-asset tier's graph cases -- UTA-0057 SS 4.6's Paths question and
// UTA-0006 SS 7 tier 3, split out of RealInstallTest.cpp by UTA-0103.
//
// UTA-0006 SS 7 tier 3 adds the graphs case: it builds both graphs for every map
// and PRINTS the figures that spec's SS 2.1 asserts, so those numbers are an
// output of the suite rather than a transcription in a document. What it
// asserts is a population and two rates, aggregated over the whole install --
// never per map, because a level whose path network disagrees with its own
// nodes is content rather than a builder defect.

#include "upkg/Class.h"
#include "upkg/Level.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"
#include "unav/Build.h"
#include "unav/Graphs.h"
#include "real/RealSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;
using namespace uta::test::real;

// --- UTA-0057 SS 4.6: are Paths values indices into the reach-spec array? ----
//
// INV-2 and INV-3. They have no fixture that could establish them: they are
// claims about content this project did not write, and a fixture would only
// assert what the fixture builder was told.
//
// Both are stated as RATES rather than as universal claims, and the reason is
// measurement rather than caution. Over the reference install every entry
// resolves on the overwhelming majority of maps, and the failures are confined
// to a handful whose path network disagrees with their own navigation points --
// maps carrying Paths values with NO reach-spec array at all, and maps whose
// network was built and then edited. That is content disagreeing with itself,
// which SS 6 already refuses to treat as a refusal.
//
// A rate is what the texture case above uses for the same shape, and it is
// scale-free: unlike a floor on a count it does not go stale as the library
// grows. It still catches what these invariants exist for. A reader that
// partitions the array wrongly, or that transposes a spec's start and end,
// does not lose a few tenths of a percent -- it loses nearly all of them.

namespace {

/// Does this actor's class descend from `NavigationPoint`?
///
/// SS 4.6: no export of the literal class carries a Paths entry, so an exact
/// name match resolves nothing and a name LIST does not close -- the corpus
/// was still introducing Paths-carrying classes in its last tenth. Ancestry is
/// the only approach that survives, which is why this needs UTA-0005.
bool descendsFromNavigationPoint(const uta::upkg::Package& package,
                                 const uta::upkg::ExportEntry& actor,
                                 const uta::upkg::PackageResolver& resolver,
                                 const std::map<std::string, fs::path>& systemPackages,
                                 std::map<std::string, bool>& cache);

/// The package an import ultimately lives in: follow its outer chain to the
/// root, which is where the package name sits.
std::string importPackageName(const uta::upkg::Package& package,
                              uta::upkg::ObjectReference reference) {
    for (int depth = 0; depth < 32; ++depth) {
        if (reference.kind() != uta::upkg::ObjectReferenceKind::Import) {
            return {};
        }
        const uta::upkg::ImportEntry& import = package.imports()[reference.index()];
        if (import.outer.kind() == uta::upkg::ObjectReferenceKind::Null) {
            const auto name = package.name(import.objectName);
            return name.has_value() ? std::string{*name} : std::string{};
        }
        reference = import.outer;
    }
    return {};
}

bool descendsFromNavigationPoint(const uta::upkg::Package& package,
                                 const uta::upkg::ExportEntry& actor,
                                 const uta::upkg::PackageResolver& resolver,
                                 const std::map<std::string, fs::path>& systemPackages,
                                 std::map<std::string, bool>& cache) {
    (void)systemPackages;
    const auto className = package.objectName(actor.objectClass);
    if (!className.has_value()) {
        return false;
    }
    const bool imported = actor.objectClass.kind() == uta::upkg::ObjectReferenceKind::Import;
    const std::string home =
        imported ? foldCase(importPackageName(package, actor.objectClass)) : "<map>";
    // Cached per CLASS, not per actor: a map holds thousands of actors of a
    // few dozen classes, and the walk is what costs.
    const std::string key = home + "/" + std::string{*className};
    if (const auto found = cache.find(key); found != cache.end()) {
        return found->second;
    }

    const uta::upkg::Package* classHome = nullptr;
    const uta::upkg::ExportEntry* classExport = nullptr;
    if (!imported) {
        classHome = &package;
        classExport = &package.exports()[actor.objectClass.index()];
    } else if (!home.empty()) {
        const auto resolved = resolver(home);
        if (resolved.has_value() && *resolved != nullptr) {
            for (const auto& candidate : (*resolved)->exports()) {
                if (!isClassExport(candidate)) {
                    continue;
                }
                const auto name = (*resolved)->name(candidate.objectName);
                if (name.has_value() && *name == *className) {
                    classHome = *resolved;
                    classExport = &candidate;
                    break;
                }
            }
        }
    }

    bool descends = false;
    if (classHome != nullptr && classExport != nullptr) {
        const auto ancestry = uta::upkg::readAncestry(*classHome, *classExport, resolver);
        if (ancestry.has_value()) {
            for (const auto& link : ancestry->chain) {
                const auto name = link.package->name(link.entry->objectName);
                if (name.has_value() && *name == "NavigationPoint") {
                    descends = true;
                    break;
                }
            }
        }
    }
    return cache.emplace(key, descends).first->second;
}

} // namespace

TEST_CASE("a navigation point's Paths entries index the level's reach-spec array",
          "[real-assets]") {
    const fs::path maps = fs::path{UTA_UT_INSTALL_DIR} / "Maps";
    const fs::path system = fs::path{UTA_UT_INSTALL_DIR} / "System";
    if (!fs::exists(maps) || !fs::exists(system)) {
        SUCCEED("no Maps or System directory in this install");
        return;
    }

    // SystemPackages owns the lifetime of what its resolver returns.
    SystemPackages packages{system};
    REQUIRE_FALSE(packages.empty());
    const uta::upkg::PackageResolver resolver = packages.resolver();

    std::map<std::string, bool> navigationClasses;
    long long entries = 0;
    long long inRange = 0;
    long long startIsTheNode = 0;
    long long navigationPoints = 0;
    long long negatives = 0;
    long long actorsUsingEverySlot = 0;

    for (const fs::directory_entry& entry : fs::directory_iterator(maps)) {
        if (!entry.is_regular_file() || foldCase(entry.path().extension().string()) != ".unr") {
            continue;
        }
        const std::vector<std::byte> raw = readWhole(entry.path());
        const auto package = uta::upkg::Package::open(viewOf(raw));
        if (!package.has_value()) {
            continue; // an earlier case owns which packages may fail to open
        }

        const uta::upkg::ExportEntry* levelExport = nullptr;
        for (const auto& object : package->exports()) {
            const auto className = package->objectName(object.objectClass);
            if (className.has_value() && *className == "Level") {
                levelExport = &object;
                break;
            }
        }
        if (levelExport == nullptr) {
            continue;
        }
        INFO("map: " << entry.path().string());
        const auto level = uta::upkg::readLevel(*package, *levelExport);
        REQUIRE(level.has_value());

        for (std::size_t index = 0; index < package->exports().size(); ++index) {
            const uta::upkg::ExportEntry& actor = package->exports()[index];
            if (actor.objectClass.kind() == uta::upkg::ObjectReferenceKind::Null) {
                continue;
            }
            if (!descendsFromNavigationPoint(*package, actor, resolver, packages.paths(),
                                             navigationClasses)) {
                continue;
            }
            ++navigationPoints;

            const auto properties = uta::upkg::readProperties(*package, actor);
            if (!properties.has_value()) {
                continue; // that layer is UTA-0003's, and SS 6 keeps its row
            }
            int slotsPresent = 0;
            for (const auto& property : *properties) {
                const auto name = package->name(property.nameIndex);
                if (!name.has_value() || *name != "Paths") {
                    continue;
                }
                ++slotsPresent;
                if (!std::holds_alternative<std::int32_t>(property.value)) {
                    continue;
                }
                const std::int32_t value = std::get<std::int32_t>(property.value);
                ++entries;
                if (value < 0) {
                    ++negatives;
                    continue;
                }
                if (static_cast<std::size_t>(value) >= level->reachSpecs.size()) {
                    continue;
                }
                ++inRange;
                // INV-3, and it is the check that separates a reader consuming
                // the right bytes from one assigning them to the right fields.
                const uta::upkg::ObjectReference start =
                    level->reachSpecs[static_cast<std::size_t>(value)].start;
                if (start.kind() == uta::upkg::ObjectReferenceKind::Export &&
                    start.index() == static_cast<std::uint32_t>(index)) {
                    ++startIsTheNode;
                }
            }
            // SS 4.6: if EVERY navigation point returned sixteen slots, unused
            // entries would be stored as a sentinel and the absent-property
            // rule would be wrong. Reported so the answer is re-derived rather
            // than trusted.
            if (slotsPresent == 16) {
                ++actorsUsingEverySlot;
            }
        }
    }

    WARN("navigation points " << navigationPoints << ", Paths entries " << entries
                              << ", in range " << inRange << ", start is the node "
                              << startIsTheNode << ", negative values " << negatives
                              << ", actors using all sixteen slots " << actorsUsingEverySlot);

    // SS 7: the tier asserts its own population, or both invariants pass
    // vacuously. A property-reading regression, or a class filter that
    // resolves nothing, surfaces no entries and satisfies every rate below by
    // checking nothing.
    REQUIRE(entries > 0);
    REQUIRE(navigationPoints > 0);

    // INV-2 and INV-3. Ninety-nine percent is a floor, not the measurement:
    // what is measured is far higher, and what a reader defect produces is far
    // lower. The gap between those two is what makes the floor stable.
    CHECK(inRange * 100 >= entries * 99);
    CHECK(startIsTheNode * 100 >= entries * 99);
}

TEST_CASE("both graphs build over every map, and the rates SS 2.1 measured hold",
          "[real-assets]") {
    const fs::path maps = fs::path{UTA_UT_INSTALL_DIR} / "Maps";
    const fs::path system = fs::path{UTA_UT_INSTALL_DIR} / "System";
    if (!fs::exists(maps) || !fs::exists(system)) {
        SUCCEED("no Maps or System directory in this install");
        return;
    }

    SystemPackages packages{system};
    REQUIRE_FALSE(packages.empty());
    const uta::upkg::PackageResolver resolver = packages.resolver();

    long long levels = 0;
    long long navNodes = 0;
    long long navEdges = 0;
    long long endpoints = 0;
    long long discardedEndpoints = 0;
    long long wiringNodes = 0;
    long long wiringEdges = 0;
    long long resolvedEvents = 0;
    long long danglingEvents = 0;
    // SS 13: the per-map maxima are printed here rather than asserted in the
    // document, which is what keeps the worst case a measurement.
    long long widestNavMap = 0;
    long long widestWiringMap = 0;

    for (const fs::directory_entry& entry : fs::directory_iterator(maps)) {
        if (!entry.is_regular_file() || foldCase(entry.path().extension().string()) != ".unr") {
            continue;
        }
        const std::vector<std::byte> raw = readWhole(entry.path());
        const auto package = uta::upkg::Package::open(viewOf(raw));
        if (!package.has_value()) {
            continue; // an earlier case owns which packages may fail to open
        }
        INFO("map: " << entry.path().string());

        const uta::upkg::ExportEntry* levelExport = nullptr;
        for (const auto& object : package->exports()) {
            const auto className = package->objectName(object.objectClass);
            if (className.has_value() && *className == "Level") {
                levelExport = &object;
                break;
            }
        }
        if (levelExport == nullptr) {
            continue;
        }
        const auto level = uta::upkg::readLevel(*package, *levelExport);
        REQUIRE(level.has_value());
        ++levels;

        const auto nav = uta::unav::buildNavGraph(*package, *level, resolver);
        REQUIRE(nav.has_value());
        const auto wiring = uta::unav::buildWiringGraph(*package);
        REQUIRE(wiring.has_value());

        navNodes += static_cast<long long>(nav->nodes.size());
        navEdges += static_cast<long long>(nav->edges.size());
        // Two per spec, which is the denominator SS 2.1 measures against.
        endpoints += 2 * static_cast<long long>(level->reachSpecs.size());
        discardedEndpoints += nav->discardedEndpoints;
        widestNavMap = std::max(widestNavMap, static_cast<long long>(nav->edges.size()));

        wiringNodes += static_cast<long long>(wiring->nodes.size());
        wiringEdges += static_cast<long long>(wiring->edges.size());
        danglingEvents += static_cast<long long>(wiring->dangling.size());
        widestWiringMap =
            std::max(widestWiringMap, static_cast<long long>(wiring->edges.size()));

        // An event that resolved yields ONE EDGE PER TARGET (INV-3), so the
        // edge count is not the event count. The distinct (source, event) pairs
        // are, which is also how a consumer would recover them.
        std::set<std::pair<std::uint32_t, std::string>> firedAndResolved;
        for (const uta::unav::WiringEdge& edge : wiring->edges) {
            firedAndResolved.emplace(edge.from, edge.event);
        }
        resolvedEvents += static_cast<long long>(firedAndResolved.size());
    }

    const long long resolvedEndpoints = endpoints - discardedEndpoints;
    const long long events = resolvedEvents + danglingEvents;

    WARN("levels " << levels << "; nav nodes " << navNodes << ", edges " << navEdges
                   << ", endpoints " << endpoints << ", discarded " << discardedEndpoints
                   << ", widest map " << widestNavMap << " edges"
                   << "; wiring nodes " << wiringNodes << ", edges " << wiringEdges
                   << ", events " << events << ", resolved " << resolvedEvents
                   << ", dangling " << danglingEvents << ", widest map " << widestWiringMap
                   << " edges");

    // SS 7: the tier asserts its own population, or every rate below passes
    // vacuously. A node filter that resolves nothing, or a property-reading
    // regression, surfaces no graph and satisfies a rate by checking nothing.
    REQUIRE(levels > 0);
    REQUIRE(navEdges > 0);
    REQUIRE(resolvedEvents > 0);

    // Both floors sit well under their measurement and well over what a defect
    // produces: a builder using the wrong index space does not lose a few
    // percent of endpoints but nearly all of them.
    CHECK(resolvedEndpoints * 100 >= endpoints * 95);
    CHECK(resolvedEvents * 100 >= events * 90);
}
