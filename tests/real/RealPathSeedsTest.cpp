// UTA-0121's real-asset case: ut-paths' scene, walk graph and routes over
// every map in the install holding a MonsterEnd.
//
// docs/specs/UTA-0121-bot-path-seeds.md SS 7. It PRINTS what that spec's SS 3
// decisions 4 and 6 rest on, so they are an output of the suite rather than a
// transcription: spots found; the floor's normal Z under every PlayerStart
// and PathNode, which is what would show 0.7 too strict; each PlayerStart's
// Location above its floor, which is what shows where UT stands a body; and
// exits by route. Every map is proposed for as EXIT_OFF_NET: which group a map
// is in is the census's, not the install's.
//
// SLOW, AND LOCAL ONLY. Run it with `!` -- the session's memory guard stops a
// long run (that spec's SS 7).
//
// ONE Install PER MAP -- UTA-0011 SS 4.11.

#include "ut-paths/Seeds.h"
#include "ut-paths/Trace.h"
#include "ut-paths/Walkable.h"

#include "core/FileSystem.h"
#include "ubake/Actors.h"
#include "ubake/Bake.h"
#include "ubake/Install.h"
#include "ubake/Name.h"
#include "upkg/Level.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <variant>
#include <vector>

#ifndef UTA_UT_INSTALL_DIR
#error "UTA_UT_INSTALL_DIR must be defined for the real-asset tier"
#endif

namespace {

namespace fs = std::filesystem;
using namespace uta::paths;
using uta::ubundle::ActorClass;
using uta::ubundle::PropertyRecord;
using uta::ubundle::ValueKind;

struct Totals {
    std::size_t maps = 0, withExit = 0, spots = 0;
    std::map<std::string, std::size_t> refused;
    std::vector<std::string> refusals;
    /// By class, the floors under its actors: below FLOOR_Z, at or above it,
    /// and none within reach.
    std::map<std::string, std::array<std::size_t, 3>> floors;
    /// PlayerStart Locations above their floor, rounded to the unit.
    std::map<long, std::size_t> startHeights;
    std::array<std::size_t, 3> routes{}; ///< found, mover, none
};

bool descendsFrom(const ActorClass& actorClass, std::string_view path) {
    return actorClass.path == path
           || std::find(actorClass.ancestry.begin(), actorClass.ancestry.end(), path)
                  != actorClass.ancestry.end();
}

/// The floor under every PlayerStart and PathNode of `map`.
void floorsOf(const uta::upkg::Package& map, std::string_view mapName,
              const uta::upkg::PackageResolver& resolver, const Scene& scene, Totals& totals) {
    const auto levelExport = uta::ubake::detail::findLevel(map, mapName);
    if (!levelExport.has_value()) return;
    const auto level = uta::upkg::readLevel(map, **levelExport);
    if (!level.has_value()) return;
    const auto actors = uta::ubake::buildActors(map, mapName, *level, resolver);
    if (!actors.has_value()) return;
    for (const uta::ubundle::ActorPlacement& actor : actors->placements.actors) {
        const ActorClass& actorClass = actors->placements.classes[actor.classIndex];
        const bool start = descendsFrom(actorClass, "engine.playerstart");
        if (!start && !descendsFrom(actorClass, "engine.pathnode")) continue;
        const PropertyRecord* record = uta::ubake::detail::resolvedRecord(
            "location", actor.properties, actorClass.defaults,
            [](const PropertyRecord& candidate) { return candidate.kind == ValueKind::Vector; });
        if (record == nullptr) continue;
        const auto& value = std::get<std::array<float, 3>>(record->value);
        const Vec3 location{value[0], value[1], value[2]};

        auto& floors = totals.floors[start ? "PlayerStart" : "PathNode"];
        const Vec3 below = location - Vec3{0, 0, 1000};
        const Hit hit = trace(scene.tree, location, below);
        if (hit.fraction >= 1) {
            ++floors[2];
            continue;
        }
        ++floors[hit.normal.z >= FLOOR_Z ? 1 : 0];
        if (start) ++totals.startHeights[std::lround(1000 * hit.fraction)];
    }
}

void censusOf(const fs::path& root, const fs::path& path, Totals& totals) {
    ++totals.maps;
    const std::string mapName = uta::ubake::detail::mapNameOf(path);
    const auto refuse = [&](std::string_view reason, std::string message) {
        ++totals.refused[std::string(reason)];
        totals.refusals.push_back(path.filename().string() + ": " + std::move(message));
    };

    auto install = uta::ubake::Install::open(root);
    REQUIRE(install.has_value());
    const auto bytes = uta::fs::readFile(path);
    if (!bytes.has_value()) return refuse("does not read", std::string(bytes.error().message()));
    const auto map = uta::upkg::Package::open(*bytes);
    if (!map.has_value()) return refuse("does not open", std::string(map.error().message()));
    const auto scene = sceneOf(*map, mapName, install->resolver());
    if (!scene.has_value()) {
        const std::string message(scene.error().message());
        if (message.find("no MonsterEnd") != std::string::npos) return; // not a Monster Hunt map
        return refuse("scene refused", message);
    }
    ++totals.withExit;

    totals.spots += walkGraph(scene->tree).spots.size();
    floorsOf(*map, mapName, install->resolver(), *scene, totals);
    for (const Route route : propose(*scene, false).routes) ++totals.routes[static_cast<std::size_t>(route)];
}

} // namespace

TEST_CASE("every Monster Hunt map's scene reads and the census prints", "[real-assets][paths]") {
    const fs::path root = UTA_UT_INSTALL_DIR;
    std::vector<fs::path> maps;
    for (const fs::directory_entry& entry : fs::directory_iterator(root / "Maps"))
        if (uta::ubake::detail::fold(entry.path().extension().string()) == ".unr") maps.push_back(entry.path());
    std::sort(maps.begin(), maps.end());
    REQUIRE_FALSE(maps.empty());

    Totals totals;
    for (const fs::path& map : maps) censusOf(root, map, totals);

    std::cout << "UTA-0121 path census over " << totals.maps << " maps, " << totals.withExit
              << " holding a MonsterEnd\n"
              << "  spots found: " << totals.spots << "\n";
    for (const auto& [className, floors] : totals.floors)
        std::cout << "  " << className << " floors: normal Z below " << FLOOR_Z << ": " << floors[0]
                  << ", at or above: " << floors[1] << ", none within 1000: " << floors[2] << "\n";
    std::cout << "  PlayerStart Location above its floor, by unit:\n";
    for (const auto& [height, count] : totals.startHeights)
        std::cout << "    " << height << ": " << count << "\n";
    std::cout << "  exits by route: found " << totals.routes[0] << ", mover " << totals.routes[1]
              << ", none " << totals.routes[2] << "\n";
    for (const auto& [reason, count] : totals.refused) std::cout << "  refused, " << reason << ": " << count << "\n";
    for (const std::string& message : totals.refusals) std::cout << "    " << message << "\n";

    CHECK(totals.withExit > 0);
}
