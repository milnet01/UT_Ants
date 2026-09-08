// UTA-0007's tier-1 cases for the point-in-room lookup.
//
// docs/specs/UTA-0007-room-partition-and-lookup.md SS 4.3, INV-3 and INV-4.
//
// The fixtures are RoomMap values written here rather than real packages, so
// each case isolates one rule. buildRoomMap's cases (INV-1, INV-6, INV-8)
// arrive with the builder; nothing here pretends to cover them.
//
// What these CANNOT check is INV-2 -- that the descent agrees with the source
// Model's own zone records over real geometry. That is the real-asset tier's,
// it is off by default, and SS 10 records that the front/back defect is
// invisible to every check that runs by default. The one positive case below
// is the seed of it: it is built so that reading iZone/iLeaf the other way
// round returns the WRONG room rather than no room.

#include "umap/Rooms.h"

#include <catch2/catch_test_macros.hpp>

using uta::umap::INDEX_NONE;
using uta::umap::NO_ROOM;
using uta::umap::Point3;
using uta::umap::RoomMap;
using uta::umap::roomAt;
using uta::umap::ZONE_REFUSED;

namespace {

/// One node splitting on the z = 0 plane, with a leaf on each side.
///
/// Front (z >= 0) is zone 1 and room 0; back (z < 0) is zone 2 and room 1.
/// The two sides resolve to DIFFERENT rooms on purpose: that is what makes a
/// swapped front/back convention a wrong answer rather than an absent one.
RoomMap twoSidedMap() {
    RoomMap map;

    RoomMap::Node node;
    node.normal = Point3{0.0F, 0.0F, 1.0F};
    node.w = 0.0F;
    node.iFront = INDEX_NONE;
    node.iBack = INDEX_NONE;
    node.iLeaf[1] = 0; // front
    node.iLeaf[0] = 1; // back
    map.nodes.push_back(node);

    map.leafZone = {1, 2};
    // Index 0 is the null zone and is always NO_ROOM -- SS 4.2.
    map.roomForZone = {NO_ROOM, 0, 1};
    map.rooms.resize(2);
    map.rooms[0].zoneIndex = 1;
    map.rooms[1].zoneIndex = 2;
    return map;
}

} // namespace

TEST_CASE("the descent reads the front and back sides the engine's way",
          "[umap]") {
    // SS 4.3: iZone and iLeaf are indexed 1 = front, 0 = back. Swapping that
    // read compiles and returns a plausible room for most points while being
    // wrong for all of them, so this case is built to catch the swap by
    // returning the OTHER room rather than NO_ROOM.
    const RoomMap map = twoSidedMap();

    CHECK(roomAt(map, Point3{0.0F, 0.0F, 64.0F}) == 0U);
    CHECK(roomAt(map, Point3{0.0F, 0.0F, -64.0F}) == 1U);

    // A point exactly on the plane takes the FRONT side. SS 4.3 states the
    // choice rather than deriving it: what matters is that a point on a
    // shared wall lands in one room rather than wherever rounding sends it.
    CHECK(roomAt(map, Point3{0.0F, 0.0F, 0.0F}) == 0U);
}

