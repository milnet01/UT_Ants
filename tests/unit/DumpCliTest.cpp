// UTA-0136: ut-dump's --nav-graph carries each reach spec's flags and collision
// size, so a consumer can decide walkability from the JSON alone.
//
// tools/ut-dump/Cli.cpp is compiled into this binary, so every case drives the
// command line as a function over files written to a temporary install.
//
// The roadmap item's test is the first case: a walking-bot filter applied to
// the JSON reproduces ut-paths' Scene::edges for the same map. The filter here
// is written from the rows alone, as the consumer's would be, and INV-11's
// seven specs of docs/specs/UTA-0121-bot-path-seeds.md are what it sorts.
//
// NO TEST NAME CONTAINS A COMMA, AND NONE STARTS WITH A DASH -- BakeCliTest.cpp
// says why.

#include "BakeFixture.h"

#include "support/UnrealPackageBuilder.h"
#include "upkg/Package.h"
#include "ut-dump/Cli.h"
#include "ut-paths/Seeds.h"
#include "ut-paths/Walkable.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace uta::test::bake;

namespace {

struct Run {
    int code = -1;
    std::string out;
    std::string err;
};

Run run(const std::vector<std::string>& args) {
    const std::vector<std::string_view> views(args.begin(), args.end());
    std::ostringstream out;
    std::ostringstream err;
    Run result;
    result.code = uta::dump::runCli(views, out, err);
    result.out = out.str();
    result.err = err.str();
    return result;
}

/// INV-11's packages, as PathSeedsTest's seedPackages builds them, by file
/// stem: Engine's navigation classes and MonsterHunt's MonsterEnd, with the
/// collision sizes the install's own classes carry. A second copy rather than
/// a shared one -- two callers, short of coding.md's Rule of Three.
std::vector<std::pair<std::string, std::vector<std::uint8_t>>> installPackages() {
    Packer engine;
    const std::int32_t navigation = engine.addClass(
        "NavigationPoint", 0,
        {floatProperty("CollisionRadius", 46), floatProperty("CollisionHeight", 50)});
    engine.addClass("PlayerStart", navigation,
                    {floatProperty("CollisionRadius", 18), floatProperty("CollisionHeight", 40)});
    engine.addClass("PathNode", navigation);
    Packer monsterHunt;
    monsterHunt.addClass("MonsterEnd", 0,
                         {floatProperty("CollisionRadius", 40), floatProperty("CollisionHeight", 40)});
    return {{"Core", tinyPackage("Object")}, {"Engine", engine.build()},
            {"MonsterHunt", monsterHunt.build()}};
}

/// INV-11's map: a PlayerStart, a MonsterEnd, and four PathNodes (actors 2 to
/// 5) joined by seven specs, each on its own ordered pair.
MapBuilder inv11Map() {
    MapBuilder map;
    map.addActorOfClass("Engine", "PlayerStart", {vectorProperty("Location", -200, 0, 0)})
        .addActorOfClass("MonsterHunt", "MonsterEnd", {vectorProperty("Location", -400, 0, 0)});
    for (int i = 0; i < 4; ++i)
        map.addActorOfClass("Engine", "PathNode", {vectorProperty("Location", 100.0F * i, 0, 0)});
    constexpr std::size_t A = 2, B = 3, C = 4, D = 5;
    map.addReachSpec(A, B, 17, 39, 1)      // walking
        .addReachSpec(B, C, 17, 39, 9)     // walking and jumping
        .addReachSpec(C, D, 17, 39, 32)    // special
        .addReachSpec(D, A, 17, 39, 85)    // walking, swimming, doors and player-only
        .addReachSpec(A, C, 17, 39, 2)     // flying
        .addReachSpec(B, D, 16, 39, 1)     // walking, narrower than the body
        .addReachSpec(C, A, 17, 38, 1);    // walking, lower than the body
    return map;
}

struct NodeRow {
    std::string name, className;
};

struct EdgeRow {
    std::size_t from = 0, to = 0;
    std::int32_t radius = 0, height = 0, flags = 0;
};

std::vector<NodeRow> nodeRows(const std::string& json) {
    // A delimiter, because `)"` inside a capture would end a plain raw string.
    static const std::regex row(R"re(\{"export": \d+, "name": "([^"]*)", "class": "([^"]*)"\})re");
    std::vector<NodeRow> rows;
    for (auto it = std::sregex_iterator(json.begin(), json.end(), row); it != std::sregex_iterator(); ++it)
        rows.push_back({(*it)[1].str(), (*it)[2].str()});
    return rows;
}

std::vector<EdgeRow> edgeRows(const std::string& json) {
    static const std::regex row(
        R"re(\{"from": (\d+), "to": (\d+), "distance": -?\d+, "collisionRadius": (-?\d+), )re"
        R"re("collisionHeight": (-?\d+), "reachFlags": (-?\d+), "pruned": \d+\})re");
    std::vector<EdgeRow> rows;
    for (auto it = std::sregex_iterator(json.begin(), json.end(), row); it != std::sregex_iterator(); ++it)
        rows.push_back({std::stoul((*it)[1].str()), std::stoul((*it)[2].str()), std::stoi((*it)[3].str()),
                        std::stoi((*it)[4].str()), std::stoi((*it)[5].str())});
    return rows;
}

/// A walking bot's rule, from the row alone: the body fits, and every flag is
/// one APawn::calcMoveFlags gives a walker -- all but R_FLY, 2.
bool walkerMayUse(const EdgeRow& edge) {
    constexpr std::int32_t WALKER = 1 | 4 | 8 | 16 | 32 | 64;
    return edge.radius >= uta::paths::RADIUS && edge.height >= uta::paths::HALF_HEIGHT
           && (edge.flags & WALKER) == edge.flags;
}

/// The map and its install on disk; the map's path.
fs::path writeMap(const TempDir& dir, const MapBuilder& map) {
    for (const auto& [stem, bytes] : installPackages())
        writeFile(dir.path() / "System" / (stem + ".u"), bytes);
    const fs::path path = dir.path() / "Maps" / "MH-Fixture.unr";
    writeFile(path, map.build());
    return path;
}

} // namespace

