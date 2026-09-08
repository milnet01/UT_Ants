// UTA-0007's tier-1 cases for the builder.
//
// docs/specs/UTA-0007-room-partition-and-lookup.md SS 4.2, SS 4.4, SS 4.5,
// INV-1, INV-6 and INV-8. RoomMapTest.cpp holds the lookup's own cases.
//
// The fixtures are `Model` values written here rather than real packages, so
// each case isolates one rule. Every plane is axis-aligned and every bound is
// a multiple of the default sample spacing, which makes the sample lattice
// exact: a case can name the cells it expects rather than approximating them.
//
// What these CANNOT check is INV-2 -- that the descent agrees with the source
// Model's own zone records over real geometry. That is the real-asset tier's.

#include "umap/Build.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

using uta::umap::buildRoomMap;
using uta::umap::Footprint;
using uta::umap::NO_ROOM;
using uta::umap::Point2;
using uta::umap::Room;
using uta::umap::RoomBuildOptions;
using uta::umap::RoomBuildResult;
using uta::umap::ZONE_REFUSED;
using Model = uta::upkg::Model;
using BspNode = uta::upkg::BspNode;

namespace {

/// The file's own "no child" sentinel, spelled as `upkg` returns it.
constexpr std::int32_t NONE = -1;

/// A node splitting on `normal . p == w`, with no children until one is set.
///
/// `BspNode`'s own iFront and iBack default to 0, which is node 0 -- so a
/// fixture that forgets to set them builds a descent that loops back on
/// itself. Every fixture here goes through this helper for that reason.
BspNode plane(float nx, float ny, float nz, float w) {
    BspNode node;
    node.plane.normal.x = nx;
    node.plane.normal.y = ny;
    node.plane.normal.z = nz;
    node.plane.w = w;
    node.iFront = NONE;
    node.iBack = NONE;
    node.iLeaf = {NONE, NONE};
    return node;
}

/// A cube of level, sampled at the default spacing.
void boundsCube(Model& model, float half) {
    model.boundsValid = true;
    model.boundsMin.x = model.boundsMin.y = model.boundsMin.z = -half;
    model.boundsMax.x = model.boundsMax.y = model.boundsMax.z = half;
}

/// One plane at z = 0. Front (z >= 0) is zone 1, back is zone 2.
///
/// The zone table has a fourth entry that no leaf names, which is what INV-1's
/// "and for no other" half turns on.
Model twoZoneModel() {
    Model model;
    boundsCube(model, 64.0F);

    BspNode node = plane(0.0F, 0.0F, 1.0F, 0.0F);
    node.iLeaf = {1, 0}; // 1 = front, 0 = back
    model.nodes.push_back(node);

    model.leaves.resize(2);
    model.leaves[0].iZone = 1; // front
    model.leaves[1].iZone = 2; // back
    model.zones.resize(4);     // zone 3 is declared and named by nothing
    return model;
}

/// Two planes crossing at the origin, with one zone on each DIAGONAL pair of
/// quadrants. Each room is therefore two volumes that touch only at a corner
/// -- SS 4.4 step 5's "two pools of one water zone".
Model diagonalQuadrantModel() {
    Model model;
    boundsCube(model, 64.0F);

    BspNode split = plane(1.0F, 0.0F, 0.0F, 0.0F);
    split.iFront = 1;
    split.iBack = 2;
    model.nodes.push_back(split);

    BspNode east = plane(0.0F, 1.0F, 0.0F, 0.0F);
    east.iLeaf = {1, 0}; // (+x,+y) -> leaf 0, (+x,-y) -> leaf 1
    model.nodes.push_back(east);

    BspNode west = plane(0.0F, 1.0F, 0.0F, 0.0F);
    west.iLeaf = {3, 2}; // (-x,+y) -> leaf 2, (-x,-y) -> leaf 3
    model.nodes.push_back(west);

    model.leaves.resize(4);
    model.leaves[0].iZone = 1; // (+x,+y)
    model.leaves[1].iZone = 2; // (+x,-y)
    model.leaves[2].iZone = 2; // (-x,+y)
    model.leaves[3].iZone = 1; // (-x,-y)
    model.zones.resize(3);
    return model;
}

/// A box of zone 2 in the middle of zone 1, so zone 1's footprint has a hole.
///
/// Four nested planes carve [-half, half] on x and y; everything outside any
/// of them falls out to a leaf of zone 1. A `half` under the sample spacing
/// leaves the inner room exactly one cell, which is the size SS 4.4 step 6's
/// three-vertex floor turns on.
Model boxInARoomModel(float half = 32.0F) {
    Model model;
    boundsCube(model, 96.0F);

    struct Side {
        float nx, ny, w;
    };
    const Side sides[4] = {{1.0F, 0.0F, -half},
                           {-1.0F, 0.0F, -half},
                           {0.0F, 1.0F, -half},
                           {0.0F, -1.0F, -half}};
    for (int i = 0; i < 4; ++i) {
        BspNode node = plane(sides[i].nx, sides[i].ny, 0.0F, sides[i].w);
        node.iLeaf[0] = i;                       // outside this side: zone 1
        if (i < 3) {
            node.iFront = i + 1;                 // still inside: test the next
        } else {
            node.iLeaf[1] = 4;                   // inside all four: zone 2
        }
        model.nodes.push_back(node);
    }

    model.leaves.resize(5);
    for (int i = 0; i < 4; ++i) model.leaves[static_cast<std::size_t>(i)].iZone = 1;
    model.leaves[4].iZone = 2;
    model.zones.resize(3);
    return model;
}

/// One plane at x + y = 0, so the sampled boundary between the two zones is a
/// diagonal STAIRCASE rather than an axis-aligned wall -- the shape SS 4.4
/// step 6's simplifier exists to remove.
Model diagonalWallModel() {
    Model model;
    boundsCube(model, 64.0F);

    BspNode node = plane(0.70710678F, 0.70710678F, 0.0F, 0.0F);
    node.iLeaf = {1, 0};
    model.nodes.push_back(node);

    model.leaves.resize(2);
    model.leaves[0].iZone = 1;
    model.leaves[1].iZone = 2;
    model.zones.resize(3);
    return model;
}

/// The two diagonal quadrants of `diagonalQuadrantModel`, bridged along the
/// level's south and east edges so they are ONE four-connected region that
/// still touches itself at the origin corner.
///
/// This is the shape SS 4.4 step 5's tracer has to choose at: the corner has
/// two ways out, and taking the wrong one splits the region's boundary into
/// two rings with nothing left to call the outer one.
Model pinchedRoomModel() {
    Model model;
    boundsCube(model, 64.0F);

    BspNode south = plane(0.0F, 1.0F, 0.0F, -48.0F); // back: the y = -64 row
    south.iFront = 1;
    south.iLeaf[0] = 0;
    model.nodes.push_back(south);

    BspNode east = plane(-1.0F, 0.0F, 0.0F, -48.0F); // back: the x = 64 column
    east.iFront = 2;
    east.iLeaf[0] = 1;
    model.nodes.push_back(east);

    BspNode split = plane(1.0F, 0.0F, 0.0F, 0.0F);
    split.iFront = 3;
    split.iBack = 4;
    model.nodes.push_back(split);

    BspNode inner = plane(0.0F, 1.0F, 0.0F, 0.0F);
    inner.iLeaf = {3, 2}; // front (+x,+y) joins the region; back (+x,-y) does not
    model.nodes.push_back(inner);

    BspNode outer = plane(0.0F, 1.0F, 0.0F, 0.0F);
    outer.iLeaf = {5, 4}; // front (-x,+y) does not; back (-x,-y) joins it
    model.nodes.push_back(outer);

    model.leaves.resize(6);
    model.leaves[0].iZone = 1; // south bridge
    model.leaves[1].iZone = 1; // east bridge
    model.leaves[2].iZone = 1; // (+x,+y)
    model.leaves[3].iZone = 2;
    model.leaves[4].iZone = 2;
    model.leaves[5].iZone = 1; // (-x,-y)
    model.zones.resize(3);
    return model;
}

/// Three zones stacked on z, so a room's floor sits exactly on a band edge.
///
/// Bottom is z < 0 (zone 3), middle is 0 <= z < 32 (zone 1), top is z >= 32
/// (zone 2). Sampled at the default spacing the middle room is one slice
/// thick at z = 0, which is also where a band opens -- and SS 4.5 step 3's
/// half-open [bands[i], bands[i+1]) is what decides whether it also sits on
/// the band below.
Model stackedFloorsModel() {
    Model model;
    boundsCube(model, 64.0F);

    BspNode ground = plane(0.0F, 0.0F, 1.0F, 0.0F);
    ground.iFront = 1;
    ground.iLeaf[0] = 0; // z < 0
    model.nodes.push_back(ground);

    BspNode ceiling = plane(0.0F, 0.0F, 1.0F, 32.0F);
    ceiling.iLeaf = {2, 1}; // front z >= 32 -> leaf 1; back -> leaf 2
    model.nodes.push_back(ceiling);

    model.leaves.resize(3);
    model.leaves[0].iZone = 3;
    model.leaves[1].iZone = 2;
    model.leaves[2].iZone = 1;
    model.zones.resize(4);
    return model;
}

/// A node side that is neither a child nor a leaf, so the descent falls back
/// to the node's OWN zone record -- SS 4.3 step 5.
///
/// Every other fixture here terminates at a leaf, which leaves `iZone` copied
/// but never read. The zones this node records are named by leaves elsewhere
/// in the same model, because SS 4.2 creates a room only for a zone a LEAF
/// names -- a node record alone reaches nothing.
Model zoneOnANodeSideModel() {
    Model model;
    boundsCube(model, 64.0F);

    BspNode ground = plane(0.0F, 0.0F, 1.0F, 0.0F);
    ground.iFront = 1;
    ground.iZone[0] = 2; // back: no child and no leaf, so the record answers
    model.nodes.push_back(ground);

    BspNode ceiling = plane(0.0F, 0.0F, 1.0F, 32.0F);
    ceiling.iLeaf = {1, 0}; // front z >= 32 -> leaf 0; back -> leaf 1
    model.nodes.push_back(ceiling);

    model.leaves.resize(2);
    model.leaves[0].iZone = 1;
    model.leaves[1].iZone = 2;
    model.zones.resize(3);
    return model;
}

/// The room built for `zone`, or nullptr.
const Room* roomForZone(const RoomBuildResult& built, std::uint32_t zone) {
    if (zone >= built.map.roomForZone.size()) return nullptr;
    const std::uint32_t at = built.map.roomForZone[zone];
    if (at == NO_ROOM) return nullptr;
    return &built.map.rooms[at];
}

/// INV-6's shape check, applied to one ring.
void requireClosedRing(const std::vector<Point2>& ring) {
    REQUIRE(ring.size() >= 3);
    const bool repeatsFirst = ring.front().x == ring.back().x && ring.front().y == ring.back().y;
    REQUIRE_FALSE(repeatsFirst);
}

/// The axis-aligned bounds of a ring, as {minX, minY, maxX, maxY}.
std::vector<float> ringBounds(const std::vector<Point2>& ring) {
    std::vector<float> box{ring[0].x, ring[0].y, ring[0].x, ring[0].y};
    for (const Point2& p : ring) {
        box[0] = std::min(box[0], p.x);
        box[1] = std::min(box[1], p.y);
        box[2] = std::max(box[2], p.x);
        box[3] = std::max(box[3], p.y);
    }
    return box;
}

} // namespace

