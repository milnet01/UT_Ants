// UTA-0008's format cases: the header, the golden byte array, the writer and
// the origin field.
//
// docs/specs/UTA-0008-bundle-container-and-origin.md, INV-4, INV-5, INV-6,
// INV-7, INV-8, INV-9 and INV-12. The malformed corpora are INV-1, INV-2,
// INV-3 and INV-11, and they live in tests/unit/BundleMalformedTest.cpp.
//
// THE GOLDEN ARRAY IS AUTHORED HERE FROM SS 4.2 TO SS 4.8, never produced by
// `write`. That is the whole point of it: a swap present in BOTH the reader
// and the writer round-trips perfectly, so a round-trip test cannot break
// INV-6. UTA-0078 was that defect one layer down -- upkg read a BSP node's
// front and back children the wrong way round -- and
// docs/specs/UTA-0069-model-bsp-tables.md SS 4.5 records that a parse-success
// check cannot see two adjacent same-width fields swapped.
//
// The `Bytes` helper below emits primitives; it does NOT know the layout. The
// LAYOUT -- which field follows which -- is stated by the call order in
// goldenBytes(), read off SS 4.6 to SS 4.8, and that is what is independent
// of src/ubundle/'s codecs.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "ubundle/Bundle.h"

#include "Bytes.h"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using uta::ErrorCode;
using uta::ubundle::Bundle;
using uta::ubundle::BundleKind;
using uta::ubundle::combine;
using uta::ubundle::Origin;
using uta::ubundle::read;
using uta::ubundle::readHeader;
using uta::ubundle::write;
using uta::umap::INDEX_NONE;
using uta::umap::NO_ROOM;
using uta::umap::ZONE_REFUSED;

