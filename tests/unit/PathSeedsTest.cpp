// Locks INV-5 to INV-10 of docs/specs/UTA-0121-bot-path-seeds.md: routes and
// the nodes proposed along them, the file, the command line, and the scene.
//
// INV-5 to INV-8 build their scenes in memory, with tests/unit/PathFixture.h
// (that spec's SS 7). INV-9 drives the command line over a census written to a
// temporary directory; INV-10 reads a map built with tests/unit/BakeFixture.h.
//
// NO TEST NAME CONTAINS A COMMA, AND NONE STARTS WITH A DASH -- BakeCliTest's
// reason.

#include "BakeFixture.h"
#include "PathFixture.h"

#include "ut-paths/Cli.h"
#include "ut-paths/Seeds.h"
#include "ut-paths/Trace.h"
#include "ut-paths/Walkable.h"

#include "core/FileSystem.h"
#include "support/UnrealPackageBuilder.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace uta::paths;
using uta::test::paths::box;
using uta::test::paths::Region;
using uta::test::paths::worldOf;

namespace {

/// SS 4.5's three heights from a centre.
constexpr std::array<double, 3> HEIGHTS = {-HALF_HEIGHT + STEP + 1, 0, HALF_HEIGHT - 1};

/// At most 350 long, and its three segments trace clear -- what INV-5 asks of
/// every hop. The floor and the mover checks are INV-5's pit case's and
/// INV-6's to show.
bool hopClear(const uta::ubundle::CollisionTree& tree, const Vec3& from, const Vec3& to) {
    if (length(to - from) > 350) return false;
    for (const double height : HEIGHTS) {
        const Vec3 lift{0, 0, height};
        if (trace(tree, from + lift, to + lift).fraction < 1) return false;
    }
    return true;
}

/// The centres of the spots `actors` are placed at.
std::vector<Vec3> placedAt(const WalkGraph& graph, const std::vector<Vec3>& actors) {
    std::vector<Vec3> spots;
    for (const Vec3& actor : actors)
        if (const auto spot = place(graph, actor)) spots.push_back(graph.spots[*spot].centre);
    return spots;
}

/// The first node an allowed hop from one of `from`, and each next node one
/// from the node before it.
void checkChain(const Scene& scene, const std::vector<Vec3>& from, const std::vector<Vec3>& nodes) {
    REQUIRE_FALSE(nodes.empty());
    CHECK(std::any_of(from.begin(), from.end(),
                      [&](const Vec3& f) { return hopClear(scene.tree, f, nodes.front()); }));
    for (std::size_t i = 1; i < nodes.size(); ++i) {
        INFO("the hop to node " << i << ", at x " << nodes[i].x << " y " << nodes[i].y);
        CHECK(hopClear(scene.tree, nodes[i - 1], nodes[i]));
    }
}

bool touches(const Vec3& spot, const Cylinder& exit) {
    return horizontal(spot, exit.centre) <= exit.radius + RADIUS
           && std::abs(spot.z - exit.centre.z) <= exit.height + HALF_HEIGHT;
}

enum class Across { Nothing, Pit, Wall, Mover };

/// INV-5's L-shaped corridor, each leg 1500 long and 256 wide and high: the
/// start and its network at one end, a MonsterEnd at the other with no
/// navigation point near it.
///
/// With a pit, the first leg is one spot wide but for a room round the pit's
/// middle, and the strip the walk follows is that room's far side: the way
/// round is long and the way straight across short, so a hop taken without
/// its floor check crosses the pit's middle rather than grazing its edge.
Scene corridor(Across across) {
    std::vector<Region> regions;
    if (across == Across::Pit) {
        regions.push_back(box({0, 0, 0}, {1500, 64, 256}));
        // The room leaves a column either side of the pit for a body to stand
        // in: X 576 and X 800, each more than a radius from the room's walls.
        regions.push_back(box({544, 0, 0}, {832, 300, 256}));
        regions.push_back(box({600, 0, -400}, {784, 236, 0}));
    } else if (across == Across::Wall) {
        regions.push_back(box({0, 0, 0}, {700, 256, 256}));
        regions.push_back(box({732, 0, 0}, {1500, 256, 256}));
    } else {
        regions.push_back(box({0, 0, 0}, {1500, 256, 256}));
    }
    regions.push_back(box({1244, 0, 0}, {1500, 1500, 256}));

    Scene scene;
    scene.tree = worldOf(regions, {0, 0, -450}, {1500, 1500, 300});
    const double y = across == Across::Pit ? 32 : 128;
    scene.network = {{64, y, 50}, {160, y, 50}};
    scene.edges = {{0, 1}, {1, 0}};
    scene.start = {100, y, 40};
    scene.exits = {Cylinder{{1372, 1400, 40}, 40, 40}};
    if (across == Across::Mover) scene.movers = {Box{{700, -10, -10}, {732, 266, 266}}};
    return scene;
}

/// Whether the segment passes over the pit's floorless middle: the pit less
/// 48 on its open sides, where no floor is within a hop's reach -- 32 to one of
/// its samples, and 16 more between two.
bool overPit(const Vec3& a, const Vec3& b) {
    const int steps = static_cast<int>(std::ceil(length(b - a) / 8)) + 1;
    for (int k = 0; k <= steps; ++k) {
        const Vec3 p = a + (b - a) * (static_cast<double>(k) / steps);
        if (p.x > 648 && p.x < 736 && p.y < 188) return true;
    }
    return false;
}

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
    result.code = runCli(views, out, err);
    result.out = out.str();
    result.err = err.str();
    return result;
}