TEST_CASE("a room exists for each zone a leaf names, and for no other", "[umap]") {
    // INV-1. The builder that walks the ZONE TABLE rather than the leaves
    // passes every other case in this file: it emits the same two rooms plus
    // one nobody can reach. Zone 3 below is what separates them.
    const RoomBuildResult built = buildRoomMap(twoZoneModel()).value();

    REQUIRE(built.map.rooms.size() == 2);
    CHECK(built.map.rooms[0].zoneIndex == 1);
    CHECK(built.map.rooms[1].zoneIndex == 2);

    REQUIRE(built.map.roomForZone.size() == 4);
    CHECK(built.map.roomForZone[0] == NO_ROOM); // the engine's null zone
    CHECK(built.map.roomForZone[3] == NO_ROOM); // declared, named by no leaf
    CHECK(roomForZone(built, 1) != nullptr);
    CHECK(roomForZone(built, 2) != nullptr);
}

TEST_CASE("a leaf naming a zone outside the table is refused, not clamped", "[umap]") {
    // SS 6. Clamping silently MOVES a room, which is the harm; storing the raw
    // value would do the same through SS 4.6's narrowing to a byte.
    // Two leaves name the same missing zone and a third names another, so the
    // report is graded on being ascending and free of repeats rather than
    // just on holding the value: a zone named by a million leaves is one
    // refusal.
    Model model = twoZoneModel();
    model.leaves.resize(4);
    model.leaves[1].iZone = 99;
    model.leaves[2].iZone = 100;
    model.leaves[3].iZone = 99;

    const RoomBuildResult built = buildRoomMap(model).value();

    REQUIRE(built.report.refusedZones == std::vector<std::uint32_t>{99, 100});
    REQUIRE(built.map.leafZone.size() == 4);
    CHECK(built.map.leafZone[1] == ZONE_REFUSED);
    CHECK(built.map.leafZone[2] == ZONE_REFUSED);
    CHECK(built.map.leafZone[3] == ZONE_REFUSED);
    // No room was created for it, and none was created for the zone its
    // truncation would have aliased onto either.
    CHECK(built.map.rooms.size() == 1);
    CHECK(built.map.rooms[0].zoneIndex == 1);
}

