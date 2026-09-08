#include "ubundle/Bundle.h"

#include <array>
#include <bit>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace uta::ubundle {
namespace {

// Section ids -- SS 4.6 to SS 4.8. Byte sequences and not integers, so there
// is no endianness to get wrong and a hex dump reads them left to right.
using SectionId = std::array<std::uint8_t, 4>;

constexpr SectionId ID_ROOM = {'R', 'O', 'O', 'M'};
constexpr SectionId ID_NAVG = {'N', 'A', 'V', 'G'};
constexpr SectionId ID_WIRG = {'W', 'I', 'R', 'G'};

constexpr SectionId MAGIC = {'U', 'T', 'A', 'B'};

// The minimum number of bytes one element of each type can occupy -- SS 4.2's
// table, derived from the layouts in SS 4.6 to SS 4.8. These size the DIVISION
// in readVector; every one of them must be non-zero, which is what makes that
// division safe.
constexpr std::uint64_t MIN_U8 = 1;
constexpr std::uint64_t MIN_U16 = 2;
constexpr std::uint64_t MIN_U32 = 4;
constexpr std::uint64_t MIN_F32 = 4;
constexpr std::uint64_t MIN_POINT2 = 8;
constexpr std::uint64_t MIN_FOOTPRINT = 8;
constexpr std::uint64_t MIN_ROOM = 20;
constexpr std::uint64_t MIN_NODE = 34;
constexpr std::uint64_t MIN_NAV_NODE = 16;
constexpr std::uint64_t MIN_NAV_EDGE = 25;
constexpr std::uint64_t MIN_WIRING_NODE = 24;
constexpr std::uint64_t MIN_WIRING_EDGE = 12;
constexpr std::uint64_t MIN_DANGLING = 8;

/// A vector's own framing: the u32 count alone, with no elements. This is the
/// minimum for a NESTED vector -- a Footprint's holes are a vector of vectors,
/// and an empty inner one occupies four bytes.
constexpr std::uint64_t MIN_VECTOR = 4;

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

/// A cursor bounded by ONE section's span -- SS 4.2 rule 2. Every read is
/// bounded by the span it was constructed with and never by the file, so a
/// section claiming more bytes than it was given fails inside its own span
/// rather than reading a neighbour's.
///
/// Multi-byte values are assembled byte by byte rather than by casting a
/// pointer: the format permits an unaligned offset everywhere, and nothing is
/// memcpy'd from or into a struct (SS 4.2 rule 3).
class Cursor {
public:
    explicit Cursor(std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - position_;
    }

    [[nodiscard]] Result<std::uint8_t> readU8() {
        if (remaining() < 1) return shortRead("u8");
        return static_cast<std::uint8_t>(bytes_[position_++]);
    }

    [[nodiscard]] Result<std::uint16_t> readU16() {
        UTA_TRY(const std::uint64_t value, readLittleEndian(2));
        return static_cast<std::uint16_t>(value);
    }

    [[nodiscard]] Result<std::uint32_t> readU32() {
        UTA_TRY(const std::uint64_t value, readLittleEndian(4));
        return static_cast<std::uint32_t>(value);
    }

    [[nodiscard]] Result<std::uint64_t> readU64() { return readLittleEndian(8); }

    [[nodiscard]] Result<std::int32_t> readI32() {
        UTA_TRY(const std::uint32_t value, readU32());
        return static_cast<std::int32_t>(value);
    }

    /// IEEE-754 binary32 moved through its bit pattern -- SS 4.2. Never
    /// through a wider type and never compared with ==, so -0.0, both
    /// infinities, a quiet NaN and a subnormal all survive (INV-9).
    [[nodiscard]] Result<float> readF32() {
        UTA_TRY(const std::uint32_t bits, readU32());
        return std::bit_cast<float>(bits);
    }

    [[nodiscard]] Result<std::span<const std::byte>> readBytes(std::size_t count) {
        if (remaining() < count) return shortRead("byte run");
        const std::span<const std::byte> out = bytes_.subspan(position_, count);
        position_ += count;
        return out;
    }

private:
    [[nodiscard]] static std::unexpected<Error> shortRead(const char* what) {
        return fail(ErrorCode::MalformedData,
                    std::string("section ran out of bytes reading a ") + what);
    }