namespace {

using uta::testing::Bytes;

// The fixture's values. Every field the fixture is free to choose holds a
// value distinct from every other's, so that transposing any two adjacent
// fields of the same width and type changes what decodes -- INV-6. The fields
// SS 4.9 pins are NOT free: a run's `from` must equal its node's position, and
// a fixture ignoring that would be refused by `read` rather than decoded.
//
// MULTIPLICITY, and SS 4.9 forces it. Three ROOM nodes and three leafZone
// entries, because with one of each `iFront`, `iBack` and both `iLeaf` slots
// draw from the two-value set {INDEX_NONE, 0} and a transposition is
// undetectable. Two NavNodes and three WiringNodes with two or more edges
// each, because with a single node every edge's `from` and `to` are pinned to
// 0 and the swap is invisible.

/// SS 4.6's ROOM payload, field by field.
Bytes goldenRoomPayload() {
    Bytes out;

    out.u32(1); // rooms: one room

    out.u32(2); // rooms[0].zoneIndex
    out.u32(1); // rooms[0].parts: one footprint

    out.u32(2); // parts[0].outer: two vertices
    out.f32(100.5F);
    out.f32(200.25F);
    out.f32(300.125F);
    out.f32(400.0625F);

    out.u32(1); // parts[0].holes: one ring
    out.u32(3); // holes[0]: three vertices
    out.f32(500.5F);
    out.f32(600.25F);
    out.f32(700.125F);
    out.f32(800.0625F);
    out.f32(900.5F);
    out.f32(1000.25F);

    out.f32(-16.5F); // rooms[0].minZ
    out.f32(20.25F); // rooms[0].maxZ

    out.u32(2); // rooms[0].floors
    out.u16(0);
    out.u16(1);

    out.u32(2); // bands -- ascending, SS 4.9
    out.f32(-64.0F);
    out.f32(128.0F);

    out.u32(3); // roomForZone -- index 0 is the null zone and is NO_ROOM
    out.u32(NO_ROOM);
    out.u32(NO_ROOM);
    out.u32(0);

    out.u32(3); // nodes

    // normal.x, normal.y, normal.z and w are FOUR consecutive f32 -- the run
    // continues out of Point3 into the node's own plane distance.
    out.f32(1.5F);
    out.f32(2.5F);
    out.f32(3.5F);
    out.f32(4.5F);
    // iFront, iBack, iLeaf[0], iLeaf[1]: four consecutive i32, all distinct.
    // iLeaf index 0 is the BACK side and index 1 the front, per the engine's
    // own convention as recorded on RoomMap::Node::iLeaf in src/umap/Rooms.h.
    out.i32(2);
    out.i32(1);
    out.i32(0);
    out.i32(INDEX_NONE);
    out.u8(7);
    out.u8(9);

    out.f32(5.5F);
    out.f32(6.5F);
    out.f32(7.5F);
    out.f32(8.5F);
    out.i32(0);
    out.i32(INDEX_NONE);
    out.i32(1);
    out.i32(2);
    out.u8(11);
    out.u8(13);

    out.f32(9.5F);
    out.f32(10.5F);
    out.f32(11.5F);
    out.f32(12.5F);
    out.i32(INDEX_NONE);
    out.i32(0);
    out.i32(2);
    out.i32(1);
    out.u8(15);
    out.u8(17);

    out.u32(3); // leafZone
    out.u8(2);
    out.u8(0);
    out.u8(ZONE_REFUSED);

    return out;
}

/// SS 4.7's NAVG payload, field by field.
Bytes goldenNavPayload() {
    Bytes out;

    out.u32(2); // nodes

    out.u32(3);     // nodes[0].exportIndex
    out.str("Aa");  // nodes[0].className
    out.u32(0);     // firstEdge and edgeCount: adjacent u32, distinct
    out.u32(2);

    out.u32(17);
    out.str("Bbb");
    out.u32(2);
    out.u32(1);

    out.u32(3); // edges

    // from and to are adjacent u32; distance, collisionRadius,
    // collisionHeight and reachFlags are FOUR consecutive i32 -- the longest
    // same-type run in this format.
    out.u32(0);
    out.u32(1);
    out.i32(11);
    out.i32(22);
    out.i32(33);
    out.i32(44);
    out.u8(5);

    out.u32(0);
    out.u32(1);
    out.i32(55);
    out.i32(66);
    out.i32(77);
    out.i32(88);
    out.u8(6);

    out.u32(1);
    out.u32(0);
    out.i32(99);
    out.i32(110);
    out.i32(121);
    out.i32(132);
    out.u8(7);

    out.u32(9); // discardedEndpoints

    return out;
}

/// SS 4.8's WIRG payload, field by field.
///
/// Three nodes rather than two, and five edges. With two nodes SS 4.9 pins
/// firstOutgoing/outgoingCount/firstIncoming/incomingCount so tightly that no
/// assignment makes all four distinct, and a fixture that repeated one of them
/// could not see that pair transposed.
Bytes goldenWiringPayload() {
    Bytes out;

    out.u32(3); // nodes

    out.u32(5);   // nodes[0].exportIndex
    out.str("t"); // nodes[0].tag
    out.u32(0);   // firstOutgoing, outgoingCount, firstIncoming, incomingCount
    out.u32(1);   // -- four consecutive u32, all distinct
    out.u32(3);
    out.u32(2);

    out.u32(13);
    out.str("uu");
    out.u32(1);
    out.u32(2);
    out.u32(0);
    out.u32(3);

    out.u32(29);
    out.str("vvv");
    out.u32(3);
    out.u32(2);
    out.u32(5); // an empty run at the end of `incoming`, which SS 4.9 allows
    out.u32(0);

    out.u32(5); // edges, grouped by `from`

    out.u32(0); // node 0's run: [0, 1)
    out.u32(1);
    out.str("a");

    out.u32(1); // node 1's run: [1, 3)
    out.u32(0);
    out.str("bb");

    out.u32(1);
    out.u32(0);
    out.str("ccc");

    out.u32(2); // node 2's run: [3, 5)
    out.u32(1);
    out.str("dddd");

    out.u32(2);
    out.u32(1);
    out.str("eeeee");

    out.u32(5); // incoming: the same five edges, grouped by `to`

    out.u32(0); // node 1's incoming run: [0, 3) -- every edge has to == 1
    out.u32(1);
    out.str("a");

    out.u32(2);
    out.u32(1);
    out.str("dddd");

    out.u32(2);
    out.u32(1);
    out.str("eeeee");

    out.u32(1); // node 0's incoming run: [3, 5) -- every edge has to == 0
    out.u32(0);
    out.str("bb");

    out.u32(1);
    out.u32(0);
    out.str("ccc");

    out.u32(1); // dangling
    out.u32(2);
    out.str("zz");

    return out;
}

/// The whole file: SS 4.3's header, SS 4.4's table, then the payloads.
std::vector<std::byte> goldenBytes() {
    const Bytes room = goldenRoomPayload();
    const Bytes nav = goldenNavPayload();
    const Bytes wiring = goldenWiringPayload();

    Bytes out;
    out.id("UTAB");
    out.u32(3); // formatVersion -- 3 since UTA-0011 added MATS
    out.u8(1);  // origin: Authored
    out.u8(0);  // kind: Map
    out.u16(0); // reserved
    out.u32(3); // sectionCount

    // The table begins at offset 16 and the first section at the end of the
    // table. There is no table-offset field -- SS 4.3.
    std::uint64_t offset = 16 + 3 * 24;
    const auto descriptor = [&out, &offset](std::string_view what, const Bytes& payload) {
        out.id(what);
        out.u64(offset);
        out.u64(payload.size());
        out.u8(0); // compression: none
        out.u8(0); // reserved
        out.u8(0);
        out.u8(0);
        offset += payload.size();
    };
    descriptor("ROOM", room);
    descriptor("NAVG", nav);
    descriptor("WIRG", wiring);

    out.append(room);
    out.append(nav);
    out.append(wiring);
    return out.data();
}

/// The structure goldenBytes() encodes, built from the same literals.
Bundle goldenBundle() {
    Bundle bundle;
    bundle.header.origin = Origin::Authored;
    bundle.header.kind = BundleKind::Map;

    uta::umap::RoomMap map;

    uta::umap::Footprint footprint;
    footprint.outer = {{100.5F, 200.25F}, {300.125F, 400.0625F}};
    footprint.holes.push_back(
        std::vector<uta::umap::Point2>{{500.5F, 600.25F}, {700.125F, 800.0625F}, {900.5F, 1000.25F}});

    uta::umap::Room room;
    room.zoneIndex = 2;
    room.parts = {footprint};
    room.minZ = -16.5F;
    room.maxZ = 20.25F;
    room.floors = {0, 1};
    map.rooms = {room};

    map.bands = {-64.0F, 128.0F};
    map.roomForZone = {NO_ROOM, NO_ROOM, 0};
    map.nodes = {
        {{1.5F, 2.5F, 3.5F}, 4.5F, 2, 1, {0, INDEX_NONE}, {7, 9}},
        {{5.5F, 6.5F, 7.5F}, 8.5F, 0, INDEX_NONE, {1, 2}, {11, 13}},
        {{9.5F, 10.5F, 11.5F}, 12.5F, INDEX_NONE, 0, {2, 1}, {15, 17}},
    };
    map.leafZone = {2, 0, ZONE_REFUSED};
    bundle.rooms = map;

    uta::unav::NavGraph nav;
    nav.nodes = {{3, "Aa", 0, 2}, {17, "Bbb", 2, 1}};
    nav.edges = {
        {0, 1, 11, 22, 33, 44, 5},
        {0, 1, 55, 66, 77, 88, 6},
        {1, 0, 99, 110, 121, 132, 7},
    };
    nav.discardedEndpoints = 9;
    bundle.nav = nav;

    uta::unav::WiringGraph wiring;
    wiring.nodes = {{5, "t", 0, 1, 3, 2}, {13, "uu", 1, 2, 0, 3}, {29, "vvv", 3, 2, 5, 0}};
    wiring.edges = {{0, 1, "a"}, {1, 0, "bb"}, {1, 0, "ccc"}, {2, 1, "dddd"}, {2, 1, "eeeee"}};
    wiring.incoming = {{0, 1, "a"}, {2, 1, "dddd"}, {2, 1, "eeeee"}, {1, 0, "bb"}, {1, 0, "ccc"}};
    wiring.dangling = {{2, "zz"}};
    bundle.wiring = wiring;

    return bundle;
}

/// A `Bundle` equal to goldenBundle() built by a DIFFERENT path -- INV-8.
/// Encoding one object twice reads the same padding both times, so a
/// memcpy'ing writer emits identical output on both calls and the case never
/// goes red under the very defect INV-8 names. Two objects built differently
/// may differ in their padding, which is what makes that defect observable.
Bundle goldenBundleBuiltDifferently() {
    Bundle bundle;
    bundle.header.origin = Origin::Authored;

    uta::umap::RoomMap map;
    map.rooms.resize(1);
    map.rooms[0].zoneIndex = 2;
    map.rooms[0].parts.resize(1);
    map.rooms[0].parts[0].outer.resize(2);
    map.rooms[0].parts[0].outer[0].x = 100.5F;
    map.rooms[0].parts[0].outer[0].y = 200.25F;
    map.rooms[0].parts[0].outer[1].x = 300.125F;
    map.rooms[0].parts[0].outer[1].y = 400.0625F;
    map.rooms[0].parts[0].holes.resize(1);
    map.rooms[0].parts[0].holes[0].resize(3);
    map.rooms[0].parts[0].holes[0][0] = {500.5F, 600.25F};
    map.rooms[0].parts[0].holes[0][1] = {700.125F, 800.0625F};
    map.rooms[0].parts[0].holes[0][2] = {900.5F, 1000.25F};
    map.rooms[0].minZ = -16.5F;
    map.rooms[0].maxZ = 20.25F;
    map.rooms[0].floors.push_back(0);
    map.rooms[0].floors.push_back(1);

    map.bands.push_back(-64.0F);
    map.bands.push_back(128.0F);
    map.roomForZone.push_back(NO_ROOM);
    map.roomForZone.push_back(NO_ROOM);
    map.roomForZone.push_back(0);

    map.nodes.resize(3);
    const float normals[3][4] = {
        {1.5F, 2.5F, 3.5F, 4.5F}, {5.5F, 6.5F, 7.5F, 8.5F}, {9.5F, 10.5F, 11.5F, 12.5F}};
    const std::int32_t links[3][4] = {
        {2, 1, 0, INDEX_NONE}, {0, INDEX_NONE, 1, 2}, {INDEX_NONE, 0, 2, 1}};
    const std::uint8_t zones[3][2] = {{7, 9}, {11, 13}, {15, 17}};
    for (std::size_t i = 0; i < 3; ++i) {
        map.nodes[i].normal.x = normals[i][0];
        map.nodes[i].normal.y = normals[i][1];
        map.nodes[i].normal.z = normals[i][2];
        map.nodes[i].w = normals[i][3];
        map.nodes[i].iFront = links[i][0];
        map.nodes[i].iBack = links[i][1];
        map.nodes[i].iLeaf[0] = links[i][2];
        map.nodes[i].iLeaf[1] = links[i][3];
        map.nodes[i].iZone[0] = zones[i][0];
        map.nodes[i].iZone[1] = zones[i][1];
    }
    map.leafZone.push_back(2);
    map.leafZone.push_back(0);
    map.leafZone.push_back(ZONE_REFUSED);
    bundle.rooms = map;

    uta::unav::NavGraph nav;
    nav.nodes.resize(2);
    nav.nodes[0].exportIndex = 3;
    nav.nodes[0].className = "Aa";
    nav.nodes[0].firstEdge = 0;
    nav.nodes[0].edgeCount = 2;
    nav.nodes[1].exportIndex = 17;
    nav.nodes[1].className = "Bbb";
    nav.nodes[1].firstEdge = 2;
    nav.nodes[1].edgeCount = 1;
    nav.edges.resize(3);
    const std::uint32_t navEnds[3][2] = {{0, 1}, {0, 1}, {1, 0}};
    const std::int32_t navValues[3][4] = {{11, 22, 33, 44}, {55, 66, 77, 88}, {99, 110, 121, 132}};
    const std::uint8_t pruned[3] = {5, 6, 7};
    for (std::size_t i = 0; i < 3; ++i) {
        nav.edges[i].from = navEnds[i][0];
        nav.edges[i].to = navEnds[i][1];
        nav.edges[i].distance = navValues[i][0];
        nav.edges[i].collisionRadius = navValues[i][1];
        nav.edges[i].collisionHeight = navValues[i][2];
        nav.edges[i].reachFlags = navValues[i][3];
        nav.edges[i].pruned = pruned[i];
    }
    nav.discardedEndpoints = 9;
    bundle.nav = nav;

    uta::unav::WiringGraph wiring;
    wiring.nodes.resize(3);
    const std::uint32_t wiringNodes[3][5] = {
        {5, 0, 1, 3, 2}, {13, 1, 2, 0, 3}, {29, 3, 2, 5, 0}};
    const char* tags[3] = {"t", "uu", "vvv"};
    for (std::size_t i = 0; i < 3; ++i) {
        wiring.nodes[i].exportIndex = wiringNodes[i][0];
        wiring.nodes[i].tag = tags[i];
        wiring.nodes[i].firstOutgoing = wiringNodes[i][1];
        wiring.nodes[i].outgoingCount = wiringNodes[i][2];
        wiring.nodes[i].firstIncoming = wiringNodes[i][3];
        wiring.nodes[i].incomingCount = wiringNodes[i][4];
    }
    const std::uint32_t edgeEnds[5][2] = {{0, 1}, {1, 0}, {1, 0}, {2, 1}, {2, 1}};
    const char* events[5] = {"a", "bb", "ccc", "dddd", "eeeee"};
    for (std::size_t i = 0; i < 5; ++i) {
        uta::unav::WiringEdge edge;
        edge.from = edgeEnds[i][0];
        edge.to = edgeEnds[i][1];
        edge.event = events[i];
        wiring.edges.push_back(edge);
    }
    wiring.incoming.push_back(wiring.edges[0]);
    wiring.incoming.push_back(wiring.edges[3]);
    wiring.incoming.push_back(wiring.edges[4]);
    wiring.incoming.push_back(wiring.edges[1]);
    wiring.incoming.push_back(wiring.edges[2]);
    wiring.dangling.resize(1);
    wiring.dangling[0].from = 2;
    wiring.dangling[0].event = "zz";
    bundle.wiring = wiring;

    return bundle;
}

/// Golden bytes with one byte replaced -- the header and table mutations the
/// cases below need.
std::vector<std::byte> goldenWithByte(std::size_t offset, std::uint8_t value) {
    std::vector<std::byte> bytes = goldenBytes();
    bytes[offset] = static_cast<std::byte>(value);
    return bytes;
}

[[nodiscard]] std::uint32_t bits(float value) { return std::bit_cast<std::uint32_t>(value); }

} // namespace

