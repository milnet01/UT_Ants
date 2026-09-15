// Each zone's ambient light and fog flag --
// docs/specs/UTA-0156-zone-ambient-light.md SS 4.3 and
// docs/specs/UTA-0015-volumetric-fog.md SS 4.2. The zone a point is in is
// ubundle::zoneAt, which the renderer shares.
//
// BAKE-SIDE ONLY, as the rest of uta_ubake: it reads upkg's Model.
//
// SCOPE: UT99's bytes, as ULevel::GetZoneActor finds them. Turning them into
// light is the renderer's (SS 4.4).

#pragma once

#include "ubundle/Bundle.h"
#include "upkg/Geometry.h"

#include <vector>

namespace uta::ubake {

/// One entry per zone of `model`, and one when it has none -- SS 4.3.
///
/// Zone i's actor is its `zoneActor` where that names a placement, else the
/// level's LevelInfo; with neither, the entry is zero. Each value is the
/// actor's own record, else its class's default, else 0 (false for bFogZone).
[[nodiscard]] std::vector<ubundle::Zone> buildZones(const upkg::Model& model, const ubundle::Placements& placements);

} // namespace uta::ubake
