// UTA-0111's builder cases: the level's collision tree and each mover's.
//
// docs/specs/UTA-0111-level-collision.md SS 4.3 to SS 4.6, INV-3 to INV-7.
// The container cases are tests/unit/BundleCollisionTest.cpp.
//
// THE MODELS ARE BUILT IN MEMORY for INV-3, INV-4, INV-5 and INV-7's
// per-refusal cases, never through ModelExportWriter: that writer writes its
// Node::iFront where upkg::readModel reads iBack (UTA-0122), so a fixture's
// labels would not be the fields the baker reads (SS 7).
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "BakeFixture.h"

#include "core/Jobs.h"
#include "support/UnrealPackageBuilder.h"
#include "ubake/Actors.h"
#include "ubake/Bake.h"
#include "ubake/Collision.h"
#include "ubake/Movers.h"
#include "ubundle/Bundle.h"
#include "umat/Library.h"
#include "umat/Material.h"
#include "upkg/Geometry.h"
#include "upkg/Level.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace uta::test::bake;
using uta::ErrorCode;
using uta::test::asBytes;
using uta::ubake::buildCollision;
using uta::ubake::buildMover;
using uta::ubake::buildMoverCollision;
using uta::ubake::MaterialLookup;
using uta::ubake::SurfaceMaterial;
using uta::ubundle::CollisionNode;
using uta::ubundle::CollisionTree;
using uta::upkg::BspNode;
using uta::upkg::BspSurf;
using uta::upkg::Model;
using uta::upkg::ObjectReferenceKind;
using uta::upkg::Package;