TEST_CASE("a zone table past the engine's ceiling is refused", "[umap]") {
    // SS 6: a table of 300 zones passes every per-leaf membership check and
    // then aliases zone 300 onto 44 through the narrowing.
    Model model = twoZoneModel();
    model.zones.resize(65);

    const auto built = buildRoomMap(model);
    REQUIRE_FALSE(built.has_value());
    CHECK(built.error().code() == uta::ErrorCode::MalformedData);
}

TEST_CASE("a sample spacing that would not terminate is refused", "[umap]") {
    RoomBuildOptions options;
    options.sampleSpacing = 0.0F;

    const auto built = buildRoomMap(twoZoneModel(), options);
    REQUIRE_FALSE(built.has_value());
    CHECK(built.error().code() == uta::ErrorCode::InvalidArgument);
}

TEST_CASE("a bounding box that would not sample is refused", "[umap]") {
    // The lattice is the level box over the spacing, cubed (SS 13), so a box
    // this project did not write bounds nothing at all: the per-axis count
    // stops fitting in the type that holds it long before the loop is merely
    // slow. Refusing says the file is wrong, where degrading would hand ubake
    // a map it could not tell from a thin one.
    Model model = twoZoneModel();
    model.boundsMin.x = -1.0e20F;
    model.boundsMax.x = 1.0e20F;

    const auto built = buildRoomMap(model);
    REQUIRE_FALSE(built.has_value());
    CHECK(built.error().code() == uta::ErrorCode::MalformedData);
}

