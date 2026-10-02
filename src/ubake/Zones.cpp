// Each zone's ambient light and fog flag, and the level's brightness --
// docs/specs/UTA-0156-zone-ambient-light.md SS 4.3, SS 4.5 and
// docs/specs/UTA-0015-volumetric-fog.md SS 4.2.

#include "ubake/Zones.h"

#include "ubake/Actors.h"
#include "umap/Build.h"

#include <algorithm>
#include <array>
#include <cmath>
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

/// UTA-0276: a pan speed is the actor's own record, else its class's default,
/// else 1, Engine.u's ZoneInfo default. A NaN or infinity is 1, so ZONE's
/// refusal of a speed that is not finite is never reached from a map.
float speedOf(std::string_view name, const ubundle::ActorPlacement& actor, const ubundle::ActorClass& actorClass) {
    const ubundle::PropertyRecord* record = recordOf(name, ubundle::ValueKind::Float, actor, actorClass);
    if (record == nullptr) return 1;
    const float value = std::get<float>(record->value);
    return std::isfinite(value) ? value : 1.0F;
}

/// UTA-0215: a Vector property, else 0. A part that is not finite is 0, so
/// ZONE's refusal of a tint that is not finite is never reached from a map.
std::array<float, 3> vectorOf(std::string_view name, const ubundle::ActorPlacement& actor,
                              const ubundle::ActorClass& actorClass) {
    const ubundle::PropertyRecord* record = recordOf(name, ubundle::ValueKind::Vector, actor, actorClass);
    if (record == nullptr) return {};
    std::array<float, 3> value = std::get<std::array<float, 3>>(record->value);
    for (float& part : value)
        if (!std::isfinite(part)) part = 0;
    return value;
}

ubundle::Zone zoneOf(const ubundle::Placements& placements, const ubundle::ActorPlacement* actor) {
    if (actor == nullptr) return {};
    const ubundle::ActorClass& actorClass = placements.classes[actor->classIndex];
    ubundle::Zone zone{byteOf("ambientbrightness", *actor, actorClass), byteOf("ambienthue", *actor, actorClass),
                       byteOf("ambientsaturation", *actor, actorClass), flagOf("bfogzone", *actor, actorClass),
                       {speedOf("texupanspeed", *actor, actorClass), speedOf("texvpanspeed", *actor, actorClass)}};
    // UTA-0215 SS 4.1: UT99 tints the view by ViewFlash's X alone.
    zone.water = flagOf("bwaterzone", *actor, actorClass);
    zone.viewFog = vectorOf("viewfog", *actor, actorClass);
    zone.viewFlash = vectorOf("viewflash", *actor, actorClass)[0];
    return zone;
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

float levelBrightnessOf(const ubundle::Placements& placements) {
    const ubundle::ActorPlacement* level = levelInfoOf(placements);
    if (level == nullptr) return 1;
    const ubundle::PropertyRecord* record =
        recordOf("brightness", ubundle::ValueKind::Float, *level, placements.classes[level->classIndex]);
    if (record == nullptr) return 1;
    const float value = std::get<float>(record->value);
    return std::isfinite(value) ? std::max(value, 0.0F) : 1.0F;
}

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