namespace {

constexpr float HALF_ROOT_TWO = 0.70710677F;

/// Bit 30 of a hull entry -- SS 4.3.
constexpr std::int32_t FLIPPED = 0x40000000;

std::uint32_t bitsOf(float value) {
    return std::bit_cast<std::uint32_t>(value);
}

/// A node on `normal` and `w`, with no children, no coplanar link, no hull
/// and no vertices.
BspNode nodeOn(std::array<float, 3> normal, float w) {
    BspNode node;
    node.plane.normal = {normal[0], normal[1], normal[2]};
    node.plane.w = w;
    node.iFront = -1;
    node.iBack = -1;
    node.iPlane = -1;
    node.iCollisionBound = -1;
    return node;
}

/// A box as the hull table stores it: six floats as their bits, min then max.
void addBox(Model& model, std::array<float, 3> min, std::array<float, 3> max) {
    for (const float part : min) model.leafHulls.push_back(std::bit_cast<std::int32_t>(part));
    for (const float part : max) model.leafHulls.push_back(std::bit_cast<std::int32_t>(part));
}

/// INV-3's Model. Node 0's vertex run comes after node 1's in the pool; the
/// two surfaces carry flags no node's nodeFlags can equal; node 0's back and
/// front differ; node 2 has no vertices and a valid iSurf, node 3 none and an
/// iSurf naming no surface.
Model transcribed() {
    Model model;
    for (int i = 0; i < 8; ++i)
        model.points.push_back({static_cast<float>(i), 10.0F * static_cast<float>(i),
                                -static_cast<float>(i)});
    for (const std::int32_t point : {3, 2, 1, 0, 7, 6, 5, 4}) model.verts.push_back({point, 0});
    BspSurf plain;
    plain.polyFlags = 0x00000100u;
    BspSurf portal;
    portal.polyFlags = 0x04000000u;
    model.surfs = {plain, portal};

    BspNode root = nodeOn({0, 0, 1}, 8);
    root.iBack = 1;
    root.iFront = 2;
    root.nodeFlags = 0x04;
    root.iSurf = 1;
    root.iVertPool = 4;
    root.numVertices = 4;
    BspNode back = nodeOn({1, 0, 0}, 2);
    back.nodeFlags = 0x01;
    back.iSurf = 0;
    back.iVertPool = 0;
    back.numVertices = 4;
    BspNode front = nodeOn({0, 1, 0}, -3);
    front.iPlane = 3;
    front.nodeFlags = 0x10;
    front.iSurf = 0;
    BspNode bare = nodeOn({0, 0, -1}, 5);
    bare.nodeFlags = 0x80;
    bare.iSurf = 99;
    model.nodes = {root, back, front, bare};
    model.rootOutside = 0;
    return model;
}

/// INV-4's Model. Node 2 names the run at offset 0 and node 0 the run at
/// offset 9, so ascending offset order is not node order; one entry carries
/// bit 30.
Model hulled() {
    Model model;
    BspNode root = nodeOn({0, 0, 1}, 0);
    root.iBack = 1;
    root.iFront = 2;
    root.iCollisionBound = 9;
    BspNode front = nodeOn({0, 1, 0}, 2);
    front.iCollisionBound = 0;
    model.nodes = {root, nodeOn({1, 0, 0}, 1), front};
    model.leafHulls = {1, 2 | FLIPPED, -1};
    addBox(model, {-1.5F, 2.25F, 3.0F}, {4.0F, 5.5F, 6.75F});
    model.leafHulls.push_back(0);
    model.leafHulls.push_back(-1);
    addBox(model, {-8.0F, -16.0F, -32.0F}, {8.0F, 16.0F, 32.0F});
    return model;
}

/// INV-5's Model: UTA-0119 INV-4's tilted square as node 0, with a hull
/// naming node 0 and, flipped, node 1; and node 1, of no vertices, as node
/// 0's front.
Model tiltedModel() {
    Model model;
    model.points = {{0, 0, 0}, {32, 0, -32}, {32, 64, -32}, {0, 64, 0}};
    model.vectors = {{HALF_ROOT_TWO, 0, HALF_ROOT_TWO}, {1, 0, 0}, {0, 1, 0}};
    for (std::int32_t k = 0; k < 4; ++k) model.verts.push_back({k, 0});
    BspSurf surf;
    surf.pBase = 0;
    surf.vNormal = 0;
    surf.vTextureU = 1;
    surf.vTextureV = 2;
    model.surfs = {surf};

    BspNode square = nodeOn({HALF_ROOT_TWO, 0, HALF_ROOT_TWO}, 0);
    square.iFront = 1;
    square.iSurf = 0;
    square.iVertPool = 0;
    square.numVertices = 4;
    square.iCollisionBound = 0;
    model.nodes = {square, nodeOn({0, 0, 1}, 100)};
    model.leafHulls = {0, 1 | FLIPPED, -1};
    addBox(model, {0, 0, -32}, {32, 64, 0});
    model.rootOutside = 1;
    return model;
}

/// A square tilted 45 degrees about Y -- BakeMoversTest.cpp's.
BrushSpec tiltedSquare() {
    BrushSpec brush;
    brush.corners = {{{0, 0, 0}, {32, 0, -32}, {32, 64, -32}, {0, 64, 0}}};
    brush.normal = {HALF_ROOT_TWO, 0, HALF_ROOT_TWO};
    return brush;
}

MemoryPackages installed() {
    Packer actors;
    actors.addClass("WeaponRemover");
    MemoryPackages packages;
    packages.add("engine", enginePackage());
    packages.add("actorpkg", actors.build());
    packages.add("core", tinyPackage("Object"));
    return packages;
}

/// Every texture wears one made material.
const MaterialLookup LOOKUP = [](uta::upkg::ObjectReference, bool) -> const SurfaceMaterial* {
    static const SurfaceMaterial made{"made", 64.0, 64.0};
    return &made;
};

const uta::upkg::ExportEntry* levelOf(const Package& map) {
    for (const uta::upkg::ExportEntry& entry : map.exports()) {
        if (entry.objectClass.kind() == ObjectReferenceKind::Null) continue;
        const auto name = map.objectName(entry.objectClass);
        if (name.has_value() && *name == "Level") return &entry;
    }
    return nullptr;
}

uta::Result<uta::ubake::BakeResult> bakeMap(const std::vector<std::uint8_t>& bytes,
                                            MemoryPackages& packages) {
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());
    uta::JobSystem jobs(2);
    return uta::ubake::detail::bake(*package, MAP_NAME, packages.resolver(), jobs,
                                    &uta::umat::curated, uta::umat::TEXTURE_BUDGET_BYTES);
}

