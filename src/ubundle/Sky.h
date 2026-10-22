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

/// UTA-0383: a recipe gives its sky's colours as the screen shows them, but a
/// map's pictures are stored darker and the screen multiplies them by its
/// exposure. The bake divides by this to store the sky as a picture is stored.
/// urender's EXPOSURE, which Frame.cpp asserts it equals.
inline constexpr float PAINTED_SKY_EXPOSURE = 5.03f;

/// A painted sky's clouds, in each linear channel, as a picture is stored --
/// UTA-0350 SS 4.3. The renderer draws with the same value (SS 4.4).
inline constexpr float PAINTED_SKY_CLOUD = 0.85f / PAINTED_SKY_EXPOSURE;

/// UTA-0389: the sea below a painted sky's horizon -- water's reflectance
/// looking straight down, and how far its own colour lies from the sky's top
/// toward its horizon. Halfway lands near TheBoat's water picture
/// (147, 169, 237 on screen, 2026-10-22), brighter than its horizon sky.
inline constexpr double PAINTED_SEA_R0 = 0.02;
inline constexpr double PAINTED_SEA_MIX = 0.5;

/// The painted sky's light from direction `d` (unit, z up): the gradient,
/// clouds blended in at their cover where they show -- UTA-0350 SS 4.3. Below
/// the horizon, a calm sea mirrors the gradient (UTA-0389). The
/// probe bake reads this; the renderer draws the same gradient and fade.
[[nodiscard]] std::array<float, 3> paintedSkyLight(const PaintedSky& sky,
                                                   const std::array<double, 3>& d) noexcept;

} // namespace uta::ubundle
