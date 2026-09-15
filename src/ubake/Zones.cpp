// Each zone's ambient light, and the zone a point is in --
// docs/specs/UTA-0156-zone-ambient-light.md SS 4.3.

#include "ubake/Zones.h"

#include "ubake/Actors.h"
#include "umap/Build.h"

#include <algorithm>
#include <string_view>

namespace uta::ubake {
namespace {

// ZONE's ceiling is the engine's, which ROOM's build already refuses past.
static_assert(ubundle::ZONE_LIMIT == umap::ZONE_CEILING);

constexpr std::string_view LEVEL_INFO = "engine.levelinfo";

std::uint8_t byteOf(std::string_view name, const ubundle::ActorPlacement& actor,
                    const ubundle::ActorClass& actorClass) {
    const ubundle::PropertyRecord* record = detail::resolvedRecord(
        name, actor.properties, actorClass.defaults,
        [](const ubundle::PropertyRecord& candidate) { return candidate.kind == ubundle::ValueKind::Byte; });
    return record == nullptr ? 0 : std::get<std::uint8_t>(record->value);
}

ubundle::ZoneAmbient ambientOf(const ubundle::Placements& placements, const ubundle::ActorPlacement* actor) {
    if (actor == nullptr) return {};
    const ubundle::ActorClass& actorClass = placements.classes[actor->classIndex];
    return {byteOf("ambientbrightness", *actor, actorClass), byteOf("ambienthue", *actor, actorClass),
            byteOf("ambientsaturation", *actor, actorClass)};
}

/// The placement of export `exportIndex`; placements are ascending by it.
const ubundle::ActorPlacement* placementOf(const ubundle::Placements& placements, std::uint32_t exportIndex) {
    const auto found = std::lower_bound(
        placements.actors.begin(), placements.actors.end(), exportIndex,
        [](const ubundle::ActorPlacement& actor, std::uint32_t index) { return actor.exportIndex < index; });
    return found != placements.actors.end() && found->exportIndex == exportIndex ? &*found : nullptr;
}

/// The lowest-exportIndex placement of LevelInfo or a class descending from it.
const ubundle::ActorPlacement* levelInfoOf(const ubundle::Placements& placements) {
    for (const ubundle::ActorPlacement& actor : placements.actors) {
        const ubundle::ActorClass& actorClass = placements.classes[actor.classIndex];
        if (actorClass.path == LEVEL_INFO
            || std::find(actorClass.ancestry.begin(), actorClass.ancestry.end(), LEVEL_INFO)
                   != actorClass.ancestry.end())
            return &actor;
    }
    return nullptr;
}

} // namespace

std::vector<ubundle::ZoneAmbient> buildZones(const upkg::Model& model, const ubundle::Placements& placements) {
    // ULevel::GetZoneActor: a zone's own actor, else the LevelInfo.
    const ubundle::ZoneAmbient level = ambientOf(placements, levelInfoOf(placements));
    std::vector<ubundle::ZoneAmbient> zones;
    zones.reserve(std::max<std::size_t>(model.zones.size(), 1));
    for (const upkg::ZoneProperties& zone : model.zones) {
        const ubundle::ActorPlacement* actor = zone.zoneActor.kind() == upkg::ObjectReferenceKind::Export
                                                   ? placementOf(placements, zone.zoneActor.index())
                                                   : nullptr;
        zones.push_back(actor != nullptr ? ambientOf(placements, actor) : level);
    }
    if (zones.empty()) zones.push_back(level);
    return zones;
}

std::uint8_t zoneAt(const umap::RoomMap& rooms, const std::array<float, 3>& location, std::size_t zoneCount) {
    const std::uint32_t room = umap::roomAt(rooms, umap::Point3{location[0], location[1], location[2]});
    if (room == umap::NO_ROOM || room >= rooms.rooms.size()) return 0;
    const std::uint32_t zone = rooms.rooms[room].zoneIndex;
    return zone < zoneCount ? static_cast<std::uint8_t>(zone) : 0;
}

} // namespace uta::ubake