    [[nodiscard]] Result<std::uint64_t> readLittleEndian(std::size_t count) {
        if (remaining() < count) return shortRead("multi-byte value");
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < count; ++i)
            value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(bytes_[position_ + i]))
                     << (8U * i);
        position_ += count;
        return value;
    }

    std::span<const std::byte> bytes_;
    std::size_t position_ = 0;
};

/// A counted run of elements -- SS 4.2's `vector<T>`.
///
/// SS 4.2 rule 1, and the whole of this reader's allocation safety: the count
/// is checked against the bytes remaining in its own section BEFORE it sizes
/// anything. The check is a DIVISION, never `count * minElement` -- that
/// product is a u64 here, but the rule exists so no later edit reintroduces
/// the wrap, and INV-2's second case grades exactly it.
template <class T, class Decode>
[[nodiscard]] Result<std::vector<T>> readVector(Cursor& cursor,
                                                std::uint64_t minElement,
                                                const char* what,
                                                Decode decode) {
    UTA_TRY(const std::uint32_t count, cursor.readU32());
    if (count > cursor.remaining() / minElement)
        return fail(ErrorCode::MalformedData,
                    std::string("a count of ") + std::to_string(count) + " " + what
                        + " exceeds the bytes remaining in its section");

    std::vector<T> out;
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        UTA_TRY(T value, decode(cursor));
        out.push_back(std::move(value));
    }
    return out;
}

/// A u32 byte length, then exactly that many bytes; no terminator.
///
/// The bytes are OPAQUE and are round-tripped verbatim -- SS 4.2. They are
/// assumed UTF-8 and are not validated: the three strings this format carries
/// come out of a UE1 name table, and that file format guarantees no encoding,
/// so a validating reader would refuse bundles for maps that exist.
[[nodiscard]] Result<std::string> readString(Cursor& cursor) {
    UTA_TRY(const std::uint32_t length, cursor.readU32());
    if (length == 0) return std::string{};
    // The bound is readBytes', and only readBytes'. A second length check here
    // would be a second implementation of one rule -- src/upkg/ByteReader.h
    // records what that costs, its sibling engine's loader having collected
    // bounds-check fixes one call site at a time. Nothing is sized from
    // `length`; the string is sized from the span readBytes returned.
    UTA_TRY(const std::span<const std::byte> raw, cursor.readBytes(length));
    std::string out(raw.size(), '\0');
    std::memcpy(out.data(), raw.data(), raw.size());
    return out;
}

[[nodiscard]] Result<std::uint8_t> readU8Element(Cursor& cursor) { return cursor.readU8(); }
[[nodiscard]] Result<std::uint16_t> readU16Element(Cursor& cursor) { return cursor.readU16(); }
[[nodiscard]] Result<std::uint32_t> readU32Element(Cursor& cursor) { return cursor.readU32(); }
[[nodiscard]] Result<float> readF32Element(Cursor& cursor) { return cursor.readF32(); }

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