TEST_CASE("the header is sixteen little-endian bytes naming the file", "[ubundle]") {
    // SS 4.3. Pinned against raw literals rather than against the writer, so
    // this is where the format's endianness is stated: a big-endian writer
    // would put 0x01 at offset 7 rather than offset 4.
    const std::vector<std::byte> bytes = goldenBytes();
    REQUIRE(bytes.size() > 16);

    const std::uint8_t expected[16] = {
        'U', 'T', 'A', 'B',    // magic -- a hex dump of a bundle names itself
        0x03, 0x00, 0x00, 0x00, // formatVersion = 3 -- UTA-0011 SS 4.10
        0x01,                   // origin = Authored
        0x00,                   // kind = Map
        0x00, 0x00,             // reserved
        0x03, 0x00, 0x00, 0x00, // sectionCount = 3
    };
    for (std::size_t i = 0; i < 16; ++i)
        CHECK(static_cast<std::uint8_t>(bytes[i]) == expected[i]);
}

TEST_CASE("a bad magic and an unsupported version are refused before anything else",
          "[ubundle]") {
    // INV-4. The version is checked for EQUALITY and BEFORE the section table
    // is read: only a lower bound would let a later version's table parse as
    // garbage rather than being refused.
    SECTION("the magic") {
        const std::vector<std::byte> bytes = goldenWithByte(2, 'X');
        const auto result = read(bytes);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::MalformedData);
        CHECK_FALSE(readHeader(bytes).has_value());
    }

    SECTION("a later version") {
        // 4, not 3: 3 is the current version since UTA-0011 added MATS.
        const std::vector<std::byte> bytes = goldenWithByte(4, 4);
        const auto result = read(bytes);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::UnsupportedVersion);
    }

    SECTION("the version before this one") {
        // UTA-0011 SS 14: a stray version-2 file is refused rather than
        // misread, which is what lets that item's cache check bake over it.
        const std::vector<std::byte> bytes = goldenWithByte(4, 2);
        const auto result = read(bytes);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::UnsupportedVersion);
        CHECK_FALSE(readHeader(bytes).has_value());
    }

    SECTION("an earlier version") {
        const std::vector<std::byte> bytes = goldenWithByte(4, 0);
        const auto result = read(bytes);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::UnsupportedVersion);
    }

    SECTION("fewer than sixteen bytes") {
        const std::vector<std::byte> bytes(15, std::byte{0});
        CHECK_FALSE(read(bytes).has_value());
        CHECK_FALSE(readHeader(bytes).has_value());
    }
}

