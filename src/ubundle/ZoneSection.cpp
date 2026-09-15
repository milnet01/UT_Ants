// The ZONE section: each zone's ambient light, and the rule tying every drawn
// vertex's zone to it -- docs/specs/UTA-0156-zone-ambient-light.md SS 4.1,
// SS 4.2, INV-1 and INV-2.

#include "Sections.h"

#include <string>

namespace uta::ubundle::detail {
namespace {

/// SS 4.1: three u8, fixed.
constexpr std::uint64_t ZONE_ENTRY = 3;

[[nodiscard]] Result<ZoneAmbient> readZone(Cursor& cursor) {
    ZoneAmbient zone;
    UTA_TRY(zone.brightness, cursor.readU8());
    UTA_TRY(zone.hue, cursor.readU8());
    UTA_TRY(zone.saturation, cursor.readU8());
    return zone;
}

void putZone(Sink& sink, const ZoneAmbient& zone) {
    sink.putU8(zone.brightness);
    sink.putU8(zone.hue);
    sink.putU8(zone.saturation);
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

Result<std::vector<ZoneAmbient>> readZones(Cursor& cursor) {
    return readVector<ZoneAmbient>(cursor, ZONE_ENTRY, "zones", readZone);
}

Result<void> validateZones(const std::vector<ZoneAmbient>& zones, ErrorCode code) {
    if (zones.empty() || zones.size() > ZONE_LIMIT)
        return fail(code, "ZONE: " + std::to_string(zones.size()) + " entries, where 1 to "
                              + std::to_string(ZONE_LIMIT) + " are allowed");
    return {};
}

std::vector<std::byte> encodeZones(const std::vector<ZoneAmbient>& zones) {
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

} // namespace uta::ubundle::detail
