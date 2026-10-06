// UTA-0328: ut-bound-scan reports a BSP node whose render bound misses the
// drawn geometry under it -- the cause UTA-0283 proved on TheBoat's node 1305,
// where the original game skips the node's whole subtree and the frame keeps
// stale pixels.
//
// tools/ut-bound-scan/Scan.cpp and Cli.cpp are compiled into this binary. The
// scan reads a Model and nothing else, so each case builds one in memory: a
// root whose subtree holds one square, and a render bound on the root.
//
// NO TEST NAME CONTAINS A COMMA, AND NONE STARTS WITH A DASH -- BakeCliTest.cpp
// says why.

#include "ut-bound-scan/Cli.h"
#include "ut-bound-scan/Scan.h"

#include "upkg/Geometry.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using uta::boundscan::Kind;
using uta::boundscan::scanModel;

namespace {

/// A node linking nowhere, drawing nothing, with no render bound.
uta::upkg::BspNode bareNode() {
    uta::upkg::BspNode node;
    node.iSurf = -1;
    node.iFront = node.iBack = node.iPlane = -1;
    node.iRenderBound = -1;
    return node;
}

/// Node 0 is the root, bound by box 0; node 1 draws a square from x = 100 to
/// x = 200 at y and z 0..10, reached from the root through `link`.
enum class Link { Front, Back, Coplanar };

uta::upkg::Model rootOverSquare(Link link, uta::upkg::Box rootBox,
                                std::uint32_t polyFlags = 0) {
    uta::upkg::Model model;
    model.points = {{100, 0, 0}, {200, 0, 0}, {200, 10, 10}, {100, 10, 10}};
    model.verts = {{0, 0}, {1, 0}, {2, 0}, {3, 0}};
    model.surfs.resize(1);
    model.surfs[0].polyFlags = polyFlags;
    model.bounds = {rootBox};

    uta::upkg::BspNode root = bareNode();
    root.iRenderBound = 0;
    (link == Link::Front ? root.iFront : link == Link::Back ? root.iBack : root.iPlane) = 1;
    uta::upkg::BspNode square = bareNode();
    square.iSurf = 0;
    square.iVertPool = 0;
    square.numVertices = 4;
    model.nodes = {root, square};
    return model;
}

/// A box from x = 0 to x = `maxX`, holding the square on y and z.
uta::upkg::Box boxTo(float maxX, bool valid = true) {
    return {.min = {0, 0, 0}, .max = {maxX, 10, 10}, .valid = valid};
}

} // namespace

TEST_CASE("A bound short of its front subtree is a miss by the overshoot", "[ut-bound-scan]") {
    const auto scan = scanModel(rootOverSquare(Link::Front, boxTo(50)), 2);
    REQUIRE(scan.findings.size() == 1);
    CHECK(scan.findings[0].kind == Kind::Miss);
    CHECK(scan.findings[0].node == 0);
    CHECK(scan.findings[0].bound == 0);
    CHECK(scan.findings[0].excess == 150);
    CHECK(scan.checked == 1);
    CHECK(scan.misses == 1);
    CHECK(scan.worst == 150);
}

TEST_CASE("The back subtree and the coplanars are guarded too", "[ut-bound-scan]") {
    for (const Link link : {Link::Back, Link::Coplanar}) {
        const auto scan = scanModel(rootOverSquare(link, boxTo(50)), 2);
        REQUIRE(scan.findings.size() == 1);
        CHECK(scan.findings[0].excess == 150);
    }
}

TEST_CASE("A bound holding its subtree is not reported", "[ut-bound-scan]") {
    const auto scan = scanModel(rootOverSquare(Link::Front, boxTo(200)), 2);
    CHECK(scan.findings.empty());
    CHECK(scan.checked == 1);
    CHECK(scan.worst == 0);
}

TEST_CASE("A miss within the tolerance is not reported", "[ut-bound-scan]") {
    const auto model = rootOverSquare(Link::Front, boxTo(50));
    CHECK(scanModel(model, 150).findings.empty());
    CHECK(scanModel(model, 149).findings.size() == 1);
}

TEST_CASE("An invisible surface is not geometry a bound guards", "[ut-bound-scan]") {
    const auto scan =
        scanModel(rootOverSquare(Link::Front, boxTo(50), uta::boundscan::PF_INVISIBLE), 2);
    CHECK(scan.findings.empty());
    CHECK(scan.checked == 0);
}

TEST_CASE("An inverted box is its own kind and carries the valid flag", "[ut-bound-scan]") {
    const uta::upkg::Box inverted{.min = {65536, 65536, 65536}, .max = {-65536, -65536, -65536}};
    for (const bool valid : {false, true}) {
        auto box = inverted;
        box.valid = valid;
        const auto scan = scanModel(rootOverSquare(Link::Front, box), 2);
        REQUIRE(scan.findings.size() == 1);
        CHECK(scan.findings[0].kind == Kind::Inverted);
        CHECK(scan.findings[0].valid == valid);
        CHECK(scan.inverted == 1);
        CHECK(scan.checked == 0);
        CHECK(scan.misses == 0);
    }
}

TEST_CASE("A well formed box flagged not valid is reported as invalid", "[ut-bound-scan]") {
    const auto scan = scanModel(rootOverSquare(Link::Front, boxTo(50, false)), 2);
    REQUIRE(scan.findings.size() == 1);
    CHECK(scan.findings[0].kind == Kind::Invalid);
    CHECK(scan.invalid == 1);
    CHECK(scan.misses == 0);
}

TEST_CASE("A node the root does not reach is not scanned", "[ut-bound-scan]") {
    auto model = rootOverSquare(Link::Front, boxTo(200));
    // Node 2: bound by a short box over node 1, but linked from nowhere.
    model.bounds.push_back(boxTo(50));
    auto orphan = bareNode();
    orphan.iFront = 1;
    orphan.iRenderBound = 1;
    model.nodes.push_back(orphan);
    CHECK(scanModel(model, 2).findings.empty());
}

TEST_CASE("Indices outside their tables are read as absent", "[ut-bound-scan]") {
    auto model = rootOverSquare(Link::Front, boxTo(50));
    model.nodes[0].iRenderBound = 7;
    model.nodes[1].iVertPool = 2; // two of four vertices past the pool
    CHECK(scanModel(model, 2).findings.empty());
    model.nodes[0].iRenderBound = 0;
    CHECK(scanModel(model, 2).findings.size() == 1);
}

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
    result.code = uta::boundscan::runCli(views, out, err);
    result.out = out.str();
    result.err = err.str();
    return result;
}

} // namespace

TEST_CASE("ut-bound-scan refuses arguments it cannot use", "[ut-bound-scan]") {
    CHECK(run({}).code == 2);
    CHECK(run({"--tolerance"}).code == 2);
    CHECK(run({"--tolerance", "2"}).code == 2);
    CHECK(run({"--tolerance", "two", "map.unr"}).code == 2);
    CHECK(run({"--tolerance", "-1", "map.unr"}).code == 2);
}

TEST_CASE("A map that cannot be read is one refused line", "[ut-bound-scan]") {
    const auto result = run({"--tolerance", "2", "no-such-dir/DM-Missing.unr"});
    CHECK(result.code == 1);
    CHECK(result.out.starts_with(R"({"kind":"refused","map":"DM-Missing.unr","reason":)"));
    CHECK(result.out.find("md5") == std::string::npos);
    CHECK(result.out.ends_with("}\n"));
}