TEST_CASE("an origin byte outside the enum is refused and never defaulted", "[ubundle]") {
    // INV-5. Casting the byte to the enum without a range check makes an
    // unreadable origin indistinguishable from a declared one -- and under
    // SS 4.5 the quarantine guard must be able to fail closed on exactly
    // that difference.
    for (const std::uint8_t bad : {std::uint8_t{2}, std::uint8_t{0xFF}}) {
        const std::vector<std::byte> bytes = goldenWithByte(8, bad);

        const auto whole = read(bytes);
        REQUIRE_FALSE(whole.has_value());
        CHECK(whole.error().code() == ErrorCode::MalformedData);

        const auto header = readHeader(bytes);
        REQUIRE_FALSE(header.has_value());
        CHECK(header.error().code() == ErrorCode::MalformedData);
    }

    // Both defined values survive, so the refusal above is about the range
    // and not about the field being read at all.
    CHECK(readHeader(goldenWithByte(8, 0))->origin == Origin::Derived);
    CHECK(readHeader(goldenWithByte(8, 1))->origin == Origin::Authored);
}

TEST_CASE("readHeader reads the first sixteen bytes and stops", "[ubundle]") {
    // SS 4.5's separate entry point, for UTA-0013's quarantine guard: it must
    // answer one question without decoding a level. It does NOT apply SS 4.4's
    // sectionCount bound -- that rule needs the file's total size, and a
    // readHeader that applied it would refuse every valid bundle while looking
    // like it was working.
    const std::vector<std::byte> whole = goldenBytes();
    const std::vector<std::byte> justTheHeader(whole.begin(), whole.begin() + 16);

    const auto header = readHeader(justTheHeader);
    REQUIRE(header.has_value());
    CHECK(header->formatVersion == 3);
    CHECK(header->origin == Origin::Authored);
    CHECK(header->kind == BundleKind::Map);

    // Sixteen bytes cannot hold three 24-byte descriptors, and readHeader
    // does not care. `read` does.
    CHECK_FALSE(read(justTheHeader).has_value());
}

