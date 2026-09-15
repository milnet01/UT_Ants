// The ZONE section: each zone's ambient light and fog flag, the rule tying
// every drawn vertex's zone to it, and the zone a point is in --
// docs/specs/UTA-0156-zone-ambient-light.md SS 4.1, SS 4.2, INV-1 and INV-2,
// and docs/specs/UTA-0015-volumetric-fog.md SS 4.1 and INV-1.

#include "Sections.h"

#include <string>

namespace uta::ubundle {
namespace detail {
namespace {

/// UTA-0156 SS 4.1's three u8, then UTA-0015's fog flag.
constexpr std::uint64_t ZONE_ENTRY = 4;

[[nodiscard]] Result<Zone> readZone(Cursor& cursor) {
    Zone zone;
    UTA_TRY(zone.brightness, cursor.readU8());
    UTA_TRY(zone.hue, cursor.readU8());
    UTA_TRY(zone.saturation, cursor.readU8());
    UTA_TRY(zone.fog, cursor.readU8());
    return zone;
}

void putZone(Sink& sink, const Zone& zone) {
    sink.putU8(zone.brightness);
    sink.putU8(zone.hue);
    sink.putU8(zone.saturation);
    sink.putU8(zone.fog);
}

/// Every vertex of `geometry` names a zone below `bound`.
[[nodiscard]] Result<void> zonesBelow(const Geometry& geometry, std::size_t bound, bool zonePresent,
                                      const std::string& where, ErrorCode code) {
    for (std::size_t i = 0; i < geometry.vertices.size(); ++i) {
        const std::uint8_t zone = geometry.vertices[i].zone;
        if (zone < bound) continue;
        return fail(code, where + ": vertex " + std::to_string(i) + " names zone " + std::to_string(zone)
                              + (zonePresent ? ", and ZONE holds " + std::to_string(bound) + " entries"
                                             : ", and the bundle has no ZONE"));
    }
    return {};
}

} // namespace

Result<std::vector<Zone>> readZones(Cursor& cursor) {
    return readVector<Zone>(cursor, ZONE_ENTRY, "zones", readZone);
}

Result<void> validateZones(const std::vector<Zone>& zones, ErrorCode code) {
    if (zones.empty() || zones.size() > ZONE_LIMIT)
        return fail(code, "ZONE: " + std::to_string(zones.size()) + " entries, where 1 to "
                              + std::to_string(ZONE_LIMIT) + " are allowed");
    for (std::size_t i = 0; i < zones.size(); ++i)
        if (zones[i].fog > 1)
            return fail(code, "ZONE: entry " + std::to_string(i) + " has a fog byte of "
                                  + std::to_string(zones[i].fog) + ", where only 0 and 1 are allowed");
    return {};
}

std::vector<std::byte> encodeZones(const std::vector<Zone>& zones) {
    Sink sink;
    sink.putVector(zones, putZone);
    return std::move(sink).take();
}

Result<void> validateVertexZones(const Bundle& bundle, ErrorCode code) {
    // With no ZONE, only zone 0 is allowed: the renderer's one zero entry.
    const bool present = bundle.zones.has_value();
    const std::size_t bound = present ? bundle.zones->size() : 1;
    if (bundle.geometry) UTA_CHECK(zonesBelow(*bundle.geometry, bound, present, "GEOM", code));
    if (bundle.movers) {
        for (std::size_t i = 0; i < bundle.movers->size(); ++i)
            UTA_CHECK(zonesBelow((*bundle.movers)[i].geometry, bound, present,
                                 "MOVR: shape " + std::to_string(i) + "'s geometry", code));
    }
    return {};
}

} // namespace detail

std::uint8_t zoneAt(const umap::RoomMap& rooms, const std::array<float, 3>& location, std::size_t zoneCount) {
    const std::uint32_t room = umap::roomAt(rooms, umap::Point3{location[0], location[1], location[2]});
    if (room == umap::NO_ROOM || room >= rooms.rooms.size()) return 0;
    const std::uint32_t zone = rooms.rooms[room].zoneIndex;
    return zone < zoneCount ? static_cast<std::uint8_t>(zone) : 0;
}

} // namespace uta::ubundle