std::vector<std::uint8_t> bytesOf(std::string_view text) {
    return {text.begin(), text.end()};
}

/// INV-10 and INV-11's packages: Engine's navigation classes and MonsterHunt's
/// MonsterEnd, with the collision sizes the install's own classes carry.
uta::test::bake::MemoryPackages seedPackages() {
    using namespace uta::test::bake;
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

    MemoryPackages packages;
    packages.add("core", tinyPackage("Object"));
    packages.add("engine", engine.build());
    packages.add("monsterhunt", monsterHunt.build());
    return packages;
}

/// sceneOf over the map `map` builds, with seedPackages() as the install.
uta::Result<Scene> sceneOfBuilt(const uta::test::bake::MapBuilder& map) {
    uta::test::bake::MemoryPackages packages = seedPackages();
    const std::vector<std::uint8_t> bytes = map.build();
    const auto package = uta::upkg::Package::open(uta::test::asBytes(bytes));
    REQUIRE(package.has_value());
    return sceneOf(*package, uta::test::bake::MAP_NAME, packages.resolver());
}

} // namespace

TEST_CASE("INV-5: a route found is a chain of allowed hops each node H above the floor",
          "[paths][seeds]") {
    const Scene scene = corridor(Across::Nothing);
    const Proposal proposal = propose(scene, false);
    REQUIRE(proposal.routes == std::vector<Route>{Route::Found});

    const WalkGraph graph = walkGraph(scene.tree);
    checkChain(scene, placedAt(graph, {scene.network[0], scene.network[1], scene.start}),
               proposal.nodes);
    for (const Vec3& node : proposal.nodes)
        CHECK(static_cast<float>(node.z) == static_cast<float>(HALF_HEIGHT));
    CHECK(touches(proposal.nodes.back(), scene.exits[0]));
}

TEST_CASE("INV-5: no hop crosses a pit the walk goes round", "[paths][seeds]") {
    const Scene scene = corridor(Across::Pit);
    const Proposal proposal = propose(scene, false);
    REQUIRE(proposal.routes == std::vector<Route>{Route::Found});

    const WalkGraph graph = walkGraph(scene.tree);
    checkChain(scene, placedAt(graph, {scene.network[0], scene.network[1], scene.start}),
               proposal.nodes);
    CHECK(touches(proposal.nodes.back(), scene.exits[0]));
    // The first hop starts within 350 of X 160, so cannot reach the pit.
    for (std::size_t i = 1; i < proposal.nodes.size(); ++i) {
        INFO("the hop to node " << i << ", at x " << proposal.nodes[i].x << " y "
                                << proposal.nodes[i].y);
        CHECK_FALSE(overPit(proposal.nodes[i - 1], proposal.nodes[i]));
    }
}

TEST_CASE("INV-6: a route through a wall is none and one only through a mover is mover",
          "[paths][seeds]") {
    const Proposal walled = propose(corridor(Across::Wall), false);
    CHECK(walled.routes == std::vector<Route>{Route::None});
    CHECK(walled.nodes.empty());

    const Scene scene = corridor(Across::Mover);
    const Proposal moved = propose(scene, false);
    CHECK(moved.routes == std::vector<Route>{Route::Mover});
    CHECK(moved.nodes.empty());
    CHECK(toJson("MH-Test", "md5", "EXIT_OFF_NET", scene, moved).find("\"moverOnly\": true")
          != std::string::npos);
}

