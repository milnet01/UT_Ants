// Where the level's sky is seen from -- UTA-0163, shared since UTA-0292.
//
// UT99 draws a PF_FakeBackdrop surface as a window onto the sky zone, seen
// from its SkyZoneInfo. The renderer draws the sky from here (urender/Sky.h),
// and the probe bake follows a ray that meets the sky from here too
// (docs/specs/UTA-0112-baked-light-probes.md SS 4.12 item 2), so both read
// one copy.

#pragma once

#include "ubundle/Bundle.h"

#include <array>
#include <optional>

namespace uta::ubundle {

/// Where the sky is drawn from.
struct SkyView {
    std::array<float, 3> location{};
};

/// ZoneInfo.LinkToSkybox's choice, read from Engine.u's script: the last
/// SkyZoneInfo (or subclass) in the level's actor order, then the last whose
/// bHighDetail matches the detail mode, which urender always draws at. Nothing
/// when the level has none.
[[nodiscard]] std::optional<SkyView> skyViewOf(const Placements& placements);
[[nodiscard]] std::optional<SkyView> skyViewOf(const Bundle& bundle);

} // namespace uta::ubundle