TEST_CASE("the golden bytes decode field by field to the values they encode", "[ubundle]") {
    // INV-6. Every type SS 4.6 to SS 4.8 encodes appears here, because a
    // fixture covering a subset cannot catch a swap in the types it omits.
    const auto result = read(goldenBytes());
    REQUIRE(result.has_value());
    const Bundle& bundle = *result;

    CHECK(bundle.header.formatVersion == 3);
    CHECK(bundle.header.origin == Origin::Authored);
    CHECK(bundle.header.kind == BundleKind::Map);

    REQUIRE(bundle.rooms.has_value());
    const uta::umap::RoomMap& map = *bundle.rooms;

    REQUIRE(map.rooms.size() == 1);
    CHECK(map.rooms[0].zoneIndex == 2U);
    CHECK(bits(map.rooms[0].minZ) == bits(-16.5F));
    CHECK(bits(map.rooms[0].maxZ) == bits(20.25F));
    REQUIRE(map.rooms[0].floors.size() == 2);
    CHECK(map.rooms[0].floors[0] == 0U);
    CHECK(map.rooms[0].floors[1] == 1U);

    REQUIRE(map.rooms[0].parts.size() == 1);
    const uta::umap::Footprint& footprint = map.rooms[0].parts[0];
    REQUIRE(footprint.outer.size() == 2);
    CHECK(bits(footprint.outer[0].x) == bits(100.5F));
    CHECK(bits(footprint.outer[0].y) == bits(200.25F));
    CHECK(bits(footprint.outer[1].x) == bits(300.125F));
    CHECK(bits(footprint.outer[1].y) == bits(400.0625F));
    REQUIRE(footprint.holes.size() == 1);
    REQUIRE(footprint.holes[0].size() == 3);
    CHECK(bits(footprint.holes[0][0].x) == bits(500.5F));
    CHECK(bits(footprint.holes[0][0].y) == bits(600.25F));
    CHECK(bits(footprint.holes[0][1].x) == bits(700.125F));
    CHECK(bits(footprint.holes[0][1].y) == bits(800.0625F));
    CHECK(bits(footprint.holes[0][2].x) == bits(900.5F));
    CHECK(bits(footprint.holes[0][2].y) == bits(1000.25F));

    REQUIRE(map.bands.size() == 2);
    CHECK(bits(map.bands[0]) == bits(-64.0F));
    CHECK(bits(map.bands[1]) == bits(128.0F));

    REQUIRE(map.roomForZone.size() == 3);
    CHECK(map.roomForZone[0] == NO_ROOM);
    CHECK(map.roomForZone[1] == NO_ROOM);
    CHECK(map.roomForZone[2] == 0U);

    REQUIRE(map.nodes.size() == 3);
    const float normals[3][4] = {
        {1.5F, 2.5F, 3.5F, 4.5F}, {5.5F, 6.5F, 7.5F, 8.5F}, {9.5F, 10.5F, 11.5F, 12.5F}};
    const std::int32_t links[3][4] = {
        {2, 1, 0, INDEX_NONE}, {0, INDEX_NONE, 1, 2}, {INDEX_NONE, 0, 2, 1}};
    const std::uint8_t zones[3][2] = {{7, 9}, {11, 13}, {15, 17}};
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(bits(map.nodes[i].normal.x) == bits(normals[i][0]));
        CHECK(bits(map.nodes[i].normal.y) == bits(normals[i][1]));
        CHECK(bits(map.nodes[i].normal.z) == bits(normals[i][2]));
        CHECK(bits(map.nodes[i].w) == bits(normals[i][3]));
        CHECK(map.nodes[i].iFront == links[i][0]);
        CHECK(map.nodes[i].iBack == links[i][1]);
        CHECK(map.nodes[i].iLeaf[0] == links[i][2]);
        CHECK(map.nodes[i].iLeaf[1] == links[i][3]);
        CHECK(map.nodes[i].iZone[0] == zones[i][0]);
        CHECK(map.nodes[i].iZone[1] == zones[i][1]);
    }

    REQUIRE(map.leafZone.size() == 3);
    CHECK(map.leafZone[0] == 2U);
    CHECK(map.leafZone[1] == 0U);
    CHECK(map.leafZone[2] == ZONE_REFUSED);

    REQUIRE(bundle.nav.has_value());
    const uta::unav::NavGraph& nav = *bundle.nav;
    REQUIRE(nav.nodes.size() == 2);
    CHECK(nav.nodes[0].exportIndex == 3U);
    CHECK(nav.nodes[0].className == "Aa");
    CHECK(nav.nodes[0].firstEdge == 0U);
    CHECK(nav.nodes[0].edgeCount == 2U);
    CHECK(nav.nodes[1].exportIndex == 17U);
    CHECK(nav.nodes[1].className == "Bbb");
    CHECK(nav.nodes[1].firstEdge == 2U);
    CHECK(nav.nodes[1].edgeCount == 1U);

    REQUIRE(nav.edges.size() == 3);
    const std::uint32_t navEnds[3][2] = {{0, 1}, {0, 1}, {1, 0}};
    const std::int32_t navValues[3][4] = {{11, 22, 33, 44}, {55, 66, 77, 88}, {99, 110, 121, 132}};
    const std::uint8_t pruned[3] = {5, 6, 7};
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(nav.edges[i].from == navEnds[i][0]);
        CHECK(nav.edges[i].to == navEnds[i][1]);
        CHECK(nav.edges[i].distance == navValues[i][0]);
        CHECK(nav.edges[i].collisionRadius == navValues[i][1]);
        CHECK(nav.edges[i].collisionHeight == navValues[i][2]);
        CHECK(nav.edges[i].reachFlags == navValues[i][3]);
        CHECK(nav.edges[i].pruned == pruned[i]);
    }
    CHECK(nav.discardedEndpoints == 9U);

    REQUIRE(bundle.wiring.has_value());
    const uta::unav::WiringGraph& wiring = *bundle.wiring;
    REQUIRE(wiring.nodes.size() == 3);
    const std::uint32_t wiringNodes[3][5] = {
        {5, 0, 1, 3, 2}, {13, 1, 2, 0, 3}, {29, 3, 2, 5, 0}};
    const char* tags[3] = {"t", "uu", "vvv"};
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(wiring.nodes[i].exportIndex == wiringNodes[i][0]);
        CHECK(wiring.nodes[i].tag == tags[i]);
        CHECK(wiring.nodes[i].firstOutgoing == wiringNodes[i][1]);
        CHECK(wiring.nodes[i].outgoingCount == wiringNodes[i][2]);
        CHECK(wiring.nodes[i].firstIncoming == wiringNodes[i][3]);
        CHECK(wiring.nodes[i].incomingCount == wiringNodes[i][4]);
    }

    REQUIRE(wiring.edges.size() == 5);
    const std::uint32_t edgeEnds[5][2] = {{0, 1}, {1, 0}, {1, 0}, {2, 1}, {2, 1}};
    const char* events[5] = {"a", "bb", "ccc", "dddd", "eeeee"};
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK(wiring.edges[i].from == edgeEnds[i][0]);
        CHECK(wiring.edges[i].to == edgeEnds[i][1]);
        CHECK(wiring.edges[i].event == events[i]);
    }

    REQUIRE(wiring.incoming.size() == 5);
    const std::uint32_t incomingEnds[5][2] = {{0, 1}, {2, 1}, {2, 1}, {1, 0}, {1, 0}};
    const char* incomingEvents[5] = {"a", "dddd", "eeeee", "bb", "ccc"};
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK(wiring.incoming[i].from == incomingEnds[i][0]);
        CHECK(wiring.incoming[i].to == incomingEnds[i][1]);
        CHECK(wiring.incoming[i].event == incomingEvents[i]);
    }

    REQUIRE(wiring.dangling.size() == 1);
    CHECK(wiring.dangling[0].from == 2U);
    CHECK(wiring.dangling[0].event == "zz");
}