TEST_CASE("INV-7: a partitioned chain bridges to the part reaching the exit and stops",
          "[paths][seeds]") {
    // A corridor one spot wide, and beside it behind a wall 1 thick an alcove.
    Scene scene;
    scene.tree = worldOf({box({0, 0, 0}, {2600, 64, 256}), box({800, 65, 0}, {1000, 200, 256})},
                         {0, 0, -50}, {2600, 200, 300});
    // 0 and 1 are the start's part. 2 and 3 are the exit's: 3 beside the exit,
    // far enough past 2 that a chain run on to the exit would need nodes past
    // 2. So is 4, in the alcove, within 50 of the corridor's spot at X 896. One
    // edge joins the two parts, from the exit's to the start's.
    scene.network = {{100, 32, 50}, {256, 32, 50}, {1504, 32, 50}, {2280, 32, 50}, {900, 75, 50}};
    scene.edges = {{0, 1}, {1, 0}, {2, 3}, {3, 2}, {2, 4}, {2, 1}};
    scene.start = {100, 32, 40};
    scene.exits = {Cylinder{{2300, 32, 40}, 40, 40}};

    const Proposal proposal = propose(scene, true);
    REQUIRE(proposal.routes == std::vector<Route>{Route::Found});

    // Across the gap from the start's part, and every hop allowed: the walled-off
    // point taking the spot's place would leave a hop past it longer than 350.
    const WalkGraph graph = walkGraph(scene.tree);
    checkChain(scene, placedAt(graph, {scene.network[0], scene.network[1], scene.start}),
               proposal.nodes);
    // None beyond the spot placed for the exit's part.
    const auto bridge = place(graph, scene.network[2]);
    REQUIRE(bridge.has_value());
    for (const Vec3& node : proposal.nodes) CHECK(node.x < graph.spots[*bridge].centre.x);
}

TEST_CASE("INV-8: the file holds SS 4.3's fields escaped and each number a float",
          "[paths][seeds]") {
    Scene scene;
    scene.exits = {Cylinder{{1024, -512, 96}, 40, 40}};
    const Proposal proposal{{Route::Found}, {{1.0 / 3.0, -300, 39}}};

    const std::string expected = "{\n"
                                 "  \"schema\": 1,\n"
                                 "  \"map\": \"MH-\\\"Bob's\\\\Map\\\"\",\n"
                                 "  \"md5\": \"0123456789abcdef0123456789abcdef\",\n"
                                 "  \"group\": \"EXIT_OFF_NET\",\n"
                                 "  \"moverOnly\": false,\n"
                                 "  \"exits\": [\n"
                                 "    {\"x\": 1024, \"y\": -512, \"z\": 96, \"route\": \"found\"}\n"
                                 "  ],\n"
                                 "  \"nodes\": [\n"
                                 "    {\"x\": 0.33333334, \"y\": -300, \"z\": 39}\n"
                                 "  ]\n"
                                 "}\n";
    CHECK(toJson("MH-\"Bob's\\Map\"", "0123456789abcdef0123456789abcdef", "EXIT_OFF_NET", scene,
                 proposal)
          == expected);
}

TEST_CASE("INV-9: only EXIT_OFF_NET and PARTITIONED rows are work and a missing file is skipped",
          "[paths][seeds]") {
    const uta::test::bake::TempDir dir;
    const fs::path install = dir.path() / "install";
    fs::create_directories(install / "Maps");
    const fs::path census = dir.path() / "census.tsv";
    uta::test::bake::writeFile(census, bytesOf("map\tfiles\tgroup\n"
                                               "MH-Off\t1\tEXIT_OFF_NET\n"
                                               "MH-Part\t1\tPARTITIONED\n"
                                               "MH-Built\t1\tNO_PATHS_BUILT\n"));
    const fs::path out = dir.path() / "out";
    const std::vector<std::string> base = {"--install", install.string(), "--census",
                                           census.string(), "--out", out.string()};

    const std::string skipped =
        "{\"schema\": 1, \"maps\": [\n"
        "  {\"map\": \"MH-Off\", \"status\": \"skipped\", \"why\": \"no file in Maps/\"},\n"
        "  {\"map\": \"MH-Part\", \"status\": \"skipped\", \"why\": \"no file in Maps/\"}\n"
        "]}\n";
    const Run all = run(base);
    INFO(all.err);
    CHECK(all.code == 0);
    CHECK(all.out == skipped);
    const auto summary = uta::fs::readFile(out / "ut-paths-summary.json");
    REQUIRE(summary.has_value());
    CHECK(std::string(reinterpret_cast<const char*>(summary->data()), summary->size()) == skipped);

    std::vector<std::string> outside = base;
    outside.emplace_back("MH-Built");
    const Run named = run(outside);
    CHECK(named.code == 1);
    CHECK(named.out.find("{\"map\": \"MH-Built\", \"status\": \"refused\"") != std::string::npos);
}