TEST_CASE("every sampled room has a closed footprint, and every unsampled one is reported",
          "[umap]") {
    SECTION("a room filling the level traces one square") {
        const RoomBuildResult built = buildRoomMap(twoZoneModel()).value();

        const Room* front = roomForZone(built, 1);
        REQUIRE(front != nullptr);
        REQUIRE(front->parts.size() == 1);
        requireClosedRing(front->parts[0].outer);
        CHECK(front->parts[0].holes.empty());

        // Cells are centred on their samples, so the traced square runs half
        // a spacing past the outermost sample on every side: -64 - 16 = -80.
        // Exactly four vertices, because the simplifier removes the collinear
        // run along each side and keeps the corners.
        CHECK(front->parts[0].outer.size() == 4);
        CHECK(ringBounds(front->parts[0].outer) == std::vector<float>{-80, -80, 80, 80});

        CHECK(built.report.roomsWithoutFootprint.empty());
    }

    SECTION("a zone occupying two disjoint volumes is one room with two parts") {
        // SS 4.4 step 5. The two quadrants touch only at a corner, so a
        // builder using eight-way connectivity returns ONE part here.
        const RoomBuildResult built = buildRoomMap(diagonalQuadrantModel()).value();

        const Room* room = roomForZone(built, 1);
        REQUIRE(room != nullptr);
        REQUIRE(room->parts.size() == 2);
        for (const Footprint& part : room->parts) {
            requireClosedRing(part.outer);
            CHECK(part.holes.empty());
        }
    }

    SECTION("a room enclosing another traces the inner one as a hole") {
        const RoomBuildResult built = buildRoomMap(boxInARoomModel()).value();

        const Room* around = roomForZone(built, 1);
        REQUIRE(around != nullptr);
        REQUIRE(around->parts.size() == 1);
        REQUIRE(around->parts[0].holes.size() == 1);
        requireClosedRing(around->parts[0].outer);
        requireClosedRing(around->parts[0].holes[0]);

        // The outer ring is the level box grown by half a cell; the hole is
        // the inner box's own three sampled columns, likewise grown.
        CHECK(ringBounds(around->parts[0].outer) == std::vector<float>{-112, -112, 112, 112});
        CHECK(ringBounds(around->parts[0].holes[0]) == std::vector<float>{-48, -48, 48, 48});

        const Room* inside = roomForZone(built, 2);
        REQUIRE(inside != nullptr);
        REQUIRE(inside->parts.size() == 1);
        CHECK(inside->parts[0].holes.empty());
    }

    SECTION("a room no sample reaches is kept and reported") {
        // SS 4.4's stated approximation: a volume thinner than the spacing in
        // all three axes catches nothing. Leaf 2 below is named by no node, so
        // its zone is a room the descent can never return.
        Model model = twoZoneModel();
        model.leaves.resize(3);
        model.leaves[2].iZone = 3;

        const RoomBuildResult built = buildRoomMap(model).value();

        const Room* orphan = roomForZone(built, 3);
        REQUIRE(orphan != nullptr);
        CHECK(orphan->parts.empty());
        // Not a measurement -- the struct defaults, which SS 4.5 step 1
        // excludes from clustering.
        CHECK(orphan->minZ == 0.0F);
        CHECK(orphan->maxZ == 0.0F);
        CHECK(built.report.roomsWithoutFootprint == std::vector<std::uint32_t>{3});

        // SS 4.5 step 1: it does not vote on where the bands fall either. Its
        // midpoint would be zero, which is not a measurement -- and zero sits
        // between the two real midpoints here, so letting it vote invents a
        // band nothing is on. A separation small enough to split the two real
        // rooms is what makes that visible.
        RoomBuildOptions options;
        options.floorSeparation = 16.0F;
        const RoomBuildResult banded = buildRoomMap(model, options).value();
        CHECK(banded.map.bands == std::vector<float>{-48.0F, 32.0F});
    }
}

