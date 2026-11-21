#include "umap/Rooms.h"

#include <cstddef>
#include <optional>

namespace uta::umap {
namespace {

/// A zone index, as the descent reads it, mapped to a room position.
///
/// Three ways this yields NO_ROOM and each is its own rule. Zone 0 is the
/// engine's null zone and is never a room (SS 4.2 measured no leaf naming it
/// across the reference install). ZONE_REFUSED marks a leaf whose zone the
/// build refused (SS 6). And a zone at or past the table's size is out of
/// range -- which is what makes an EMPTY roomForZone safe, the case the two
/// maps in SS 4.2's census with no zone table at all produce.
[[nodiscard]] std::uint32_t roomForZone(const RoomMap& map, std::uint8_t zone) {
    if (zone == 0 || zone == ZONE_REFUSED) return NO_ROOM;
    if (static_cast<std::size_t>(zone) >= map.roomForZone.size()) return NO_ROOM;
    return map.roomForZone[static_cast<std::size_t>(zone)];
}

/// The zone byte the descent ends at, or nothing where the tree cannot be
/// descended: empty, a bad child or leaf index, or a cycle. roomAt and
/// zoneNumberAt share it, so the two always agree on the leaf.
[[nodiscard]] std::optional<std::uint8_t> descend(const RoomMap& map, Point3 point) {
    if (map.nodes.empty()) return std::nullopt;

    std::size_t index = 0;
    // Bounded by the node count, because a valid descent visits each node at
    // most once. The counter bounds a CYCLE; the range checks below bound an
    // out-of-range index. Those are different failures -- a cycle loops
    // forever, a bad index reads memory that is not ours -- and a counter
    // catches only the first. INV-3 covers both. Both arrive from a file this
    // project did not write.
    for (std::size_t step = 0; step < map.nodes.size(); ++step) {
        const RoomMap::Node& node = map.nodes[index];

        // The side of the plane, and the convention is the engine's own:
        // iZone and iLeaf are indexed 1 = front, 0 = back. Reading that array
        // the other way compiles and returns a plausible room for most
        // points while being wrong for all of them, which is what INV-2
        // exists to catch.
        //
        // A point exactly on the plane takes the FRONT side. The choice is
        // arbitrary and only has to be stated, so that a point on a shared
        // wall resolves to one room rather than to whichever way the
        // rounding happened to fall.
        const float side = node.normal.x * point.x + node.normal.y * point.y +
                           node.normal.z * point.z - node.w;
        const std::size_t which = side >= 0.0F ? 1U : 0U;

        const std::int32_t child = which == 1U ? node.iFront : node.iBack;
        if (child != INDEX_NONE) {
            if (child < 0 || static_cast<std::size_t>(child) >= map.nodes.size())
                return std::nullopt;
            index = static_cast<std::size_t>(child);
            continue;
        }

        const std::int32_t leaf = node.iLeaf[which];
        if (leaf != INDEX_NONE) {
            if (leaf < 0 || static_cast<std::size_t>(leaf) >= map.leafZone.size())
                return std::nullopt;
            return map.leafZone[static_cast<std::size_t>(leaf)];
        }

        return node.iZone[which];
    }

    // The counter ran out, so the child links form a cycle. Refusing beats
    // looping: this hangs the bake otherwise, and the bake is not interactive.
    return std::nullopt;
}

} // namespace

std::uint32_t roomAt(const RoomMap& map, Point3 point) {
    const std::optional<std::uint8_t> zone = descend(map, point);
    return zone ? roomForZone(map, *zone) : NO_ROOM;
}

ZoneHit zoneNumberAt(const RoomMap& map, Point3 point) {
    const std::optional<std::uint8_t> zone = descend(map, point);
    if (!zone) return {};
    // Zone 0 means outside the level only on a map that has rooms at all: on
    // one with none, a failed build and a level with no zone table both read
    // as zone 0 everywhere, and that must never kill (UTA-0493 SS 4.1).
    if (*zone == 0) return map.rooms.empty() ? ZoneHit{} : ZoneHit{ZoneHit::Kind::Outside, 0};
    if (roomForZone(map, *zone) == NO_ROOM) return {};
    return {ZoneHit::Kind::Zone, *zone};
}

} // namespace uta::umap