/// buildCollision refuses `model` with MalformedData, naming `node` and
/// saying `says`.
void refused(const Model& model, std::string_view node, std::string_view says) {
    const auto tree = buildCollision(model);
    REQUIRE_FALSE(tree.has_value());
    CHECK(tree.error().code() == ErrorCode::MalformedData);
    CHECK(tree.error().message().find(node) != std::string_view::npos);
    CHECK(tree.error().message().find(says) != std::string_view::npos);
}

struct Moved {
    uta::ubundle::MoverShape shape;
    uta::ubundle::MoverCollision collision;
};

/// buildMover and buildMoverCollision over tiltedModel(), for one mover
/// carrying `prePivot` and `mainScale`.
Moved moveTilted(PropertySpec prePivot, PropertySpec mainScale) {
    MapBuilder map;
    map.addActor("Door0", map.importClass("Engine", "Mover"),
                 {objectProperty("Brush", map.addBrushModel(tiltedSquare())), std::move(prePivot),
                  std::move(mainScale)});
    MemoryPackages packages = installed();
    const std::vector<std::uint8_t> bytes = map.build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());
    const uta::upkg::ExportEntry* const entry = levelOf(*package);
    REQUIRE(entry != nullptr);
    const auto level = uta::upkg::readLevel(*package, *entry);
    REQUIRE(level.has_value());
    const auto actors = uta::ubake::buildActors(*package, MAP_NAME, *level, packages.resolver());
    REQUIRE(actors.has_value());
    const auto sites = uta::ubake::findMovers(*package, actors->placements);
    REQUIRE(sites.has_value());
    REQUIRE(sites->size() == 1);

    const Model model = tiltedModel();
    auto shape = buildMover(sites->front(), model, actors->placements, LOOKUP);
    REQUIRE(shape.has_value());
    auto collision = buildMoverCollision(sites->front(), model, actors->placements);
    REQUIRE(collision.has_value());
    return {std::move(*shape), std::move(*collision)};
}

/// `point` is one of the shape's vertex positions, bit for bit.
bool isShapePosition(const uta::ubundle::MoverShape& shape, const std::array<float, 3>& point) {
    return std::any_of(shape.geometry.vertices.begin(), shape.geometry.vertices.end(),
                       [&](const auto& vertex) {
                           return bitsOf(vertex.position[0]) == bitsOf(point[0])
                                  && bitsOf(vertex.position[1]) == bitsOf(point[1])
                                  && bitsOf(vertex.position[2]) == bitsOf(point[2]);
                       });
}

} // namespace

// ------------------------------------------------------------------ INV-3

TEST_CASE("INV-3: the level's tree is its Model's fields verbatim", "[ubake][collision]") {
    const Model model = transcribed();
    const auto tree = buildCollision(model);
    REQUIRE(tree.has_value());

    REQUIRE(tree->points.size() == model.points.size());
    for (std::size_t p = 0; p < model.points.size(); ++p) {
        CHECK(bitsOf(tree->points[p][0]) == bitsOf(model.points[p].x));
        CHECK(bitsOf(tree->points[p][1]) == bitsOf(model.points[p].y));
        CHECK(bitsOf(tree->points[p][2]) == bitsOf(model.points[p].z));
    }

    REQUIRE(tree->nodes.size() == model.nodes.size());
    for (std::size_t n = 0; n < model.nodes.size(); ++n) {
        const CollisionNode& out = tree->nodes[n];
        const BspNode& in = model.nodes[n];
        CHECK(bitsOf(out.normal[0]) == bitsOf(in.plane.normal.x));
        CHECK(bitsOf(out.normal[1]) == bitsOf(in.plane.normal.y));
        CHECK(bitsOf(out.normal[2]) == bitsOf(in.plane.normal.z));
        CHECK(bitsOf(out.distance) == bitsOf(in.plane.w));
        CHECK(out.back == in.iBack);
        CHECK(out.front == in.iFront);
        CHECK(out.coplanar == in.iPlane);
        CHECK(out.nodeFlags == in.nodeFlags);
        CHECK(out.hull == -1);
    }
    // Each node's surface's flags where it has vertices, else 0.
    CHECK(tree->nodes[0].polyFlags == 0x04000000u);
    CHECK(tree->nodes[1].polyFlags == 0x00000100u);
    CHECK(tree->nodes[2].polyFlags == 0);
    CHECK(tree->nodes[3].polyFlags == 0);

    // Each outline from its own run of the pool, appended in node order.
    CHECK(tree->nodes[0].firstOutline == 0);
    CHECK(tree->nodes[0].outlineCount == 4);
    CHECK(tree->nodes[1].firstOutline == 4);
    CHECK(tree->nodes[1].outlineCount == 4);
    CHECK(tree->nodes[2].outlineCount == 0);
    CHECK(tree->nodes[3].outlineCount == 0);
    CHECK(tree->outline == std::vector<std::uint32_t>{7, 6, 5, 4, 3, 2, 1, 0});

    CHECK_FALSE(tree->outside);
    CHECK(tree->hulls.empty());

    Model outside = transcribed();
    outside.rootOutside = 1;
    const auto flipped = buildCollision(outside);
    REQUIRE(flipped.has_value());
    CHECK(flipped->outside);
}