TEST_CASE("every room sits on at least one floor", "[umap]") {
    SECTION("rooms within one separation share a band") {
        const RoomBuildResult built = buildRoomMap(twoZoneModel()).value();

        REQUIRE(built.map.bands.size() == 1);
        for (const Room& room : built.map.rooms) {
            REQUIRE(room.floors.size() == 1);
            CHECK(room.floors[0] == 0);
        }
    }

    SECTION("a room spanning two bands joins both") {
        // SS 4.5 step 4's stairwell. Midpoints are +32 (front, 0..64) and -48
        // (back, -64..-32); a separation of 32 splits them, and the front
        // room's extent still reaches down into the lower band.
        RoomBuildOptions options;
        options.floorSeparation = 32.0F;

        const RoomBuildResult built = buildRoomMap(twoZoneModel(), options).value();

        REQUIRE(built.map.bands.size() == 2);
        CHECK(built.map.bands[0] == -48.0F); // the LOWEST midpoint in cluster 0
        CHECK(built.map.bands[1] == 32.0F);

        const Room* front = roomForZone(built, 1);
        const Room* back = roomForZone(built, 2);
        REQUIRE(front != nullptr);
        REQUIRE(back != nullptr);
        CHECK(front->floors == std::vector<std::uint16_t>{0, 1});
        CHECK(back->floors == std::vector<std::uint16_t>{0});
    }

    SECTION("a level whose rooms caught no sample still has a band to sit on") {
        // No box, so no samples (SS 4.4 step 1 has none to walk). Every room
        // lands on band 0 by SS 4.5 step 5 -- which requires band 0 to exist.
        Model model = twoZoneModel();
        model.boundsValid = false;

        const RoomBuildResult built = buildRoomMap(model).value();

        REQUIRE(built.map.rooms.size() == 2);
        REQUIRE(built.map.bands.size() == 1);
        for (const Room& room : built.map.rooms) {
            CHECK(room.parts.empty());
            REQUIRE(room.floors.size() == 1);
            CHECK(room.floors[0] == 0);
        }
        CHECK(built.report.roomsWithoutFootprint == std::vector<std::uint32_t>{1, 2});
    }

    SECTION("a level with no zones has no rooms and no bands") {
        // SS 6. roomForZone is empty, so every zone is at or past its size.
        Model model = twoZoneModel();
        model.zones.clear();

        const RoomBuildResult built = buildRoomMap(model).value();

        CHECK(built.map.rooms.empty());
        CHECK(built.map.bands.empty());
        CHECK(built.map.roomForZone.empty());
    }
}