[[nodiscard]] Result<umap::RoomMap> readRoomMap(Cursor& cursor) {
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

[[nodiscard]] Result<unav::NavNode> readNavNode(Cursor& cursor) {
    unav::NavNode node;
    UTA_TRY(node.exportIndex, cursor.readU32());
    UTA_TRY(node.className, readString(cursor));
    UTA_TRY(node.firstEdge, cursor.readU32());
    UTA_TRY(node.edgeCount, cursor.readU32());
    return node;
}

/// SS 4.7. `distance`, `collisionRadius`, `collisionHeight` and `reachFlags`
/// are four consecutive i32 -- the longest same-type run in this format and
/// the one where a transposition is least visible.
[[nodiscard]] Result<unav::NavEdge> readNavEdge(Cursor& cursor) {
    unav::NavEdge edge;
    UTA_TRY(edge.from, cursor.readU32());
    UTA_TRY(edge.to, cursor.readU32());
    UTA_TRY(edge.distance, cursor.readI32());
    UTA_TRY(edge.collisionRadius, cursor.readI32());
    UTA_TRY(edge.collisionHeight, cursor.readI32());
    UTA_TRY(edge.reachFlags, cursor.readI32());
    UTA_TRY(edge.pruned, cursor.readU8());
    return edge;
}

[[nodiscard]] Result<unav::NavGraph> readNavGraph(Cursor& cursor) {
    unav::NavGraph graph;
    UTA_TRY(graph.nodes, readVector<unav::NavNode>(cursor, MIN_NAV_NODE, "nav nodes", readNavNode));
    UTA_TRY(graph.edges, readVector<unav::NavEdge>(cursor, MIN_NAV_EDGE, "nav edges", readNavEdge));
    UTA_TRY(graph.discardedEndpoints, cursor.readU32());
    return graph;
}

[[nodiscard]] Result<unav::WiringNode> readWiringNode(Cursor& cursor) {
    unav::WiringNode node;
    UTA_TRY(node.exportIndex, cursor.readU32());
    UTA_TRY(node.tag, readString(cursor));
    UTA_TRY(node.firstOutgoing, cursor.readU32());
    UTA_TRY(node.outgoingCount, cursor.readU32());
    UTA_TRY(node.firstIncoming, cursor.readU32());
    UTA_TRY(node.incomingCount, cursor.readU32());
    return node;
}

[[nodiscard]] Result<unav::WiringEdge> readWiringEdge(Cursor& cursor) {
    unav::WiringEdge edge;
    UTA_TRY(edge.from, cursor.readU32());
    UTA_TRY(edge.to, cursor.readU32());
    UTA_TRY(edge.event, readString(cursor));
    return edge;
}

[[nodiscard]] Result<unav::DanglingEvent> readDangling(Cursor& cursor) {
    unav::DanglingEvent dangling;
    UTA_TRY(dangling.from, cursor.readU32());
    UTA_TRY(dangling.event, readString(cursor));
    return dangling;
}

[[nodiscard]] Result<unav::WiringGraph> readWiringGraph(Cursor& cursor) {
    unav::WiringGraph graph;
    UTA_TRY(graph.nodes,
            readVector<unav::WiringNode>(cursor, MIN_WIRING_NODE, "wiring nodes", readWiringNode));
    UTA_TRY(graph.edges,
            readVector<unav::WiringEdge>(cursor, MIN_WIRING_EDGE, "wiring edges", readWiringEdge));
    UTA_TRY(graph.incoming,
            readVector<unav::WiringEdge>(cursor, MIN_WIRING_EDGE, "incoming edges", readWiringEdge));
    UTA_TRY(graph.dangling,
            readVector<unav::DanglingEvent>(cursor, MIN_DANGLING, "dangling events", readDangling));
    return graph;
}

// ---------------------------------------------------------------------------
// Structural validation -- SS 4.9
// ---------------------------------------------------------------------------
//
// Every check runs before `read` returns, and `write` runs the same checks
// over the structure it was handed. Validating in the accessors instead is
// what UTA-0003's INV-6 records the cost of: uta::unav::edgesFrom builds a
// std::span from firstEdge and edgeCount with no guard of its own, so a run
// reaching past `edges` is undefined behaviour in the CONSUMER and not a
// wrong answer this library can be blamed for later.

/// `value` is a valid index into a table of `size` entries, or INDEX_NONE.
[[nodiscard]] bool indexOrNone(std::int32_t value, std::size_t size) noexcept {
    if (value == umap::INDEX_NONE) return true;
    return value >= 0 && static_cast<std::uint64_t>(value) < static_cast<std::uint64_t>(size);
}

/// `first + count <= size`, computed so it cannot overflow.
[[nodiscard]] bool runWithin(std::uint32_t first, std::uint32_t count, std::size_t size) noexcept {
    if (first > size) return false;
    return count <= size - first;
}

[[nodiscard]] Result<void> validateRoomMap(const umap::RoomMap& map, ErrorCode code) {
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

[[nodiscard]] Result<void> validateNavGraph(const unav::NavGraph& graph, ErrorCode code) {
    // nodeOf relies on the order; an unsorted table makes it return ANOTHER
    // actor's edges rather than fail.
    for (std::size_t i = 1; i < graph.nodes.size(); ++i)
        if (graph.nodes[i].exportIndex <= graph.nodes[i - 1].exportIndex)
            return fail(code, "NAVG: nodes are not in strictly ascending exportIndex order");

    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        const unav::NavNode& node = graph.nodes[i];
        if (!runWithin(node.firstEdge, node.edgeCount, graph.edges.size()))
            return fail(code, "NAVG: a node's edge run reaches past the edge table");
        for (std::uint32_t j = 0; j < node.edgeCount; ++j)
            if (graph.edges[static_cast<std::size_t>(node.firstEdge) + j].from != i)
                return fail(code, "NAVG: an edge in a node's run does not name that node");
    }

    for (const unav::NavEdge& edge : graph.edges)
        if (edge.from >= graph.nodes.size() || edge.to >= graph.nodes.size())
            return fail(code, "NAVG: an edge endpoint names no node");

    return {};
}

[[nodiscard]] Result<void> validateWiringGraph(const unav::WiringGraph& graph, ErrorCode code) {
    for (std::size_t i = 1; i < graph.nodes.size(); ++i)
        if (graph.nodes[i].exportIndex <= graph.nodes[i - 1].exportIndex)
            return fail(code, "WIRG: nodes are not in strictly ascending exportIndex order");

    // Weaker than the real invariant, which is that `incoming` is a
    // PERMUTATION of `edges`. SS 10 records it as partial rather than
    // claiming the check it is not.
    if (graph.incoming.size() != graph.edges.size())
        return fail(code, "WIRG: incoming and edges hold different numbers of edges");

    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        const unav::WiringNode& node = graph.nodes[i];
        if (!runWithin(node.firstOutgoing, node.outgoingCount, graph.edges.size()))
            return fail(code, "WIRG: a node's outgoing run reaches past the edge table");
        for (std::uint32_t j = 0; j < node.outgoingCount; ++j)
            if (graph.edges[static_cast<std::size_t>(node.firstOutgoing) + j].from != i)
                return fail(code, "WIRG: an edge in a node's outgoing run does not name that node");

        if (!runWithin(node.firstIncoming, node.incomingCount, graph.incoming.size()))
            return fail(code, "WIRG: a node's incoming run reaches past the incoming table");
        for (std::uint32_t j = 0; j < node.incomingCount; ++j)
            if (graph.incoming[static_cast<std::size_t>(node.firstIncoming) + j].to != i)
                return fail(code, "WIRG: an edge in a node's incoming run does not name that node");
    }

    for (const unav::WiringEdge& edge : graph.edges)
        if (edge.from >= graph.nodes.size() || edge.to >= graph.nodes.size())
            return fail(code, "WIRG: an edge endpoint names no node");
    for (const unav::WiringEdge& edge : graph.incoming)
        if (edge.from >= graph.nodes.size() || edge.to >= graph.nodes.size())
            return fail(code, "WIRG: an incoming edge endpoint names no node");

    for (const unav::DanglingEvent& dangling : graph.dangling)
        if (dangling.from >= graph.nodes.size())
            return fail(code, "WIRG: a dangling event names no node");

    return {};
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

/// Append-only byte sink. Every field is emitted individually, in the order
/// its SS 4.6 to SS 4.8 table gives; nothing is memcpy'd from a struct, so no
/// padding byte ever reaches the file and the output is identical on GCC,
/// Clang and MSVC (INV-7, INV-8).
class Sink {
public:
    void putU8(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }

    void putU16(std::uint16_t value) { putLittleEndian(value, 2); }
    void putU32(std::uint32_t value) { putLittleEndian(value, 4); }
    void putU64(std::uint64_t value) { putLittleEndian(value, 8); }
    void putI32(std::int32_t value) { putU32(static_cast<std::uint32_t>(value)); }
    void putF32(float value) { putU32(std::bit_cast<std::uint32_t>(value)); }

    void putId(const SectionId& id) {
        for (const std::uint8_t part : id) putU8(part);
    }

    void putString(const std::string& value) {
        putU32(static_cast<std::uint32_t>(value.size()));
        for (const char part : value) putU8(static_cast<std::uint8_t>(part));
    }

    template <class T, class Encode>
    void putVector(const std::vector<T>& values, Encode encode) {
        putU32(static_cast<std::uint32_t>(values.size()));
        for (const T& value : values) encode(*this, value);
    }

    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
    [[nodiscard]] std::vector<std::byte> take() && noexcept { return std::move(bytes_); }

    void append(const std::vector<std::byte>& other) {
        bytes_.insert(bytes_.end(), other.begin(), other.end());
    }

private:
    void putLittleEndian(std::uint64_t value, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i)
            bytes_.push_back(static_cast<std::byte>((value >> (8U * i)) & 0xFFU));
    }

    std::vector<std::byte> bytes_;
};

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

