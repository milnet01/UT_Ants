// Each zone's ambient light and fog flag --
// docs/specs/UTA-0156-zone-ambient-light.md SS 4.3 and
// docs/specs/UTA-0015-volumetric-fog.md SS 4.2.

#include "ubake/Zones.h"

#include "ubake/Actors.h"
#include "umap/Build.h"

#include <algorithm>
#include <string_view>
#include <variant>

namespace uta::ubake {
namespace {

// ZONE's ceiling is the engine's, which ROOM's build already refuses past.
static_assert(ubundle::ZONE_LIMIT == umap::ZONE_CEILING);

constexpr std::string_view LEVEL_INFO = "engine.levelinfo";

const ubundle::PropertyRecord* recordOf(std::string_view name, ubundle::ValueKind kind,
                                        const ubundle::ActorPlacement& actor, const ubundle::ActorClass& actorClass) {
    return detail::resolvedRecord(name, actor.properties, actorClass.defaults,
                                  [kind](const ubundle::PropertyRecord& candidate) { return candidate.kind == kind; });
}

std::uint8_t byteOf(std::string_view name, const ubundle::ActorPlacement& actor,
                    const ubundle::ActorClass& actorClass) {
    const ubundle::PropertyRecord* record = recordOf(name, ubundle::ValueKind::Byte, actor, actorClass);
    return record == nullptr ? 0 : std::get<std::uint8_t>(record->value);
}

std::uint8_t flagOf(std::string_view name, const ubundle::ActorPlacement& actor,
                    const ubundle::ActorClass& actorClass) {
    const ubundle::PropertyRecord* record = recordOf(name, ubundle::ValueKind::Bool, actor, actorClass);
    return record != nullptr && std::get<bool>(record->value) ? 1 : 0;
}

ubundle::Zone zoneOf(const ubundle::Placements& placements, const ubundle::ActorPlacement* actor) {
    if (actor == nullptr) return {};
    const ubundle::ActorClass& actorClass = placements.classes[actor->classIndex];
    return {byteOf("ambientbrightness", *actor, actorClass), byteOf("ambienthue", *actor, actorClass),
            byteOf("ambientsaturation", *actor, actorClass), flagOf("bfogzone", *actor, actorClass)};
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

std::vector<ubundle::Zone> buildZones(const upkg::Model& model, const ubundle::Placements& placements) {
    // ULevel::GetZoneActor: a zone's own actor, else the LevelInfo.
    const ubundle::Zone level = zoneOf(placements, levelInfoOf(placements));
    std::vector<ubundle::Zone> zones;
    zones.reserve(std::max<std::size_t>(model.zones.size(), 1));
    for (const upkg::ZoneProperties& zone : model.zones) {
        const ubundle::ActorPlacement* actor = zone.zoneActor.kind() == upkg::ObjectReferenceKind::Export
                                                   ? placementOf(placements, zone.zoneActor.index())
                                                   : nullptr;
        zones.push_back(actor != nullptr ? zoneOf(placements, actor) : level);
    }
    if (zones.empty()) zones.push_back(level);
    return zones;
}

} // namespace uta::ubake
