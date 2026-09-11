// UTA-0111's real-asset case: buildCollision over every map's level Model, and
// buildMoverCollision over every mover.
//
// docs/specs/UTA-0111-level-collision.md SS 7. It PRINTS what that spec's SS 2
// items 3 and 5, SS 4.3's refusals holding on every map, and SS 4.5's readings
// rest on, so those are an output of the suite rather than a transcription:
// trees built, and maps refused, by reason; hull entries with bit 30 set and
// clear, by which side of the entry's plane its run's box lies; links naming a
// node stored before their own; nodes no walk from node 0 reaches, and those
// of them carrying an outline or a hull; and the share of PlayerStart
// locations the level's tree classifies as outside, walked as SS 4.5 says and
// with the two children the other way round. Those two shares are what show
// which child is front.
//
// ONE Install PER MAP -- UTA-0011 SS 4.11.

#include "core/FileSystem.h"
#include "ubake/Actors.h"
#include "ubake/Collision.h"
#include "ubake/Install.h"
#include "ubake/Movers.h"
#include "ubake/Name.h"
#include "ubundle/Bundle.h"
#include "upkg/Geometry.h"
#include "upkg/Level.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
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
namespace detail = uta::ubake::detail;
using uta::ubundle::CollisionNode;
using uta::ubundle::CollisionTree;
using uta::upkg::ObjectReferenceKind;
using uta::upkg::Package;

/// NF_NotCsg and NF_IsNew -- the 432 headers' Engine/Inc/UnObj.h.
constexpr std::uint8_t NOT_CSG = 0x01;
constexpr std::uint8_t IS_NEW = 0x20;

/// SS 4.5's half unit.
constexpr double TOLERANCE = 0.5;

struct Totals {
    std::size_t maps = 0, levelTrees = 0, moverTrees = 0;
    std::map<std::string, std::size_t> skipped; ///< maps whose level Model never reached COLL
    std::map<std::string, std::size_t> refused; ///< COLL's own refusals, by reason
    std::vector<std::string> refusals;
    std::size_t levelRefused = 0, moverRefused = 0, moverRefusedWhereShapeBuilt = 0;
    // Hull entries, by flag, by where the run's box lies against the entry's plane.
    std::size_t clearBehind = 0, clearFront = 0, clearStraddle = 0;
    std::size_t setBehind = 0, setFront = 0, setStraddle = 0;
    std::size_t linksBefore = 0, unreached = 0, unreachedWithOutlineOrHull = 0;
    std::size_t starts = 0, outsideAsSpecified = 0, outsideOtherWay = 0;
};

std::string classOf(const Package& package, const uta::upkg::ExportEntry& entry) {
    if (entry.objectClass.kind() == ObjectReferenceKind::Null) return {};
    return detail::fold(std::string(package.objectName(entry.objectClass).value_or("")));
}

/// A refusal's message with its numbers taken out, so maps bucket by reason.
std::string reasonOf(std::string_view message) {
    std::string out;
    for (const char c : message)
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) out.push_back(c);
    return out;
}

void refuse(Totals& totals, std::string_view mapName, std::string_view message) {
    ++totals.refused[reasonOf(message)];
    if (totals.refusals.size() < 5) totals.refusals.push_back(std::string(mapName) + ": " + std::string(message));
}

void skip(Totals& totals, const std::string& reason) {
    ++totals.skipped[reason];
}

double sideOf(const CollisionNode& node, double x, double y, double z) {
    return static_cast<double>(node.normal[0]) * x + static_cast<double>(node.normal[1]) * y
           + static_cast<double>(node.normal[2]) * z - static_cast<double>(node.distance);
}

/// SS 4.5's walk: `outside` starts at the tree's, each node updates it by
/// FBspNode::ChildOutside with no extra flags, and a point with
/// normal . p > distance takes front, child 1. `asSpecified` false sends it
/// the other way.
bool outsideAt(const CollisionTree& tree, double x, double y, double z, bool asSpecified) {
    bool outside = tree.outside;
    std::int32_t at = tree.nodes.empty() ? -1 : 0;
    while (at != -1) { // SS 4.2 item 2: the walk ends
        const CollisionNode& node = tree.nodes[static_cast<std::size_t>(at)];
        const bool inFront = sideOf(node, x, y, z) > 0;
        const bool child = asSpecified ? inFront : !inFront;
        const bool csg = node.outlineCount > 0 && (node.nodeFlags & (IS_NEW | NOT_CSG)) == 0;
        outside = child ? (outside || csg) : (outside && !csg);
        at = child ? node.front : node.back;
    }
    return outside;
}