TEST_CASE("every floor a room names indexes a band", "[umap]") {
    // INV-8's second half, checked over every fixture in this file rather than
    // once: a floor index past the band table is what drops a room off the map
    // screen, and it costs nothing to ask of all of them.
    const Model models[3] = {twoZoneModel(), diagonalQuadrantModel(), boxInARoomModel()};
    for (const Model& model : models) {
        const RoomBuildResult built = buildRoomMap(model).value();
        for (const Room& room : built.map.rooms) {
            REQUIRE_FALSE(room.floors.empty());
            for (std::uint16_t floor : room.floors) CHECK(floor < built.map.bands.size());
        }
    }
}

TEST_CASE("the simplifier removes the staircase and never a ring's last three vertices",
          "[umap]") {
    SECTION("a diagonal wall keeps its steps at the default tolerance and loses them above it") {
        // SS 4.4 step 6. A tolerance of zero is the traced ring with only its
        // collinear midpoints gone, which is the baseline the simplifier has
        // to beat; the default sits under the 22.6-unit deviation a 32-unit
        // step makes, so it beats it only above that.
        RoomBuildOptions exact;
        exact.simplifyTolerance = 0.0F;
        RoomBuildOptions coarse;
        coarse.simplifyTolerance = 32.0F;

        const RoomBuildResult stepped = buildRoomMap(diagonalWallModel(), exact).value();
        const RoomBuildResult smoothed = buildRoomMap(diagonalWallModel(), coarse).value();

        const Room* steppedRoom = roomForZone(stepped, 1);
        const Room* smoothedRoom = roomForZone(smoothed, 1);
        REQUIRE(steppedRoom != nullptr);
        REQUIRE(smoothedRoom != nullptr);
        REQUIRE(steppedRoom->parts.size() == 1);
        REQUIRE(smoothedRoom->parts.size() == 1);

        const std::vector<Point2>& steps = steppedRoom->parts[0].outer;
        const std::vector<Point2>& smooth = smoothedRoom->parts[0].outer;
        requireClosedRing(steps);
        requireClosedRing(smooth);
        CHECK(smooth.size() < steps.size());

        // It REMOVES vertices and never invents one, so the coarse ring is a
        // subset of the exact one. Without this a simplifier that resampled
        // the ring would pass the count check above.
        for (const Point2& kept : smooth) {
            const bool present = std::any_of(steps.begin(), steps.end(), [&](const Point2& p) {
                return p.x == kept.x && p.y == kept.y;
            });
            CHECK(present);
        }
    }

    SECTION("a one-cell room keeps three vertices however coarse the tolerance") {
        // SS 4.4 step 6's floor, stated there precisely so INV-6 does not turn
        // on an implementer's `>` versus `>=`.
        RoomBuildOptions options;
        options.simplifyTolerance = 1000.0F;

        const RoomBuildResult built = buildRoomMap(boxInARoomModel(1.0F), options).value();

        const Room* inside = roomForZone(built, 2);
        REQUIRE(inside != nullptr);
        REQUIRE(inside->parts.size() == 1);
        requireClosedRing(inside->parts[0].outer);
    }
}