[[nodiscard]] std::vector<std::byte> encodeRoomMap(const umap::RoomMap& map) {
    Sink sink;
    sink.putVector(map.rooms, putRoom);
    sink.putVector(map.bands, [](Sink& out, float value) { out.putF32(value); });
    sink.putVector(map.roomForZone, [](Sink& out, std::uint32_t value) { out.putU32(value); });
    sink.putVector(map.nodes, putNode);
    sink.putVector(map.leafZone, [](Sink& out, std::uint8_t value) { out.putU8(value); });
    return std::move(sink).take();
}

void putNavNode(Sink& sink, const unav::NavNode& node) {
    sink.putU32(node.exportIndex);
    sink.putString(node.className);
    sink.putU32(node.firstEdge);
    sink.putU32(node.edgeCount);
}

void putNavEdge(Sink& sink, const unav::NavEdge& edge) {
    sink.putU32(edge.from);
    sink.putU32(edge.to);
    sink.putI32(edge.distance);
    sink.putI32(edge.collisionRadius);
    sink.putI32(edge.collisionHeight);
    sink.putI32(edge.reachFlags);
    sink.putU8(edge.pruned);
}

[[nodiscard]] std::vector<std::byte> encodeNavGraph(const unav::NavGraph& graph) {
    Sink sink;
    sink.putVector(graph.nodes, putNavNode);
    sink.putVector(graph.edges, putNavEdge);
    sink.putU32(graph.discardedEndpoints);
    return std::move(sink).take();
}

