// UTA-0476 -- ut-export's scene and camera path, against ExperiRays' EXRAY-0001.
//
// tools/ut-export/Scene.cpp and Path.cpp are compiled into this binary. Each
// case builds a small bundle by hand and reads buildScene's answer, so the
// rules are checked without parsing glTF; one case checks writeGlb's framing.
// The file's last proof, loading through ExperiRays' own reader, waits on its
// EXRAY-0012.
//
// NO TEST NAME CONTAINS A COMMA, AND NONE STARTS WITH A DASH -- BakeCliTest.cpp
// says why.

#include "ut-export/Path.h"
#include "ut-export/Scene.h"

#include "unav/Reach.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <cstring>
#include <numbers>
#include <string>

namespace {

namespace ub = uta::ubundle;
namespace ex = uta::tools::exporter;
using Catch::Approx;

ub::GeometryVertex vertex(float x, float y, float z, std::uint8_t zone = 0) {
    ub::GeometryVertex v;
    v.position = {x, y, z};
    v.normal = {0, 0, 1}; // every fixture surface faces UT's up
    v.zone = zone;
    return v;
}

/// One floor triangle per call, appended as its own batch.
void addTriangle(ub::Geometry& g, std::array<ub::GeometryVertex, 3> corners, std::string material = "m") {
    const auto first = static_cast<std::uint32_t>(g.vertices.size());
    g.batches.push_back({std::move(material), 0, static_cast<std::uint32_t>(g.indices.size()), 3, {}});
    for (std::uint32_t k = 0; k < 3; ++k) {
        g.vertices.push_back(corners[k]);
        g.indices.push_back(first + k);
    }
}

ub::Bundle levelWith(ub::Geometry geometry) {
    ub::Bundle b;
    b.geometry = std::move(geometry);
    b.lights.emplace();
    return b;
}

ex::Scene build(const ub::Bundle& b, bool addedLamps = true) {
    auto scene = ex::buildScene(b, "ut:a:b", addedLamps);
    REQUIRE(scene.has_value());
    return *scene;
}

const ex::Node& nodeWithId(const ex::Scene& s, const std::string& id) {
    for (const ex::Node& n : s.nodes)
        if (n.id == id) return n;
    FAIL("no node " << id);
    return s.nodes.front();
}

/// SS 4.2's winding rule, measured on the output.
double windingAlongNormal(const ex::Mesh& m, std::size_t triangle) {
    const auto& idx = m.primitives.at(0).indices;
    const auto p = [&](std::size_t k) { return m.positions[idx[3 * triangle + k]]; };
    const auto a = p(0), b = p(1), c = p(2);
    const std::array<double, 3> u{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, v{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const std::array<double, 3> cr{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
    const auto& n = m.normals[idx[3 * triangle]];
    return cr[0] * n[0] + cr[1] * n[1] + cr[2] * n[2];
}

} // namespace

TEST_CASE("ut-export maps UT axes to glTF's as (X Z Y) over 50", "[export]") {
    const auto p = ex::toGltf(ex::Vec3{100, 200, 300});
    CHECK(p[0] == Approx(2));
    CHECK(p[1] == Approx(6)); // UT's Z is glTF's up
    CHECK(p[2] == Approx(4));
}

TEST_CASE("ut-export turns a UT yaw into glTF's mirrored rotation", "[export]") {
    // EXRAY-0001 SS 4.2: (x y z w) becomes (-x -z -y w). A quarter turn of yaw
    // about UT's Z is (0 0 s c), so glTF's is (0 -s 0 c).
    const ex::Quat q = ex::quaternionOf(ex::toGltf(ex::rotationOf({0, 16384, 0})));
    const double s = std::sqrt(0.5);
    CHECK(q[0] == Approx(0).margin(1e-9));
    CHECK(q[1] == Approx(-s));
    CHECK(q[2] == Approx(0).margin(1e-9));
    CHECK(q[3] == Approx(s));
}

TEST_CASE("ut-export winds every triangle to follow its normal whichever way it was stored", "[export]") {
    ub::Geometry g;
    addTriangle(g, {vertex(0, 0, 0), vertex(100, 0, 0), vertex(0, 100, 0)}); // anticlockwise from above in UT
    addTriangle(g, {vertex(0, 0, 0), vertex(0, 100, 0), vertex(100, 0, 0)}); // clockwise
    const ex::Scene s = build(levelWith(g));
    const ex::Mesh& level = s.meshes[*nodeWithId(s, "level").mesh];
    CHECK(windingAlongNormal(level, 0) > 0);
    CHECK(windingAlongNormal(level, 1) > 0);
    CHECK(s.report.reversed == 1); // the mirror reverses exactly one of the two
    CHECK(level.normals[0][1] == Approx(1)); // UT's up is glTF's +Y
}

TEST_CASE("ut-export carries each vertex's zone and each triangle's chart", "[export]") {
    ub::Geometry g;
    addTriangle(g, {vertex(0, 0, 0, 3), vertex(100, 0, 0, 3), vertex(0, 100, 0, 3)});
    addTriangle(g, {vertex(0, 0, 9, 5), vertex(0, 100, 9, 5), vertex(100, 0, 9, 5)});
    ub::Bundle b = levelWith(g);
    b.shadowMask.emplace();
    b.shadowMask->vertexChart = {7, 7, 7, ub::MASK_NO_CHART, ub::MASK_NO_CHART, ub::MASK_NO_CHART};
    const ex::Scene s = build(b);
    const ex::Mesh& level = s.meshes[*nodeWithId(s, "level").mesh];
    CHECK(level.zones == std::vector<std::uint8_t>{3, 3, 3, 5, 5, 5});
    CHECK(level.primitives.at(0).triangleChart == std::vector<std::uint32_t>{7, ub::MASK_NO_CHART});
}

TEST_CASE("ut-export puts a triangle with one swaying corner in the sway node", "[export]") {
    ub::Geometry g;
    addTriangle(g, {vertex(0, 0, 0), vertex(100, 0, 0), vertex(0, 100, 0)});
    auto swaying = vertex(0, 0, 50);
    swaying.sway = ub::SwayKind::Cloth;
    swaying.swayWeight = 1;
    addTriangle(g, {swaying, vertex(100, 0, 50), vertex(0, 100, 50)});
    const ex::Scene s = build(levelWith(g));
    CHECK(s.report.swayTriangles == 1);
    CHECK(nodeWithId(s, "level:sway").motion == "sway");
    CHECK(s.meshes[*nodeWithId(s, "level:sway").mesh].primitives.at(0).indices.size() == 3);
    CHECK(s.meshes[*nodeWithId(s, "level").mesh].primitives.at(0).indices.size() == 3);
}

TEST_CASE("ut-export writes spots as spots and leaves out a spot of cone 0", "[export]") {
    ub::Geometry g;
    addTriangle(g, {vertex(0, 0, 0), vertex(100, 0, 0), vertex(0, 100, 0)});
    ub::Bundle b = levelWith(g);
    ub::Light point;
    point.radius = 3;
    point.brightness = 255;
    ub::Light spot = point;
    spot.effect = ub::LE_SPOTLIGHT;
    spot.cone = 128;
    ub::Light dark = spot;
    dark.cone = 0;
    b.lights = {point, spot, dark};
    const ex::Scene s = build(b);
    REQUIRE(s.lights.size() == 2);
    CHECK(s.report.skippedSpots == 1);
    CHECK(s.lights[0].type == ex::PunctualLight::Type::Point);
    CHECK(*s.lights[0].range == Approx(25.0 * 4 / 50));
    CHECK(s.lights[1].type == ex::PunctualLight::Type::Spot);
    CHECK(s.lights[1].utIndex == 1);
    CHECK(s.lights[1].outerConeAngle == Approx(std::numbers::pi / 3)); // acos(1 - 128/256)
    CHECK(std::max({s.lights[1].color[0], s.lights[1].color[1], s.lights[1].color[2]}) == Approx(1));
}

TEST_CASE("ut-export drops the pairs of the lamp state it does not write", "[export]") {
    ub::Geometry g;
    addTriangle(g, {vertex(0, 0, 0), vertex(100, 0, 0), vertex(0, 100, 0)});
    ub::Bundle b = levelWith(g);
    b.lights = {ub::Light{}};
    ub::AddedLamp adding, restoring;
    adding.light.brightness = restoring.light.brightness = 100;
    restoring.restores = true;
    b.lamps = {adding, restoring};
    b.shadowMask.emplace();
    b.shadowMask->vertexChart = {0, 0, 0};
    b.shadowMask->charts = {{0, 3, 1, 1}};
    b.shadowMask->pairs = {{0, 0, 0, 0, {}}, {1, 0, 0, 0, {}}, {2, ub::MASK_ALL_LIT, ub::MASK_ALL_LIT, 1, {}}};

    const ex::Scene on = build(b, true);
    CHECK(on.pairs->light == std::vector<std::uint32_t>{0, 1}); // LITE's and the adding lamp's
    CHECK(on.report.droppedPairs == 1);
    nodeWithId(on, "light:1");

    const ex::Scene off = build(b, false);
    CHECK(off.pairs->light == std::vector<std::uint32_t>{0, 2});
    CHECK(off.pairs->allLit == std::vector<std::uint8_t>{0, 1});
    CHECK(off.pairs->moverReach == std::vector<std::uint8_t>{0, 1});
    nodeWithId(off, "light:2");
}

TEST_CASE("ut-export writes a mover that can move as a moving node at its place", "[export]") {
    ub::Geometry g;
    addTriangle(g, {vertex(0, 0, 0), vertex(100, 0, 0), vertex(0, 100, 0)});
    ub::Bundle b = levelWith(g);
    ub::MoverShape mover;
    mover.exportIndex = 42;
    mover.location = {500, 0, 100};
    mover.rotation = {0, 16384, 0};
    addTriangle(mover.geometry, {vertex(0, 0, 0), vertex(10, 0, 0), vertex(0, 10, 0)});
    b.movers = {mover};
    const ex::Scene s = build(b);
    const ex::Node& node = nodeWithId(s, "mover:42");
    CHECK(node.motion == "moving");
    CHECK(node.mesh.has_value());
    CHECK(node.translation[0] == Approx(10));
    CHECK(node.translation[1] == Approx(2));
    CHECK(node.rotation[1] == Approx(-std::sqrt(0.5)));
}

TEST_CASE("ut-export keys a scene by both files' hashes", "[export]") {
    const std::array<std::byte, 0> unr{};
    const std::array<std::byte, 3> utab{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    // FIPS 180-2's SHA-256 of no bytes, and of "abc".
    CHECK(ex::sceneKeyOf(unr, utab) == "ut:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855:"
                                       "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("ut-export frames a glTF binary file", "[export]") {
    ub::Geometry g;
    addTriangle(g, {vertex(0, 0, 0, 2), vertex(100, 0, 0, 2), vertex(0, 100, 0, 2)});
    const std::vector<std::byte> glb = ex::writeGlb(build(levelWith(g)));
    const auto u32 = [&](std::size_t at) {
        std::uint32_t v = 0;
        std::memcpy(&v, glb.data() + at, 4);
        return v;
    };
    REQUIRE(glb.size() > 28);
    CHECK(u32(0) == 0x46546C67u);
    CHECK(u32(4) == 2);
    CHECK(u32(8) == glb.size());
    const std::uint32_t jsonLength = u32(12);
    CHECK(jsonLength % 4 == 0);
    CHECK(u32(16) == 0x4E4F534Au);
    const std::string json(reinterpret_cast<const char*>(glb.data()) + 20, jsonLength);
    CHECK_THAT(json, Catch::Matchers::ContainsSubstring("\"_XR_ZONE\""));
    CHECK_THAT(json, Catch::Matchers::ContainsSubstring("\"triangle_chart\""));
    CHECK_THAT(json, Catch::Matchers::ContainsSubstring("\"scene_key\":\"ut:a:b\""));
    CHECK(u32(20 + jsonLength + 4) == 0x004E4942u);
    CHECK(20 + jsonLength + 8 + u32(20 + jsonLength) == glb.size());
}

TEST_CASE("ut-export's walkers tour the navigation graph and walk back", "[export]") {
    // Three points in a row, A - B - C, joined both ways; A is a PlayerStart.
    ub::Bundle b;
    b.placements.emplace();
    b.placements->classes = {ub::ActorClass{"engine.pathnode", true, {}, ub::AncestryEnd::Root, {}, {}}};
    b.nav.emplace();
    for (std::uint32_t i = 0; i < 3; ++i) {
        ub::PropertyRecord location{"Location", 0, ub::ValueKind::Vector, std::array<float, 3>{400.0f * i, 0, 0}};
        b.placements->actors.push_back({10 + i, "Point" + std::to_string(i), 0, {location}});
        b.nav->nodes.push_back({10 + i, i == 0 ? "PlayerStart" : "PathNode", 0, 0});
    }
    const auto link = [&](std::uint32_t from, std::uint32_t to) {
        b.nav->edges.push_back({from, to, 400, 100, 100, uta::unav::R_WALK, 0, 0});
    };
    link(0, 1), link(1, 0), link(1, 2), link(2, 1);
    for (std::uint32_t i = 0, at = 0; i < 3; ++i) {
        const std::uint32_t count = i == 1 ? 2 : 1;
        b.nav->nodes[i].firstEdge = at;
        b.nav->nodes[i].edgeCount = count;
        at += count;
    }

    ex::PathReport report;
    ex::PathOptions options;
    options.seconds = 2;
    const auto path = ex::writeCameraPath(b, "ut:a:b", options, report);
    REQUIRE(path.has_value());
    CHECK(report.walkers == 1);          // one PlayerStart
    CHECK(report.locatedNodes == 3);
    CHECK(report.tourStops == 5);        // A B C B A
    CHECK(report.frames == 61);
    CHECK_THAT(*path, Catch::Matchers::ContainsSubstring("\"scene_keys\":[\"ut:a:b\"]"));
    CHECK_THAT(*path, Catch::Matchers::ContainsSubstring("\"rest\":{\"translation\":[0,0,0]}"));
    // At 400 units a second the walker stands on B, 8 m along glTF's x, at 1 s.
    CHECK_THAT(*path, Catch::Matchers::ContainsSubstring("{\"time\":1,"));
    CHECK_THAT(*path, Catch::Matchers::ContainsSubstring("\"walker:0\":{\"translation\":[8,0,0]"));
}