void tallyTree(const CollisionTree& tree, Totals& totals) {
    // Each hull entry against its run's box.
    for (const auto& hull : tree.hulls) {
        for (const auto& plane : hull.planes) {
            const CollisionNode& node = tree.nodes[plane.node];
            double lo = 1e300, hi = -1e300;
            for (int corner = 0; corner < 8; ++corner) {
                const double d = sideOf(node, (corner & 1) ? hull.max[0] : hull.min[0],
                                        (corner & 2) ? hull.max[1] : hull.min[1],
                                        (corner & 4) ? hull.max[2] : hull.min[2]);
                lo = std::min(lo, d);
                hi = std::max(hi, d);
            }
            const bool behind = hi < -TOLERANCE, front = lo > TOLERANCE;
            if (plane.flipped)
                ++(behind ? totals.setBehind : front ? totals.setFront : totals.setStraddle);
            else
                ++(behind ? totals.clearBehind : front ? totals.clearFront : totals.clearStraddle);
        }
    }

    // Links naming a node stored before their own.
    for (std::size_t n = 0; n < tree.nodes.size(); ++n)
        for (const std::int32_t link : {tree.nodes[n].back, tree.nodes[n].front, tree.nodes[n].coplanar})
            totals.linksBefore += link != -1 && static_cast<std::size_t>(link) <= n;

    // Nodes no walk from node 0 reaches.
    if (tree.nodes.empty()) return;
    std::vector<bool> seen(tree.nodes.size(), false);
    std::vector<std::size_t> stack{0};
    while (!stack.empty()) {
        const std::size_t n = stack.back();
        stack.pop_back();
        seen[n] = true;
        for (const std::int32_t link : {tree.nodes[n].back, tree.nodes[n].front, tree.nodes[n].coplanar})
            if (link != -1) stack.push_back(static_cast<std::size_t>(link));
    }
    for (std::size_t n = 0; n < tree.nodes.size(); ++n) {
        if (seen[n]) continue;
        ++totals.unreached;
        totals.unreachedWithOutlineOrHull += tree.nodes[n].outlineCount > 0 || tree.nodes[n].hull != -1;
    }
}

void censusOf(const fs::path& root, const fs::path& mapPath, Totals& totals) {
    ++totals.maps;
    const std::string mapName = detail::mapNameOf(mapPath);
    const auto bytes = uta::fs::readFile(mapPath);
    if (!bytes.has_value()) return skip(totals, "the map does not read");
    const auto map = Package::open(*bytes);
    if (!map.has_value()) return skip(totals, "the map does not open");
    const auto exports = map->exports();

    const uta::upkg::ExportEntry* levelExport = nullptr;
    for (const auto& entry : exports)
        if (classOf(*map, entry) == "level") levelExport = &entry;
    if (levelExport == nullptr) return skip(totals, "no Level export");
    const auto level = uta::upkg::readLevel(*map, *levelExport);
    if (!level.has_value() || level->model.kind() != ObjectReferenceKind::Export
        || level->model.index() >= exports.size())
        return skip(totals, "no level Model");
    const auto levelModel = uta::upkg::readModel(*map, exports[level->model.index()]);
    if (!levelModel.has_value()) return skip(totals, "the level's Model does not read");

    const auto tree = uta::ubake::buildCollision(*levelModel);
    if (!tree.has_value()) {
        ++totals.levelRefused;
        return refuse(totals, mapName, tree.error().message());
    }
    ++totals.levelTrees;
    tallyTree(*tree, totals);

    // Every PlayerStart's Location, walked both ways.
    for (const auto& entry : exports) {
        if (classOf(*map, entry) != "playerstart") continue;
        const auto properties = uta::upkg::readProperties(*map, entry);
        if (!properties.has_value()) continue;
        for (const uta::upkg::Property& property : *properties) {
            if (detail::fold(std::string(map->name(property.nameIndex).value_or(""))) != "location") continue;
            const auto* location = std::get_if<uta::upkg::Vector3>(&property.value);
            if (location == nullptr) continue;
            ++totals.starts;
            totals.outsideAsSpecified += outsideAt(*tree, location->x, location->y, location->z, true);
            totals.outsideOtherWay += outsideAt(*tree, location->x, location->y, location->z, false);
        }
    }

    // Every mover.
    auto install = uta::ubake::Install::open(root);
    REQUIRE(install.has_value());
    const auto actors = uta::ubake::buildActors(*map, mapName, *level, install->resolver());
    if (!actors.has_value()) return;
    const auto movers = uta::ubake::findMovers(*map, actors->placements);
    if (!movers.has_value()) return;
    const uta::ubake::MaterialLookup standIn = [](uta::upkg::ObjectReference,
                                                  bool) -> const uta::ubake::SurfaceMaterial* {
        static const uta::ubake::SurfaceMaterial made{"stand-in", 64.0, 64.0};
        return &made;
    };
    for (const uta::ubake::MoverSite& site : *movers) {
        const auto model = uta::upkg::readModel(*map, *site.model);
        if (!model.has_value()) continue;
        const auto built = uta::ubake::buildMoverCollision(site, *model, actors->placements);
        if (built.has_value()) {
            ++totals.moverTrees;
            tallyTree(built->tree, totals);
            continue;
        }
        ++totals.moverRefused;
        refuse(totals, mapName, built.error().message());
        if (uta::ubake::buildMover(site, *model, actors->placements, standIn).has_value())
            ++totals.moverRefusedWhereShapeBuilt;
    }
}

} // namespace

