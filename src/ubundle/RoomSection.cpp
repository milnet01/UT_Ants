// The ROOM section: umap's RoomMap --
// docs/specs/UTA-0008-bundle-container-and-origin.md SS 4.6 and SS 4.9.

#include "Sections.h"

namespace uta::ubundle::detail {
namespace {

constexpr std::uint64_t MIN_POINT2 = 8;
constexpr std::uint64_t MIN_FOOTPRINT = 8;
constexpr std::uint64_t MIN_ROOM = 20;
constexpr std::uint64_t MIN_NODE = 34;

[[nodiscard]] Result<umap::Point2> readPoint2(Cursor& cursor) {
    umap::Point2 point;
    UTA_TRY(point.x, cursor.readF32());
    UTA_TRY(point.y, cursor.readF32());
    return point;
}

[[nodiscard]] Result<std::vector<umap::Point2>> readRing(Cursor& cursor) {
    return readVector<umap::Point2>(cursor, MIN_POINT2, "ring vertices", readPoint2);
}

[[nodiscard]] Result<umap::Footprint> readFootprint(Cursor& cursor) {
    umap::Footprint footprint;
    UTA_TRY(footprint.outer, readRing(cursor));
    UTA_TRY(footprint.holes,
            readVector<std::vector<umap::Point2>>(cursor, MIN_VECTOR, "holes", readRing));
    return footprint;
}

[[nodiscard]] Result<umap::Room> readRoom(Cursor& cursor) {
    umap::Room room;
    UTA_TRY(room.zoneIndex, cursor.readU32());
    UTA_TRY(room.parts,
            readVector<umap::Footprint>(cursor, MIN_FOOTPRINT, "footprints", readFootprint));
    UTA_TRY(room.minZ, cursor.readF32());
    UTA_TRY(room.maxZ, cursor.readF32());
    UTA_TRY(room.floors, readVector<std::uint16_t>(cursor, MIN_U16, "floors", readU16Element));
    return room;
}

/// SS 4.6. `iFront` precedes `iBack`; index 0 of `iLeaf` and `iZone` is the
/// BACK side and index 1 the front, per the engine's own convention as
/// recorded on RoomMap::Node::iLeaf in src/umap/Rooms.h. UTA-0078 was this
/// defect one layer down, and a parse-success check cannot see two adjacent
/// same-width fields swapped -- INV-6 and INV-7 are what can.
[[nodiscard]] Result<umap::RoomMap::Node> readNode(Cursor& cursor) {
    umap::RoomMap::Node node;
    UTA_TRY(node.normal.x, cursor.readF32());
    UTA_TRY(node.normal.y, cursor.readF32());
    UTA_TRY(node.normal.z, cursor.readF32());
    UTA_TRY(node.w, cursor.readF32());
    UTA_TRY(node.iFront, cursor.readI32());
    UTA_TRY(node.iBack, cursor.readI32());
    UTA_TRY(node.iLeaf[0], cursor.readI32());
    UTA_TRY(node.iLeaf[1], cursor.readI32());
    UTA_TRY(node.iZone[0], cursor.readU8());
    UTA_TRY(node.iZone[1], cursor.readU8());
    return node;
}

/// `value` is a valid index into a table of `size` entries, or INDEX_NONE.
[[nodiscard]] bool indexOrNone(std::int32_t value, std::size_t size) noexcept {
    if (value == umap::INDEX_NONE) return true;
    return value >= 0 && static_cast<std::uint64_t>(value) < static_cast<std::uint64_t>(size);
}

void putPoint2(Sink& sink, const umap::Point2& point) {
    sink.putF32(point.x);
    sink.putF32(point.y);
}

void putRing(Sink& sink, const std::vector<umap::Point2>& ring) {
    sink.putVector(ring, putPoint2);
}

void putFootprint(Sink& sink, const umap::Footprint& footprint) {
    putRing(sink, footprint.outer);
    sink.putVector(footprint.holes, putRing);
}

void putRoom(Sink& sink, const umap::Room& room) {
    sink.putU32(room.zoneIndex);
    sink.putVector(room.parts, putFootprint);
    sink.putF32(room.minZ);
    sink.putF32(room.maxZ);
    sink.putVector(room.floors, [](Sink& out, std::uint16_t value) { out.putU16(value); });
}

void putNode(Sink& sink, const umap::RoomMap::Node& node) {
    sink.putF32(node.normal.x);
    sink.putF32(node.normal.y);
    sink.putF32(node.normal.z);
    sink.putF32(node.w);
    sink.putI32(node.iFront);
    sink.putI32(node.iBack);
    sink.putI32(node.iLeaf[0]);
    sink.putI32(node.iLeaf[1]);
    sink.putU8(node.iZone[0]);
    sink.putU8(node.iZone[1]);
}

} // namespace

Result<umap::RoomMap> readRoomMap(Cursor& cursor) {
    umap::RoomMap map;
    UTA_TRY(map.rooms, readVector<umap::Room>(cursor, MIN_ROOM, "rooms", readRoom));
    UTA_TRY(map.bands, readVector<float>(cursor, MIN_F32, "bands", readF32Element));
    UTA_TRY(map.roomForZone,
            readVector<std::uint32_t>(cursor, MIN_U32, "roomForZone entries", readU32Element));
    UTA_TRY(map.nodes, readVector<umap::RoomMap::Node>(cursor, MIN_NODE, "nodes", readNode));
    UTA_TRY(map.leafZone,
            readVector<std::uint8_t>(cursor, MIN_U8, "leafZone entries", readU8Element));
    return map;
}

Result<void> validateRoomMap(const umap::RoomMap& map, ErrorCode code) {
    const std::size_t roomCount = map.rooms.size();
    const std::size_t zoneCount = map.roomForZone.size();

    // Empty is a real state and not a defect: UTA-0007 SS 6 records two maps
    // in the reference install whose zone table is empty, and the result is a
    // valid RoomMap with no rooms. Where it is non-empty, index 0 is the
    // engine's null zone and is always NO_ROOM.
    if (!map.roomForZone.empty() && map.roomForZone[0] != umap::NO_ROOM)
        return fail(code, "ROOM: roomForZone[0] is not NO_ROOM");

    for (const std::uint32_t room : map.roomForZone)
        if (room != umap::NO_ROOM && room >= roomCount)
            return fail(code, "ROOM: a roomForZone entry names no room");

    // UTA-0007's INV-1: each room names a distinct zone in [1, zones). The
    // agreement check below also FORCES distinctness -- two rooms naming one
    // zone would each have to be the single value roomForZone holds for it --
    // so there is no second O(n^2) pass.
    for (std::size_t position = 0; position < roomCount; ++position) {
        const umap::Room& room = map.rooms[position];
        if (room.zoneIndex == 0) return fail(code, "ROOM: a room names zone 0");
        if (room.zoneIndex >= zoneCount)
            return fail(code, "ROOM: a room's zoneIndex is outside roomForZone");
        if (static_cast<std::uint64_t>(map.roomForZone[room.zoneIndex]) != position)
            return fail(code, "ROOM: roomForZone and rooms disagree about which room owns a zone");
        if (room.floors.empty()) return fail(code, "ROOM: a room's floors is empty");
        for (const std::uint16_t floor : room.floors)
            if (floor >= map.bands.size())
                return fail(code, "ROOM: a room names a floor band that does not exist");
    }

    // Written as "not >= previous" rather than "< previous", so a NaN fails
    // rather than passing: every comparison against a NaN is false. INV-9's
    // NaN case therefore uses fields this rule does not reach.
    for (std::size_t i = 1; i < map.bands.size(); ++i)
        if (!(map.bands[i] >= map.bands[i - 1]))
            return fail(code, "ROOM: bands are not in ascending order");

    for (const std::uint8_t zone : map.leafZone)
        if (zone != umap::ZONE_REFUSED && zone >= zoneCount)
            return fail(code, "ROOM: a leafZone entry is outside roomForZone");

    for (const umap::RoomMap::Node& node : map.nodes) {
        if (!indexOrNone(node.iFront, map.nodes.size())
            || !indexOrNone(node.iBack, map.nodes.size()))
            return fail(code, "ROOM: a node's child index is out of range");
        if (!indexOrNone(node.iLeaf[0], map.leafZone.size())
            || !indexOrNone(node.iLeaf[1], map.leafZone.size()))
            return fail(code, "ROOM: a node's leaf index is out of range");
    }

    // Node::iZone is deliberately NOT checked -- SS 4.9. roomAt reaches it
    // only through roomForZone, which returns NO_ROOM for zone 0, for
    // ZONE_REFUSED and for any zone past the table, so an out-of-range zone
    // byte is already total at the accessor (UTA-0007's INV-3). A refusal
    // here would reject a bundle the runtime handles correctly.
    return {};
}

std::vector<std::byte> encodeRoomMap(const umap::RoomMap& map) {
    Sink sink;
    sink.putVector(map.rooms, putRoom);
    sink.putVector(map.bands, [](Sink& out, float value) { out.putF32(value); });
    sink.putVector(map.roomForZone, [](Sink& out, std::uint32_t value) { out.putU32(value); });
    sink.putVector(map.nodes, putNode);
    sink.putVector(map.leafZone, [](Sink& out, std::uint8_t value) { out.putU8(value); });
    return std::move(sink).take();
}

} // namespace uta::ubundle::detail