TEST_CASE("write reproduces the golden bytes exactly", "[ubundle]") {
    // INV-7, and the cross-compiler check: the golden array is a literal
    // fixed in this source, so the matrix runs one comparison against one
    // constant on GCC, Clang and MSVC alike and any leg whose bytes differ
    // goes red on its own.
    //
    // INV-6 grades the READER against these bytes and this grades the WRITER
    // against them, so neither can be satisfied by a compensating error in
    // the other.
    const auto written = write(goldenBundle());
    REQUIRE(written.has_value());
    CHECK(*written == goldenBytes());
}

TEST_CASE("write is deterministic across two independently built values", "[ubundle]") {
    // INV-8. Two separately built objects, NOT one encoded twice: encoding
    // one object twice reads the same padding both times, so a memcpy'ing
    // writer emits identical output on both calls and this case would never
    // go red under the defect it names.
    const auto first = write(goldenBundle());
    const auto second = write(goldenBundleBuiltDifferently());
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(*first == *second);
}

TEST_CASE("every float survives the round trip bit for bit", "[ubundle]") {
    // INV-9. The NaN and infinity cases use fields SS 4.9 does not constrain
    // -- a Footprint's Point2 coordinates and a Node's normal and w. They may
    // NOT use `bands`, which SS 4.9 requires to be ascending: a NaN there is
    // refused by that rule rather than round-tripped, so a fixture built on
    // bands would test the validator and report on the codec.
    const float negativeZero = -0.0F;
    const float positiveInfinity = std::numeric_limits<float>::infinity();
    const float negativeInfinity = -std::numeric_limits<float>::infinity();
    const float notANumber = std::numeric_limits<float>::quiet_NaN();
    const float subnormal = std::numeric_limits<float>::denorm_min();

    uta::umap::RoomMap map;
    uta::umap::Room room;
    room.zoneIndex = 1;
    room.parts.resize(1);
    room.parts[0].outer = {{negativeZero, positiveInfinity}, {negativeInfinity, notANumber}};
    room.minZ = -1.0F;
    room.maxZ = 1.0F;
    room.floors = {0};
    map.rooms = {room};
    map.bands = {0.0F};
    map.roomForZone = {NO_ROOM, 0};
    map.nodes.resize(1);
    map.nodes[0].normal = {subnormal, negativeZero, positiveInfinity};
    map.nodes[0].w = notANumber;
    map.nodes[0].iLeaf[0] = INDEX_NONE;
    map.nodes[0].iLeaf[1] = INDEX_NONE;

    Bundle bundle;
    bundle.rooms = map;

    const auto written = write(bundle);
    REQUIRE(written.has_value());
    const auto reread = read(*written);
    REQUIRE(reread.has_value());
    REQUIRE(reread->rooms.has_value());

    // Compared by BIT PATTERN, never with ==. Under == a -0.0 reads as
    // preserved when it has been replaced by +0.0, and a NaN compares unequal
    // to itself -- docs/specs/UTA-0049-numeric-contract.md's INV-5.
    const uta::umap::Footprint& out = reread->rooms->rooms[0].parts[0];
    CHECK(bits(out.outer[0].x) == bits(negativeZero));
    CHECK(bits(out.outer[0].y) == bits(positiveInfinity));
    CHECK(bits(out.outer[1].x) == bits(negativeInfinity));
    CHECK(bits(out.outer[1].y) == bits(notANumber));

    const uta::umap::RoomMap::Node& node = reread->rooms->nodes[0];
    CHECK(bits(node.normal.x) == bits(subnormal));
    CHECK(bits(node.normal.y) == bits(negativeZero));
    CHECK(bits(node.normal.z) == bits(positiveInfinity));
    CHECK(bits(node.w) == bits(notANumber));
}