// ------------------------------------------------------------------ INV-4

TEST_CASE("INV-4: the hulls are the runs in ascending offset order", "[ubake][collision]") {
    const auto tree = buildCollision(hulled());
    REQUIRE(tree.has_value());
    REQUIRE(tree->hulls.size() == 2);

    // Hull 0 is the run at offset 0, which node 2 names.
    const auto& first = tree->hulls[0];
    REQUIRE(first.planes.size() == 2);
    CHECK(first.planes[0].node == 1);
    CHECK_FALSE(first.planes[0].flipped);
    CHECK(first.planes[1].node == 2);
    CHECK(first.planes[1].flipped);
    CHECK(first.min == std::array<float, 3>{-1.5F, 2.25F, 3.0F});
    CHECK(first.max == std::array<float, 3>{4.0F, 5.5F, 6.75F});

    // Hull 1 is the run at offset 9, which node 0 names.
    const auto& second = tree->hulls[1];
    REQUIRE(second.planes.size() == 1);
    CHECK(second.planes[0].node == 0);
    CHECK_FALSE(second.planes[0].flipped);
    CHECK(second.min == std::array<float, 3>{-8.0F, -16.0F, -32.0F});
    CHECK(second.max == std::array<float, 3>{8.0F, 16.0F, 32.0F});

    CHECK(tree->nodes[0].hull == 1);
    CHECK(tree->nodes[1].hull == -1);
    CHECK(tree->nodes[2].hull == 0);
}

// ------------------------------------------------------------------ INV-5

TEST_CASE("INV-5: a mover's tree is its Model's in its shape's pivot space", "[ubake][collision]") {
    const Moved moved = moveTilted(vectorProperty("PrePivot", 8, 0, 0), scaleProperty("MainScale", 2, -1, 1));
    const CollisionTree& tree = moved.collision.tree;
    CHECK(moved.collision.exportIndex == moved.shape.exportIndex);

    // Every point is one of the shape's positions, bit for bit.
    REQUIRE(tree.points.size() == 4);
    for (const auto& point : tree.points) CHECK(isShapePosition(moved.shape, point));

    // The links and the outline are the Model's, under the mirror too.
    REQUIRE(tree.nodes.size() == 2);
    CHECK(tree.nodes[0].back == -1);
    CHECK(tree.nodes[0].front == 1);
    CHECK(tree.nodes[0].coplanar == -1);
    CHECK(tree.nodes[0].firstOutline == 0);
    CHECK(tree.nodes[0].outlineCount == 4);
    CHECK(tree.outline == std::vector<std::uint32_t>{0, 1, 2, 3});

    // A corner stays on the moved plane, and a point in front stays in front.
    const CollisionNode& plane = tree.nodes[0];
    const auto side = [&](double x, double y, double z) {
        return static_cast<double>(plane.normal[0]) * x + static_cast<double>(plane.normal[1]) * y
               + static_cast<double>(plane.normal[2]) * z - static_cast<double>(plane.distance);
    };
    for (const std::uint32_t k : tree.outline)
        CHECK(std::abs(side(tree.points[k][0], tree.points[k][1], tree.points[k][2])) < 1e-4);
    // (10 / root 2, 0, 10 / root 2) is ten units in front of the square; SS 4.4
    // moves it to (2 (x - 8), -0, z).
    const double along = 10.0 * HALF_ROOT_TWO;
    CHECK(side(2.0 * (along - 8.0), 0.0, along) > 0);

    // The box is moved, and swapped on y, where MainScale is negative.
    REQUIRE(tree.hulls.size() == 1);
    const auto& hull = tree.hulls[0];
    CHECK(hull.min == std::array<float, 3>{-16.0F, -64.0F, -32.0F});
    CHECK(hull.max == std::array<float, 3>{48.0F, 0.0F, 0.0F});
    REQUIRE(hull.planes.size() == 2);
    CHECK(hull.planes[0].node == 0);
    CHECK_FALSE(hull.planes[0].flipped);
    CHECK(hull.planes[1].node == 1);
    CHECK(hull.planes[1].flipped);
}

