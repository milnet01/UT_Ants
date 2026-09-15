// Each zone's ambient light, and the zone a point is in --
// docs/specs/UTA-0156-zone-ambient-light.md SS 4.3.
//
// BAKE-SIDE ONLY, as the rest of uta_ubake: it reads upkg's Model.
//
// SCOPE: UT99's bytes, as ULevel::GetZoneActor finds them. Turning them into
// light is the renderer's (SS 4.4).

#pragma once

#include "ubundle/Bundle.h"
#include "umap/Rooms.h"
#include "upkg/Geometry.h"

#include <array>
#include <cstdint>
#include <vector>

namespace uta::ubake {

/// One entry per zone of `model`, and one when it has none -- SS 4.3.
///
/// Zone i's actor is its `zoneActor` where that names a placement, else the
/// level's LevelInfo; with neither, the entry is zero. Each value is the
/// actor's own byte, else its class's default, else 0.
[[nodiscard]] std::vector<ubundle::ZoneAmbient> buildZones(const upkg::Model& model,
                                                          const ubundle::Placements& placements);

/// The zone of the room `umap::roomAt` finds at `location`, or 0 where it finds
/// none or the zone is not below `zoneCount` -- SS 4.3's mover rule.
[[nodiscard]] std::uint8_t zoneAt(const umap::RoomMap& rooms, const std::array<float, 3>& location,
                                  std::size_t zoneCount);

} // namespace uta::ubake