TEST_CASE("combine returns the most restrictive origin", "[ubundle]") {
    // INV-12. A maximum or a bitwise OR over the numeric values gives
    // Authored for (Authored, Derived) -- the publishable value, from an
    // input that touched somebody's UT install. That is the inversion
    // SS 3 decision 4 chose Derived = 0 to make loud.
    //
    // All THREE mixed pairs are asserted: a fixture asserting only the two
    // matching pairs passes under a max and under an OR alike.
    CHECK(combine(Origin::Authored, Origin::Authored) == Origin::Authored);
    CHECK(combine(Origin::Authored, Origin::Derived) == Origin::Derived);
    CHECK(combine(Origin::Derived, Origin::Authored) == Origin::Derived);
    CHECK(combine(Origin::Derived, Origin::Derived) == Origin::Derived);
}

TEST_CASE("a defined field carrying an undefined value is refused", "[ubundle]") {
    // INV-12's second half. Each of these is accepted-with-a-default by a
    // reader that casts rather than checks, and a reserved field nobody
    // refuses is not a contract.
    SECTION("an unknown kind") {
        // An undefined kind names sections this version cannot know -- SS 6.
        const auto result = read(goldenWithByte(9, 2));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::MalformedData);
        CHECK_FALSE(readHeader(goldenWithByte(9, 2)).has_value());
    }

    SECTION("a non-zero reserved field in the header") {
        const auto result = read(goldenWithByte(10, 1));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::MalformedData);
    }

    SECTION("a non-zero reserved byte in a section descriptor") {
        // The first descriptor starts at 16; its reserved run is at 21.
        const auto result = read(goldenWithByte(16 + 21, 1));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::MalformedData);
    }

    SECTION("a non-zero compression byte") {
        // The byte is DEFINED and its value is not, so this is a version
        // refusal rather than corruption -- SS 6.
        const auto result = read(goldenWithByte(16 + 20, 1));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::UnsupportedVersion);
    }
}