void putWiringNode(Sink& sink, const unav::WiringNode& node) {
    sink.putU32(node.exportIndex);
    sink.putString(node.tag);
    sink.putU32(node.firstOutgoing);
    sink.putU32(node.outgoingCount);
    sink.putU32(node.firstIncoming);
    sink.putU32(node.incomingCount);
}

void putWiringEdge(Sink& sink, const unav::WiringEdge& edge) {
    sink.putU32(edge.from);
    sink.putU32(edge.to);
    sink.putString(edge.event);
}

void putDangling(Sink& sink, const unav::DanglingEvent& dangling) {
    sink.putU32(dangling.from);
    sink.putString(dangling.event);
}

[[nodiscard]] std::vector<std::byte> encodeWiringGraph(const unav::WiringGraph& graph) {
    Sink sink;
    sink.putVector(graph.nodes, putWiringNode);
    sink.putVector(graph.edges, putWiringEdge);
    sink.putVector(graph.incoming, putWiringEdge);
    sink.putVector(graph.dangling, putDangling);
    return std::move(sink).take();
}

// ---------------------------------------------------------------------------
// The header and the section table
// ---------------------------------------------------------------------------

struct RawHeader {
    BundleHeader header;
    /// Read but NOT bounded here -- SS 4.5. `read` bounds it once it knows
    /// the file's total size; readHeader cannot and must not.
    std::uint32_t sectionCount = 0;
};

[[nodiscard]] Result<RawHeader> decodeHeader(std::span<const std::byte> bytes) {
    if (bytes.size() < HEADER_SIZE)
        return fail(ErrorCode::MalformedData, "a bundle is at least 16 bytes; this is shorter");

    Cursor cursor(bytes.first(HEADER_SIZE));

    // INV-4: magic first, before anything else is read.
    for (const std::uint8_t expected : MAGIC) {
        UTA_TRY(const std::uint8_t actual, cursor.readU8());
        if (actual != expected)
            return fail(ErrorCode::MalformedData, "not a .utab bundle: the magic is wrong");
    }

    RawHeader raw;
    // Checked for EQUALITY and before the section table is read. A lower
    // bound would let a later version's table parse as garbage rather than
    // being refused -- INV-4.
    UTA_TRY(raw.header.formatVersion, cursor.readU32());
    if (raw.header.formatVersion != FORMAT_VERSION)
        return fail(ErrorCode::UnsupportedVersion,
                    "bundle format version " + std::to_string(raw.header.formatVersion)
                        + "; this build reads version " + std::to_string(FORMAT_VERSION));

    // INV-5. Never defaulted to a value: a file whose origin cannot be read
    // has no origin, and the distinction between unreadable and derived is
    // what lets a caller log the difference even though both fail closed.
    UTA_TRY(const std::uint8_t origin, cursor.readU8());
    if (origin > static_cast<std::uint8_t>(Origin::Authored))
        return fail(ErrorCode::MalformedData,
                    "origin byte " + std::to_string(origin) + " is not 0 or 1");
    raw.header.origin = static_cast<Origin>(origin);

    UTA_TRY(const std::uint8_t kind, cursor.readU8());
    if (kind > static_cast<std::uint8_t>(BundleKind::Character))
        return fail(ErrorCode::MalformedData,
                    "kind byte " + std::to_string(kind) + " is not 0 or 1");
    raw.header.kind = static_cast<BundleKind>(kind);

    // Refusing a non-zero reserved field is the only thing that makes a
    // reserved field a contract.
    UTA_TRY(const std::uint16_t reserved, cursor.readU16());
    if (reserved != 0)
        return fail(ErrorCode::MalformedData, "the header's reserved field is not zero");

    UTA_TRY(raw.sectionCount, cursor.readU32());
    return raw;
}