TEST_CASE("INV-10: the start is the first PlayerStart and the exits every MonsterEnd",
          "[paths][seeds]") {
    using namespace uta::test::bake;
    MapBuilder map;
    map.addActorOfClass("Engine", "PlayerStart", {vectorProperty("Location", 1, 2, 3)})
        .addActorOfClass("Engine", "PlayerStart", {vectorProperty("Location", 4, 5, 6)})
        .addActorOfClass("MonsterHunt", "MonsterEnd",
                         {vectorProperty("Location", 10, 20, 30), floatProperty("CollisionRadius", 77)});
    map.addActor("MyEnd0", map.addClass("MyEnd", map.importClass("MonsterHunt", "MonsterEnd")),
                 {vectorProperty("Location", 40, 50, 60)});

    const auto scene = sceneOfBuilt(map);
    INFO((scene.has_value() ? std::string() : std::string(scene.error().message())));
    REQUIRE(scene.has_value());
    CHECK(scene->start == Vec3{1, 2, 3});
    REQUIRE(scene->exits.size() == 2);
    CHECK(scene->exits[0].centre == Vec3{10, 20, 30});
    CHECK(scene->exits[0].radius == 77);
    CHECK(scene->exits[0].height == 40);
    CHECK(scene->exits[1].centre == Vec3{40, 50, 60});
    CHECK(scene->exits[1].radius == 40);
    CHECK(scene->exits[1].height == 40);
}

TEST_CASE("INV-11: the scene keeps only the edges a walking bot may use", "[paths][seeds]") {
    using namespace uta::test::bake;
    MapBuilder map;
    map.addActorOfClass("Engine", "PlayerStart", {vectorProperty("Location", -200, 0, 0)})
        .addActorOfClass("MonsterHunt", "MonsterEnd", {vectorProperty("Location", -400, 0, 0)});
    // Four PathNodes, actors 2 to 5, a hundred apart on X.
    for (int i = 0; i < 4; ++i)
        map.addActorOfClass("Engine", "PathNode", {vectorProperty("Location", 100.0F * i, 0, 0)});
    // Seven specs, each on its own ordered pair. A kept spec is exactly the
    // body's size, so a comparison stricter than supports()'s refuses it.
    constexpr std::size_t A = 2, B = 3, C = 4, D = 5;
    map.addReachSpec(A, B, 17, 39, 1)      // walking
        .addReachSpec(B, C, 17, 39, 9)     // walking and jumping
        .addReachSpec(C, D, 17, 39, 32)    // special
        .addReachSpec(D, A, 17, 39, 85)    // walking, swimming, doors and player-only
        .addReachSpec(A, C, 17, 39, 2)     // flying
        .addReachSpec(B, D, 16, 39, 1)     // walking, narrower than the body
        .addReachSpec(C, A, 17, 38, 1);    // walking, lower than the body

    const auto scene = sceneOfBuilt(map);
    INFO((scene.has_value() ? std::string() : std::string(scene.error().message())));
    REQUIRE(scene.has_value());
    // The network position of the navigation point at X = x.
    const auto at = [&](double x) {
        for (std::size_t i = 0; i < scene->network.size(); ++i)
            if (scene->network[i] == Vec3{x, 0, 0}) return i;
        FAIL("no navigation point at x " << x);
        return std::size_t{0};
    };
    const std::set<std::pair<std::size_t, std::size_t>> kept(scene->edges.begin(), scene->edges.end());
    const std::set<std::pair<std::size_t, std::size_t>> expected = {
        {at(0), at(100)}, {at(100), at(200)}, {at(200), at(300)}, {at(300), at(0)}};
    CHECK(kept == expected);
}
