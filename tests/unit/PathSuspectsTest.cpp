// UTA-0514: ut-paths --suspect-spots, the spots UT_MonsterHunt's undrawn-frame
// probe looks at first. The render BSP and the walk graph are built by hand.
//
// The tree: node 0 splits on X; X < 0 is solid. Node 1 splits X >= 0 on Y;
// Y >= 0 is empty with no leaf, the suspect condition. Node 2 takes Y < 0,
// empty in leaf 7.

#include "BakeFixture.h"

#include "ut-paths/Cli.h"
#include "ut-paths/Suspects.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace uta::paths;
namespace fs = std::filesystem;

namespace {

uta::upkg::BspNode node(float nx, float ny, float nz, std::int32_t front, std::int32_t back,
                        std::int32_t backLeaf, std::int32_t frontLeaf) {
    uta::upkg::BspNode n;
    n.plane.normal = uta::upkg::Vector3{nx, ny, nz};
    n.plane.w = 0;
    n.iFront = front;
    n.iBack = back;
    n.iLeaf = {backLeaf, frontLeaf};
    return n;
}

uta::upkg::Model tree() {
    uta::upkg::Model m;
    m.nodes.push_back(node(1, 0, 0, 1, -1, -1, -1));
    m.nodes.push_back(node(0, 1, 0, -1, 2, -1, -1));
    m.nodes.push_back(node(0, -1, 0, -1, -1, -1, 7));
    return m;
}

/// Spots at `centres`, joined a -> b for each pair.
WalkGraph graphOf(const std::vector<Vec3>& centres, const std::vector<std::pair<std::uint32_t, std::uint32_t>>& joins) {
    WalkGraph g;
    for (const Vec3& c : centres) g.spots.push_back(Spot{c, Vec3{0, 0, 1}, 0, 0});
    g.joins.resize(centres.size());
    for (const auto& [a, b] : joins) g.joins[a].push_back(b);
    return g;
}

} // namespace

TEST_CASE("UTA-0514: a point's render-BSP side and leaf", "[paths][suspects]") {
    const auto m = tree();
    CHECK_FALSE(bspSideAt(m, Vec3{-5, 0, 0}).empty);
    const BspSide leafless = bspSideAt(m, Vec3{5, 5, 0});
    CHECK(leafless.empty);
    CHECK(leafless.leaf == -1);
    const BspSide inLeaf = bspSideAt(m, Vec3{5, -5, 0});
    CHECK(inLeaf.empty);
    CHECK(inLeaf.leaf == 7);

    // A malformed tree reads as solid rather than hanging or reading past the end.
    uta::upkg::Model looped;
    looped.nodes.push_back(node(1, 0, 0, 0, -1, -1, -1));
    CHECK_FALSE(bspSideAt(looped, Vec3{5, 0, 0}).empty);
    uta::upkg::Model outside;
    outside.nodes.push_back(node(1, 0, 0, 9, -1, -1, -1));
    CHECK_FALSE(bspSideAt(outside, Vec3{5, 0, 0}).empty);
    const uta::upkg::Model none;
    CHECK_FALSE(bspSideAt(none, Vec3{5, 0, 0}).empty);
}

TEST_CASE("UTA-0514: suspects are leafless eyes, joined or not, one per cube, most first", "[paths][suspects]") {
    // 0 in leaf 7; 1 leafless, joined to nothing; 2 and 3 leafless in one
    // cube; 4 solid.
    const WalkGraph g = graphOf({{5, -100, 0}, {5, 100, 0}, {300, 100, 0}, {310, 110, 0}, {-100, -100, 0}},
                                {{0, 2}, {2, 3}});
    const Suspects s = suspectSpots(tree(), g, std::nullopt);
    CHECK(s.standing == 5);
    CHECK(s.leafless == 3);
    CHECK(s.clusters == 2);
    REQUIRE(s.spots.size() == 2);
    CHECK(s.spots[0].x == 300); // the cube of two, shown by its first spot
    CHECK(s.spots[1].x == 5);

    CHECK(s.reached == 0);

    CHECK(suspectSpots(tree(), g, std::nullopt, 1).spots.size() == 1);
}

TEST_CASE("UTA-0516: cubes a walk from the start reaches come first, shown by a reached spot",
          "[paths][suspects]") {
    // 0 the start, in leaf 7. Cube (1,0): 1 unjoined, then 3, which a join
    // stored 3 -> 0 reaches. Cube (0,0): 2, 4 and 5, none reached, so more
    // suspects but second.
    const WalkGraph g = graphOf({{5, -100, 0}, {310, 110, 0}, {5, 100, 0}, {300, 100, 0}, {10, 110, 0}, {20, 120, 0}},
                                {{3, 0}});
    const Suspects s = suspectSpots(tree(), g, 0);
    CHECK(s.leafless == 5);
    REQUIRE(s.spots.size() == 2);
    CHECK(s.spots[0].x == 300);
    CHECK(s.spots[0].y == 100);
    CHECK(s.spots[1].x == 5);
    CHECK(s.reached == 1);
}

TEST_CASE("UTA-0514: equal cubes keep the lower corner first", "[paths][suspects]") {
    const WalkGraph g = graphOf({{5, -100, 0}, {300, 100, 0}, {5, 100, 0}}, {});
    const Suspects s = suspectSpots(tree(), g, std::nullopt);
    REQUIRE(s.spots.size() == 2);
    CHECK(s.spots[0].x == 5);
    CHECK(s.spots[1].x == 300);
}

TEST_CASE("UTA-0514: AddSpots lines round each coordinate, after the reached count", "[paths][suspects]") {
    CHECK(addSpotsLines({{1.4, -2.6, 27.5}, {0, 0, 0}}, 1)
          == "; reached 1 of 2\nAddSpots=(X=1,Y=-3,Z=28)\nAddSpots=(X=0,Y=0,Z=0)\n");
    CHECK(addSpotsLines({}, 0) == "; reached 0 of 0\n");
}

TEST_CASE("UTA-0514: --suspect-spots refuses bad arguments and reports a missing map", "[paths][suspects]") {
    const auto run = [](const std::vector<std::string>& args, std::string* out = nullptr) {
        const std::vector<std::string_view> views(args.begin(), args.end());
        std::ostringstream o, e;
        const int code = runCli(views, o, e);
        if (out != nullptr) *out = o.str();
        return code;
    };
    const uta::test::bake::TempDir dir;
    const fs::path install = dir.path() / "install";
    fs::create_directories(install / "Maps");
    const fs::path outDir = dir.path() / "out";
    const std::vector<std::string> base = {"--install", install.string(), "--out", outDir.string(), "--suspect-spots"};

    CHECK(run(base) == 2); // no map file
    std::vector<std::string> withGroup = base;
    withGroup.insert(withGroup.end(), {"--group", "PARTITIONED", "x.unr"});
    CHECK(run(withGroup) == 2);

    std::vector<std::string> missing = base;
    missing.push_back((install / "Maps" / "MH-Gone.unr").string());
    std::string summary;
    CHECK(run(missing, &summary) == 1);
    CHECK(summary.find("{\"map\": \"MH-Gone\", \"status\": \"refused\"") != std::string::npos);
    CHECK(fs::exists(outDir / "ut-paths-spots.json"));
    CHECK_FALSE(fs::exists(outDir / "MH-Gone.spots.ini"));
}