struct Descriptor {
    SectionId id{};
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

[[nodiscard]] bool knownId(const SectionId& id) noexcept {
    return id == ID_ROOM || id == ID_NAVG || id == ID_WIRG;
}

} // namespace

Result<BundleHeader> readHeader(std::span<const std::byte> bytes) {
    UTA_TRY(const RawHeader raw, decodeHeader(bytes));
    return raw.header;
}

Result<Bundle> read(std::span<const std::byte> bytes) {
    UTA_TRY(const RawHeader raw, decodeHeader(bytes));

    const std::uint64_t fileSize = bytes.size();

    // SS 4.4, and written as a DIVISION because SS 4.2 rule 1 requires it:
    // 16 + 24 * sectionCount <= fileSize is precisely the overflow that rule
    // forbids -- sectionCount is a u32, 24 * 0xAAAAAAAB wraps to 8, and the
    // test then passes on a file of any size while the reader walks
    // 2,863,311,531 descriptors.
    if (raw.sectionCount > (fileSize - HEADER_SIZE) / SECTION_DESCRIPTOR_SIZE)
        return fail(ErrorCode::MalformedData, "the section count overruns the file");

    Cursor table(bytes.subspan(HEADER_SIZE));
    std::vector<Descriptor> descriptors;
    descriptors.reserve(raw.sectionCount);
    for (std::uint32_t i = 0; i < raw.sectionCount; ++i) {
        Descriptor descriptor;
        for (std::uint8_t& part : descriptor.id) {
            UTA_TRY(part, table.readU8());
        }
        UTA_TRY(descriptor.offset, table.readU64());
        UTA_TRY(descriptor.size, table.readU64());

        // The byte is DEFINED and its value is not, so this is a version
        // refusal rather than corruption -- SS 6.
        UTA_TRY(const std::uint8_t compression, table.readU8());
        if (compression != 0)
            return fail(ErrorCode::UnsupportedVersion,
                        "section compression " + std::to_string(compression)
                            + " is not defined in format version 1");

        for (int part = 0; part < 3; ++part) {
            UTA_TRY(const std::uint8_t reserved, table.readU8());
            if (reserved != 0)
                return fail(ErrorCode::MalformedData,
                            "a section descriptor's reserved field is not zero");
        }

        // Refused, not skipped. Under SS 4.3's exact version match there is
        // no such thing as a file from a later version this reader should
        // tolerate, so an unknown id means a corrupt or hand-edited file.
        if (!knownId(descriptor.id))
            return fail(ErrorCode::MalformedData, "a section id is not defined in this version");

        descriptors.push_back(descriptor);
    }

    // The WHOLE table is validated before any section is decoded -- INV-11.
    // Trusting it and decoding each section from its own descriptor lets
    // overlapping sections decode without error, one of them wrong.
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        for (std::size_t j = i + 1; j < descriptors.size(); ++j)
            if (descriptors[i].id == descriptors[j].id)
                return fail(ErrorCode::MalformedData, "a section id appears twice");

        // Ascending offset order. This is what makes the tiling pass below a
        // single linear pass rather than a comparison of every pair.
        if (i > 0 && descriptors[i].offset < descriptors[i - 1].offset)
            return fail(ErrorCode::MalformedData,
                        "section descriptors are not in ascending offset order");

        // Computed so offset + size cannot overflow.
        if (descriptors[i].offset > fileSize || descriptors[i].size > fileSize - descriptors[i].offset)
            return fail(ErrorCode::MalformedData, "a section's byte range lies outside the file");
    }

    // The sections tile the file exactly: the first begins at the end of the
    // table, each next begins where the last ended, and the last ends at the
    // end of the file. `write` never produces a gap or a trailing byte, so
    // accepting either would be one reader tolerating what another refuses.
    std::uint64_t expected =
        HEADER_SIZE + static_cast<std::uint64_t>(raw.sectionCount) * SECTION_DESCRIPTOR_SIZE;
    for (const Descriptor& descriptor : descriptors) {
        if (descriptor.offset != expected)
            return fail(ErrorCode::MalformedData,
                        "the sections do not tile the file: a gap or an overlap");
        expected += descriptor.size;
    }
    if (expected != fileSize)
        return fail(ErrorCode::MalformedData, "bytes trail the last section");

    Bundle bundle;
    bundle.header = raw.header;

    for (const Descriptor& descriptor : descriptors) {
        Cursor payload(bytes.subspan(static_cast<std::size_t>(descriptor.offset),
                                     static_cast<std::size_t>(descriptor.size)));
        if (descriptor.id == ID_ROOM) {
            UTA_TRY(bundle.rooms, readRoomMap(payload));
            UTA_CHECK(validateRoomMap(*bundle.rooms, ErrorCode::MalformedData));
        } else if (descriptor.id == ID_NAVG) {
            UTA_TRY(bundle.nav, readNavGraph(payload));
            UTA_CHECK(validateNavGraph(*bundle.nav, ErrorCode::MalformedData));
        } else {
            UTA_TRY(bundle.wiring, readWiringGraph(payload));
            UTA_CHECK(validateWiringGraph(*bundle.wiring, ErrorCode::MalformedData));
        }

        // Trailing bytes mean the layout was misread, not that there is
        // slack -- SS 6.
        if (payload.remaining() != 0)
            return fail(ErrorCode::MalformedData, "a section ends with bytes unread");
    }

    return bundle;
}