TEST_CASE("INV-5: a mover's points are its shape's positions under a fractional pivot",
          "[ubake][collision]") {
    // At the case above's numbers every product is exact in float, so points
    // moved by a second function, in float, would agree with the shape by
    // accident. These numbers are not exact, so only SS 4.4's shared function
    // matches bit for bit.
    const Moved moved = moveTilted(vectorProperty("PrePivot", 8.1F, -0.3F, 2.7F),
                                   scaleProperty("MainScale", 1.1F, -0.7F, 3.3F));
    REQUIRE(moved.collision.tree.points.size() == 4);
    for (const auto& point : moved.collision.tree.points) CHECK(isShapePosition(moved.shape, point));
}

// ------------------------------------------------------------------ INV-6

TEST_CASE("INV-6: a mover gets a tree exactly when it gets a shape", "[ubake][collision]") {
    // UTA-0119 INV-3's map: its actors isolate each rule of which actors are
    // movers, and only Mover0 and Brush1 are.
    MapBuilder map;
    const std::int32_t brush = map.addBrushModel(tiltedSquare());
    const std::int32_t mover = map.importClass("Engine", "Mover");
    const std::int32_t brushClass = map.importClass("Engine", "Brush");
    map.addActor("Mover0", mover, {objectProperty("Brush", brush)});
    map.addActor("Brush1", brushClass, {objectProperty("Brush", brush), boolProperty("bStatic", false)});
    map.addActor("Mover2", mover, {objectProperty("Brush", brush), boolProperty("bStatic", true)});
    map.addActor("Brush3", brushClass, {objectProperty("Brush", brush)});
    map.addActor("Remover4", map.importClass("ActorPkg", "WeaponRemover"),
                 {objectProperty("Brush", brush)});
    map.addActor("Door5", map.importClass("NoSuchPkg", "SlidingDoor"), {objectProperty("Brush", brush)});
    map.addActor("Mover6", mover, {objectProperty("Brush", 0)});
    MemoryPackages packages = installed();

    const auto result = bakeMap(map.build(), packages);
    REQUIRE(result.has_value());
    REQUIRE(result->bundle.movers.has_value());
    REQUIRE(result->bundle.collision.has_value());
    const auto& shapes = *result->bundle.movers;
    const auto& trees = result->bundle.collision->movers;
    REQUIRE(shapes.size() == 2);
    REQUIRE(trees.size() == shapes.size());
    for (std::size_t i = 0; i < shapes.size(); ++i) CHECK(trees[i].exportIndex == shapes[i].exportIndex);
}

TEST_CASE("INV-6: a map with no mover writes an empty movers", "[ubake][collision]") {
    MapBuilder map;
    MemoryPackages packages = installed();
    const auto result = bakeMap(map.build(), packages);
    REQUIRE(result.has_value());
    REQUIRE(result->bundle.collision.has_value());
    CHECK(result->bundle.collision->movers.empty());
    CHECK_FALSE(result->bundle.collision->level.nodes.empty());
}

// ------------------------------------------------------------------ INV-7

TEST_CASE("INV-7: a back link naming no node is refused", "[ubake][collision]") {
    Model model = transcribed();
    model.nodes[1].iBack = 4;
    refused(model, "node 1", "iBack 4");
}

TEST_CASE("INV-7: a front link naming no node is refused", "[ubake][collision]") {
    Model model = transcribed();
    model.nodes[1].iFront = -2;
    refused(model, "node 1", "iFront -2");
}