TEST_CASE("a leaf naming zone 0 gets no room", "[umap]") {
    // INV-1's "no room names zone 0". SS 4.2 measured no leaf naming it across
    // 2759160 leaves of the reference install, so nothing in that corpus
    // reaches this line -- which is exactly why a fixture has to. Populating
    // roomForZone[0] turns "outside the level" into a room the map screen
    // draws and the server records exploration against.
    Model model = twoZoneModel();
    model.leaves[1].iZone = 0;

    const RoomBuildResult built = buildRoomMap(model).value();

    CHECK(built.map.roomForZone[0] == NO_ROOM);
    REQUIRE(built.map.rooms.size() == 1);
    CHECK(built.map.rooms[0].zoneIndex == 1);
    // It is IN the table, so it is not a refusal either -- SS 6 keeps those
    // two apart, and zone 0 is a legal index that is simply not a room.
    CHECK(built.report.refusedZones.empty());
    CHECK(built.map.leafZone[1] == 0);
}

TEST_CASE("a room that touches itself at a corner traces one outer ring", "[umap]") {
    // SS 4.4 step 5 gives each connected component ONE outer ring and calls
    // every other ring of that component an enclosed one. A corner where the
    // region meets itself is the only place a tracer has a choice; taking the
    // clockwise way out closes the ring early and leaves the rest of the room
    // to be mistaken for a hole in it.
    const RoomBuildResult built = buildRoomMap(pinchedRoomModel()).value();

    const Room* room = roomForZone(built, 1);
    REQUIRE(room != nullptr);
    REQUIRE(room->parts.size() == 1);
    requireClosedRing(room->parts[0].outer);

    // ONE outer ring, running through the corner and round the whole level,
    // and ONE hole: the two cells the bridges seal off behind that corner.
    // Closing at the corner instead yields a second outer ring with nothing
    // to distinguish it from a hole, so the count below is what separates the
    // two rules rather than the ring shapes.
    REQUIRE(room->parts[0].holes.size() == 1);
    requireClosedRing(room->parts[0].holes[0]);
    CHECK(ringBounds(room->parts[0].outer) == std::vector<float>{-80, -80, 80, 80});
    CHECK(ringBounds(room->parts[0].holes[0]) == std::vector<float>{-16, -48, 48, -16});
}

TEST_CASE("a band's lower edge belongs to it and its upper edge does not", "[umap]") {
    // SS 4.5 step 3 makes band i cover [bands[i], bands[i+1]), so a room whose
    // floor sits exactly on bands[i+1] belongs to band i+1 and NOT to band i.
    // A half-open reading and a closed one differ only at that one value, and
    // only a room standing on it can tell them apart.
    RoomBuildOptions options;
    options.floorSeparation = 32.0F;

    const RoomBuildResult built = buildRoomMap(stackedFloorsModel(), options).value();

    REQUIRE(built.map.bands == std::vector<float>{-48.0F, 0.0F, 48.0F});

    const Room* bottom = roomForZone(built, 3);
    const Room* middle = roomForZone(built, 1);
    const Room* top = roomForZone(built, 2);
    REQUIRE(bottom != nullptr);
    REQUIRE(middle != nullptr);
    REQUIRE(top != nullptr);

    // The middle room is one sample slice at z = 0, which is bands[1] exactly.
    CHECK(middle->minZ == 0.0F);
    CHECK(middle->maxZ == 0.0F);
    CHECK(middle->floors == std::vector<std::uint16_t>{1});

    CHECK(bottom->floors == std::vector<std::uint16_t>{0});
    CHECK(top->floors == std::vector<std::uint16_t>{1, 2});
}

TEST_CASE("a node side with no leaf answers from the node's own zone record", "[umap]") {
    // SS 4.3 step 5, and SS 4.6's copy is what it reads. The record is a
    // two-entry array indexed 1 = front, 0 = back, so a copy that swaps the
    // pair compiles, keeps every room, and moves the level's whole lower half
    // out of the room it belongs to.
    const RoomBuildResult built = buildRoomMap(zoneOnANodeSideModel()).value();

    const Room* lower = roomForZone(built, 2);
    const Room* upper = roomForZone(built, 1);
    REQUIRE(lower != nullptr);
    REQUIRE(upper != nullptr);

    // Zone 2 is reached two ways -- by leaf above z = 0 and by the node's own
    // back record below it -- so its extent runs to the floor of the level.
    CHECK(lower->minZ == -64.0F);
    CHECK(lower->maxZ == 0.0F);
    CHECK(upper->minZ == 32.0F);
    CHECK(upper->maxZ == 64.0F);
}
