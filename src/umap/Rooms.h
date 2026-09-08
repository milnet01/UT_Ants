// The level map: rooms derived from a level's own BSP zones, and the rule for
// which room a point falls in.
//
// docs/specs/UTA-0007-room-partition-and-lookup.md.
//
// SCOPE: the room types and the point-in-room lookup. Nothing here reads a
// package byte, and no member of any type below is an `upkg` type -- INV-5.
// That is what lets this library link uta_core alone, which is what keeps the
// package reader out of every runtime target (docs/design.md rule 2).
// Building a RoomMap from a Model is Build.h's job, in the other library.
//
// LIFETIME: as unav's graphs, these types OWN everything they hold. A RoomMap
// outlives the `Model` it was built from, because ubundle serialises it after
// that package may be closed.
//
// TWO INDEX SPACES, and confusing them is silent. A ZONE INDEX is the source
// Model's own numbering and is what `Room::zoneIndex` holds; a ROOM POSITION
// is an index into `RoomMap::rooms` and is what `roomAt` returns.
// `roomForZone` is the bridge, and every consumer starts there.
//
// NO PER-PLAYER STATE LIVES HERE, and that is a security property rather than
// an omission -- INV-7. Exploration is ugame's, recorded server-side; design
// rule 18 makes a client holding another team's exploration a wallhack, and a
// flag on Room would be serialised into the bundle every client holds.

#ifndef UTA_UMAP_ROOMS_H
#define UTA_UMAP_ROOMS_H

#include <cstdint>
#include <vector>

namespace uta::umap {

/// A point on the map plane, and a point in the level. `umap`'s own, because
/// `upkg`'s Vector3 is declared in src/upkg/Properties.h and is not linkable
/// from here (docs/design.md rule 2, SS 4.1).
struct Point2 {
    float x = 0, y = 0;
};

struct Point3 {
    float x = 0, y = 0, z = 0;
};

/// No room. Returned by `roomAt` for a point outside every room, and stored
/// in `roomForZone` for every zone that has none -- SS 4.3.
inline constexpr std::uint32_t NO_ROOM = 0xFFFFFFFFu;

/// Stored in `RoomMap::leafZone` for a leaf whose zone the build refused.
/// Resolves to NO_ROOM, never to a room -- SS 4.6, SS 6.
inline constexpr std::uint8_t ZONE_REFUSED = 0xFFu;

/// The file's own "no child" / "not a leaf" sentinel, as UE1 spells it.
inline constexpr std::int32_t INDEX_NONE = -1;

/// One connected piece of a room's floor plan: an outer ring and its holes.
/// Every ring is closed with its first vertex NOT repeated at the end.
struct Footprint {
    std::vector<Point2> outer;
    std::vector<std::vector<Point2>> holes;
};

/// One room: a zone of the level, its footprint, and where it sits.
struct Room {
    /// The zone index this room was built from, in the source Model's own
    /// numbering. Never 0 -- SS 4.3.
    std::uint32_t zoneIndex = 0;

    /// One entry per CONNECTED component of the room -- SS 4.4. A zone used
    /// twice in a level (two pools of one water zone) is one room with two
    /// parts, not two rooms and not one self-intersecting ring.
    std::vector<Footprint> parts;

    /// Vertical extent, from the room's own samples -- SS 4.4. Both keep
    /// these defaults where the room caught no sample; that is not a
    /// measurement, and SS 4.5 excludes such a room from clustering.
    float minZ = 0;
    float maxZ = 0;

    /// Floor bands this room appears on -- SS 4.5. Never empty. More than one
    /// marks a room that connects bands, such as a stairwell.
    std::vector<std::uint16_t> floors;
};

/// A level's rooms, and the data the lookup needs.
struct RoomMap {
    /// The BSP planes and child links the descent needs, copied from the
    /// source Model at bake time -- SS 4.6. A copy, deliberately: the
    /// alternative is the runtime holding a upkg::Model, which design rule 2
    /// forbids. It is also narrower than the source, carrying nothing the
    /// descent does not read.
    struct Node {
        Point3 normal;
        float w = 0;
        std::int32_t iFront = INDEX_NONE, iBack = INDEX_NONE;
        /// 1 = front, 0 = back -- the engine's own convention. INDEX_NONE
        /// means the side is not a leaf.
        std::int32_t iLeaf[2] = {INDEX_NONE, INDEX_NONE};
        std::uint8_t iZone[2] = {0, 0};
    };

    std::vector<Room> rooms;

    /// Floor band boundaries, ascending. `bands[i]` is the LOWEST midpoint in
    /// cluster i; band i covers [bands[i], bands[i+1]), and the topmost band
    /// is unbounded above -- SS 4.5.
    std::vector<float> bands;

    /// Maps a source zone index to a position in `rooms`, or NO_ROOM. Sized
    /// to the source Model's zone table, so index 0 is present and always
    /// NO_ROOM, as is every zone no leaf names -- SS 4.2.
    std::vector<std::uint32_t> roomForZone;

    std::vector<Node> nodes;

    /// leafZone[i] is leaves[i].iZone in the source Model, narrowed. Leaf's
    /// own iZone is i32 in upkg; TWO refusals stand between it and this byte
    /// and BOTH are required (SS 6) -- the zone table must be within the
    /// engine's 64-zone ceiling, and the leaf's zone must be inside that
    /// table. Membership alone does not make the narrowing safe: a file
    /// declaring 300 zones passes a membership test and aliases 300 onto 44.
    std::vector<std::uint8_t> leafZone;
};

/// The room containing `point`, or NO_ROOM. SS 4.3.
///
/// Total: every input returns, including a RoomMap whose child indices form a
/// cycle or point outside their tables (INV-3).
[[nodiscard]] std::uint32_t roomAt(const RoomMap& map, Point3 point);

} // namespace uta::umap

#endif // UTA_UMAP_ROOMS_H