TEST_CASE("UTA-0136: a walking filter over --nav-graph reproduces the scene's edges", "[dump]") {
    const MapBuilder map = inv11Map();
    const TempDir dir;
    const fs::path mapPath = writeMap(dir, map);

    const Run result = run({"--system", (dir.path() / "System").string(), "--nav-graph", mapPath.string()});
    INFO(result.err);
    REQUIRE(result.code == 0);

    const std::vector<NodeRow> nodes = nodeRows(result.out);
    const std::vector<EdgeRow> edges = edgeRows(result.out);
    // Every spec, the unusable ones too: the filter is the consumer's to apply.
    REQUIRE(edges.size() == 7);

    // The same map through ut-paths, over the same packages.
    MemoryPackages packages;
    for (const auto& [stem, bytes] : installPackages()) {
        std::string folded = stem;
        for (char& ch : folded) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        packages.add(folded, bytes);
    }
    const std::vector<std::uint8_t> bytes = map.build();
    const auto package = uta::upkg::Package::open(uta::test::asBytes(bytes));
    REQUIRE(package.has_value());
    const auto scene = uta::paths::sceneOf(*package, MAP_NAME, packages.resolver());
    INFO((scene.has_value() ? std::string() : std::string(scene.error().message())));
    REQUIRE(scene.has_value());
    // Every navigation point is placed, so a node's position in the JSON is its
    // position in the scene's network.
    REQUIRE(scene->network.size() == nodes.size());

    std::set<std::pair<std::size_t, std::size_t>> filtered;
    for (const EdgeRow& edge : edges)
        if (walkerMayUse(edge)) filtered.emplace(edge.from, edge.to);
    const std::set<std::pair<std::size_t, std::size_t>> kept(scene->edges.begin(), scene->edges.end());
    CHECK(filtered.size() == 4);
    CHECK(filtered == kept);
}

TEST_CASE("UTA-0136: an edge names its nodes by actor name and class", "[dump]") {
    const TempDir dir;
    const fs::path mapPath = writeMap(dir, inv11Map());
    const Run result = run({"--system", (dir.path() / "System").string(), "--nav-graph", mapPath.string()});
    REQUIRE(result.code == 0);

    const std::vector<NodeRow> nodes = nodeRows(result.out);
    const std::vector<EdgeRow> edges = edgeRows(result.out);
    // The flying spec, the only one carrying flag 2, runs PathNode2 to PathNode4.
    std::size_t flying = 0;
    for (const EdgeRow& edge : edges) {
        if (edge.flags != 2) continue;
        ++flying;
        REQUIRE(edge.from < nodes.size());
        REQUIRE(edge.to < nodes.size());
        CHECK(nodes[edge.from].name == "PathNode2");
        CHECK(nodes[edge.from].className == "PathNode");
        CHECK(nodes[edge.to].name == "PathNode4");
    }
    CHECK(flying == 1);
}

TEST_CASE("UTA-0136: without --nav-graph the nav object carries counts only", "[dump]") {
    const TempDir dir;
    const fs::path mapPath = writeMap(dir, inv11Map());
    const Run result = run({"--system", (dir.path() / "System").string(), mapPath.string()});
    REQUIRE(result.code == 0);
    CHECK(result.out.find("\"nav\": {\"nodes\": ") != std::string::npos);
    CHECK(result.out.find("nodeList") == std::string::npos);
    CHECK(result.out.find("edgeList") == std::string::npos);
}