TEST_CASE("zone zero and an empty map are not rooms", "[umap]") {
    // INV-4. Zone 0 is the engine's null zone; SS 4.2 measured that no leaf
    // of 2 759 160 in the reference install names it. Treating it as a room
    // turns "outside the level" into something the map screen draws and the
    // server keeps exploration bits against.
    SECTION("a side recording zone 0 resolves to no room") {
        RoomMap map;
        RoomMap::Node node;
        node.normal = Point3{0.0F, 0.0F, 1.0F};
        node.iZone[1] = 0;
        node.iZone[0] = 0;
        map.nodes.push_back(node);
        // roomForZone[0] deliberately names a REAL room, which a conforming
        // builder never produces (INV-1). That is the point: it makes the
        // zone-0 guard the only thing that can return NO_ROOM here. With the
        // table saying NO_ROOM instead, this case passes whether the guard
        // exists or not -- verified by mutation, which is how the weaker
        // version of this fixture was caught.
        map.roomForZone = {0};
        map.rooms.resize(1);

        CHECK(roomAt(map, Point3{0.0F, 0.0F, 1.0F}) == NO_ROOM);
        CHECK(roomAt(map, Point3{0.0F, 0.0F, -1.0F}) == NO_ROOM);
    }

    SECTION("a map with no nodes resolves to no room") {
        const RoomMap map;
        CHECK(roomAt(map, Point3{0.0F, 0.0F, 0.0F}) == NO_ROOM);
    }

    SECTION("an empty zone table resolves to no room") {
        // The two maps in SS 4.2's census whose zone table is empty. Every
        // zone is then at or past roomForZone.size(), which is what makes an
        // empty table safe rather than an out-of-range read.
        RoomMap map;
        RoomMap::Node node;
        node.normal = Point3{0.0F, 0.0F, 1.0F};
        node.iZone[1] = 3;
        map.nodes.push_back(node);
        // roomForZone deliberately left empty.

        CHECK(roomAt(map, Point3{0.0F, 0.0F, 1.0F}) == NO_ROOM);
    }

    SECTION("a refused zone resolves to no room") {
        // ZONE_REFUSED is 0xFF, and the engine caps a level at 64 zones, so
        // for any conforming zone table the RANGE check already refuses it
        // and this guard is unreachable. The table here is sized past it on
        // purpose, so the guard is what answers -- otherwise no test can tell
        // the guard from its absence, and an untestable guard is one nobody
        // can tell is still working.
        RoomMap map = twoSidedMap();
        map.leafZone[0] = ZONE_REFUSED;
        map.roomForZone.assign(256U, NO_ROOM);
        map.roomForZone[ZONE_REFUSED] = 0;

        CHECK(roomAt(map, Point3{0.0F, 0.0F, 64.0F}) == NO_ROOM);
    }
}

TEST_CASE("a cyclic node graph terminates", "[umap]") {
    // INV-3, the time half. A Model whose child indices form a cycle is
    // malformed input from a file this project did not write; an unbounded
    // descent hangs the bake rather than refusing it.
    RoomMap map;

    RoomMap::Node first;
    first.normal = Point3{0.0F, 0.0F, 1.0F};
    first.iFront = 1;
    map.nodes.push_back(first);

    RoomMap::Node second;
    second.normal = Point3{0.0F, 0.0F, 1.0F};
    second.iFront = 0;
    map.nodes.push_back(second);

    CHECK(roomAt(map, Point3{0.0F, 0.0F, 1.0F}) == NO_ROOM);
}

TEST_CASE("an out-of-range index resolves to no room", "[umap]") {
    // INV-3, the range half -- and it is a DIFFERENT failure from the cycle
    // above. An iteration counter stops a loop and does nothing about an
    // index outside its table, which reads memory that is not ours.
    // Both sections below carry PADDING nodes, and the padding is the whole
    // point of them. The descent is bounded by nodes.size(), so on a
    // one-node map it exits after the first step and never dereferences the
    // bad index -- the loop bound rejects the fixture before the range check
    // is reached, and the test passes whether the check exists or not. That
    // was the first version of these cases, and removing the range check
    // survived them under AddressSanitizer. With room to take a second step,
    // the unguarded read is a real out-of-bounds access.
    SECTION("a child index outside the node table") {
        RoomMap map;
        RoomMap::Node node;
        node.normal = Point3{0.0F, 0.0F, 1.0F};
        node.iFront = 99;
        map.nodes.push_back(node);
        map.nodes.push_back(node); // padding -- see above
        map.nodes.push_back(node);

        CHECK(roomAt(map, Point3{0.0F, 0.0F, 1.0F}) == NO_ROOM);
    }

    SECTION("a negative child index that is not INDEX_NONE") {
        RoomMap map;
        RoomMap::Node node;
        node.normal = Point3{0.0F, 0.0F, 1.0F};
        node.iFront = -7;
        map.nodes.push_back(node);
        map.nodes.push_back(node); // padding -- see above
        map.nodes.push_back(node);

        CHECK(roomAt(map, Point3{0.0F, 0.0F, 1.0F}) == NO_ROOM);
    }

    SECTION("a leaf index outside the leaf table") {
        RoomMap map = twoSidedMap();
        map.nodes[0].iLeaf[1] = 99;

        CHECK(roomAt(map, Point3{0.0F, 0.0F, 64.0F}) == NO_ROOM);
    }

    SECTION("a zone index outside the zone table") {
        RoomMap map = twoSidedMap();
        map.leafZone[0] = 200;

        CHECK(roomAt(map, Point3{0.0F, 0.0F, 64.0F}) == NO_ROOM);
    }
}