TEST_CASE("every map's collision bakes and the census prints", "[real-assets][collision]") {
    const fs::path root{UTA_UT_INSTALL_DIR};
    std::vector<fs::path> maps;
    for (const fs::directory_entry& entry : fs::directory_iterator(root / "Maps"))
        if (entry.is_regular_file() && detail::fold(entry.path().extension().string()) == ".unr")
            maps.push_back(entry.path());
    std::sort(maps.begin(), maps.end());
    REQUIRE_FALSE(maps.empty());

    Totals totals;
    for (const fs::path& map : maps) censusOf(root, map, totals);

    const auto pct = [](std::size_t n, std::size_t of) { return of ? 100.0 * n / of : 0.0; };
    std::cout << "UTA-0111 collision census over " << totals.maps << " maps\n"
              << "  trees built: level " << totals.levelTrees << ", mover " << totals.moverTrees << "\n"
              << "  refused by COLL: level " << totals.levelRefused << ", mover " << totals.moverRefused
              << " (of which a shape built " << totals.moverRefusedWhereShapeBuilt << ")\n"
              << "  hull entries, bit 30 clear: box behind " << totals.clearBehind << ", in front "
              << totals.clearFront << ", straddling " << totals.clearStraddle << "\n"
              << "  hull entries, bit 30 set:   box behind " << totals.setBehind << ", in front "
              << totals.setFront << ", straddling " << totals.setStraddle << "\n"
              << "  links naming a node stored before their own " << totals.linksBefore << "\n"
              << "  nodes no walk from node 0 reaches " << totals.unreached << ", carrying an outline or a hull "
              << totals.unreachedWithOutlineOrHull << "\n"
              << "  PlayerStarts " << totals.starts << ": outside walked as SS 4.5 says "
              << pct(totals.outsideAsSpecified, totals.starts) << "%, the other way round "
              << pct(totals.outsideOtherWay, totals.starts) << "%\n";
    for (const auto& [reason, count] : totals.skipped) std::cout << "  skipped, " << reason << ": " << count << "\n";
    for (const auto& [reason, count] : totals.refused) std::cout << "  refused, " << reason << ": " << count << "\n";
    for (const std::string& message : totals.refusals) std::cout << "    " << message << "\n";

    CHECK(totals.levelTrees > 0);
    CHECK(totals.moverTrees > 0);
    // SS 4.3: every refusal held on every Model. A mover whose shape does
    // not build is UTA-0119's to refuse, so only one whose shape does counts.
    CHECK(totals.levelRefused == 0);
    CHECK(totals.moverRefusedWhereShapeBuilt == 0);
    // SS 4.5: no box wholly on the wrong side of its entry's plane.
    CHECK(totals.clearFront == 0);
    CHECK(totals.setBehind == 0);
    // SS 4.5: which child a point in front takes.
    REQUIRE(totals.starts > 0);
    CHECK(pct(totals.outsideAsSpecified, totals.starts) > 99.0);
    CHECK(pct(totals.outsideOtherWay, totals.starts) < 1.0);
}