Result<std::vector<std::byte>> write(const Bundle& bundle) {
    // Refused rather than written, so a bad bundle cannot be produced here
    // and then blamed on the reader -- SS 6.
    if (bundle.rooms) UTA_CHECK(validateRoomMap(*bundle.rooms, ErrorCode::InvalidArgument));
    if (bundle.nav) UTA_CHECK(validateNavGraph(*bundle.nav, ErrorCode::InvalidArgument));
    if (bundle.wiring) UTA_CHECK(validateWiringGraph(*bundle.wiring, ErrorCode::InvalidArgument));

    // The fixed order ROOM, NAVG, WIRG. Fixed rather than incidental because
    // docs/design.md SS Close calls names a bundle written by any tool other
    // than ubake by the hash of its own contents, and a hash over an
    // incidentally-ordered file names one world two things.
    std::vector<std::pair<SectionId, std::vector<std::byte>>> sections;
    if (bundle.rooms) sections.emplace_back(ID_ROOM, encodeRoomMap(*bundle.rooms));
    if (bundle.nav) sections.emplace_back(ID_NAVG, encodeNavGraph(*bundle.nav));
    if (bundle.wiring) sections.emplace_back(ID_WIRG, encodeWiringGraph(*bundle.wiring));

    Sink sink;
    sink.putId(MAGIC);
    // FORMAT_VERSION, not bundle.header.formatVersion. `read` only ever
    // yields 1 and this only ever emits 1, so the two cannot disagree; a
    // caller's value is an input this function has no way to honour.
    sink.putU32(FORMAT_VERSION);
    sink.putU8(static_cast<std::uint8_t>(bundle.header.origin));
    sink.putU8(static_cast<std::uint8_t>(bundle.header.kind));
    sink.putU16(0);
    sink.putU32(static_cast<std::uint32_t>(sections.size()));

    std::uint64_t offset = HEADER_SIZE + sections.size() * SECTION_DESCRIPTOR_SIZE;
    for (const auto& [id, payload] : sections) {
        sink.putId(id);
        sink.putU64(offset);
        sink.putU64(payload.size());
        sink.putU8(0); // compression: none, and version 1 defines no other
        sink.putU8(0);
        sink.putU8(0);
        sink.putU8(0);
        offset += payload.size();
    }

    for (const auto& section : sections) sink.append(section.second);

    return std::move(sink).take();
}

} // namespace uta::ubundle