TEST_CASE("INV-7: a coplanar link naming no node is refused", "[ubake][collision]") {
    Model model = transcribed();
    model.nodes[2].iPlane = 4;
    refused(model, "node 2", "iPlane 4");
}

TEST_CASE("INV-7: a node a walk from node 0 reaches twice is refused", "[ubake][collision]") {
    Model model = transcribed();
    model.nodes[1].iBack = 0;
    refused(model, "node 0", "reached twice");
}

TEST_CASE("INV-7: an iSurf naming no surface on a node with vertices is refused",
          "[ubake][collision]") {
    Model model = transcribed();
    model.nodes[0].iSurf = 2;
    refused(model, "node 0", "iSurf 2");
}

TEST_CASE("INV-7: a node of two vertices is refused", "[ubake][collision]") {
    Model model = transcribed();
    model.nodes[0].numVertices = 2;
    refused(model, "node 0", "numVertices 2");
}

TEST_CASE("INV-7: a node of one vertex is refused", "[ubake][collision]") {
    Model model = transcribed();
    model.nodes[1].numVertices = 1;
    refused(model, "node 1", "numVertices 1");
}

TEST_CASE("INV-7: a vertex run past verts is refused", "[ubake][collision]") {
    Model model = transcribed();
    model.nodes[0].iVertPool = 5;
    refused(model, "node 0", "vertex pool 5");
}

TEST_CASE("INV-7: a pVertex naming no point is refused", "[ubake][collision]") {
    Model model = transcribed();
    model.verts[5].pVertex = 8;
    refused(model, "node 0", "pVertex 8");
}

TEST_CASE("INV-7: an iCollisionBound below -1 is refused", "[ubake][collision]") {
    Model model = hulled();
    model.nodes[1].iCollisionBound = -2;
    refused(model, "node 1", "iCollisionBound -2");
}

TEST_CASE("INV-7: an iCollisionBound past leafHulls is refused", "[ubake][collision]") {
    Model model = hulled();
    model.nodes[1].iCollisionBound = static_cast<std::int32_t>(model.leafHulls.size());
    refused(model, "node 1", "iCollisionBound " + std::to_string(model.leafHulls.size()));
}

TEST_CASE("INV-7: a hull run with no -1 is refused", "[ubake][collision]") {
    Model model = hulled();
    model.leafHulls.resize(10); // node 0's run at 9 is its one entry, then the end
    refused(model, "node 0", "no -1");
}

TEST_CASE("INV-7: a hull run with fewer than six entries after its -1 is refused",
          "[ubake][collision]") {
    Model model = hulled();
    model.leafHulls.resize(15); // node 0's run: 0, -1, then four
    refused(model, "node 0", "fewer than six");
}

TEST_CASE("INV-7: a hull entry naming no node is refused", "[ubake][collision]") {
    Model model = hulled();
    model.leafHulls[1] = 7 | FLIPPED;
    refused(model, "node 2", "with bit 30 cleared");
}

TEST_CASE("INV-7: a rootOutside other than 0 or 1 is refused naming the Model",
          "[ubake][collision]") {
    Model model = transcribed();
    model.rootOutside = 2;
    refused(model, "the Model's rootOutside", "2");
}

TEST_CASE("INV-7: a level Model's coplanar link naming no node refuses the bake",
          "[ubake][collision]") {
    MapBuilder map;
    map.setFloorCoplanar(7);
    MemoryPackages packages = installed();
    const auto result = bakeMap(map.build(), packages);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::MalformedData);
    CHECK(result.error().message().find("node 0") != std::string_view::npos);
    CHECK(result.error().message().find("iPlane 7") != std::string_view::npos);
}

TEST_CASE("INV-7: a mover Model's coplanar link naming no node refuses the bake naming the actor",
          "[ubake][collision]") {
    MapBuilder map;
    BrushSpec brush = tiltedSquare();
    brush.iPlane = 5;
    map.addActor("Door0", map.importClass("Engine", "Mover"),
                 {objectProperty("Brush", map.addBrushModel(brush))});
    MemoryPackages packages = installed();
    const auto result = bakeMap(map.build(), packages);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::MalformedData);
    CHECK(result.error().message().find("dm-fixture.door0") != std::string_view::npos);
    CHECK(result.error().message().find("iPlane 5") != std::string_view::npos);
}