TEST_CASE("a bundle with no sections is sixteen bytes and reads back empty", "[ubundle]") {
    // SS 4.4: sections are individually optional, and absence is distinct
    // from a present-but-empty section. With no sections the table is empty
    // and the header is the whole file.
    Bundle empty;
    const auto written = write(empty);
    REQUIRE(written.has_value());
    CHECK(written->size() == 16);

    const auto reread = read(*written);
    REQUIRE(reread.has_value());
    CHECK_FALSE(reread->rooms.has_value());
    CHECK_FALSE(reread->nav.has_value());
    CHECK_FALSE(reread->wiring.has_value());
    CHECK(reread->header.origin == Origin::Derived);
}

TEST_CASE("a present but empty section is not an absent one", "[ubundle]") {
    // SS 4.4. An empty NavGraph says the level was examined and had no
    // navigation points; an absent NAVG says nothing at all.
    Bundle bundle;
    bundle.nav = uta::unav::NavGraph{};

    const auto written = write(bundle);
    REQUIRE(written.has_value());
    const auto reread = read(*written);
    REQUIRE(reread.has_value());
    REQUIRE(reread->nav.has_value());
    CHECK(reread->nav->nodes.empty());
    CHECK_FALSE(reread->rooms.has_value());
    CHECK_FALSE(reread->wiring.has_value());
}

TEST_CASE("write refuses a bundle whose structure it would have to lie about", "[ubundle]") {
    // SS 6's last row. Refused rather than written, so a bad bundle cannot be
    // produced here and then blamed on the reader.
    Bundle bundle = goldenBundle();
    bundle.nav->nodes[0].edgeCount = 99; // a run past the end of `edges`

    const auto written = write(bundle);
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code() == ErrorCode::InvalidArgument);
}
