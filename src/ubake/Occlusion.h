// Baked ambient occlusion -- docs/specs/UTA-0164-ambient-occlusion.md SS 4.2
// and SS 4.3.
//
// Each lit polygon of GEOM gets a rectangle of an atlas, on a texel grid fixed
// by its plane's normal alone, so the polygons of one BSP cut sample the same
// world points. A texel stores how open the space just above it is.
//
// DETERMINISTIC AT ANY WORKER COUNT. A texel's value depends on that texel
// alone, its sum runs in directions()' order, and it goes to its own slot of
// the atlas (INV-6).

#pragma once

#include "core/Error.h"
#include "core/Jobs.h"
#include "ubundle/Bundle.h"

#include <cstdint>

namespace uta::ubake {

/// SS 4.3: the starting texel size, in UT units -- four across OCCLUSION_DISTANCE.
inline constexpr float OCCLUSION_TEXEL_SIZE = 16;

/// SS 4.3: how far an occluder counts, in UT units.
inline constexpr double OCCLUSION_DISTANCE = 64;

/// SS 4.3: how far above the surface a ray starts, in UT units.
inline constexpr double OCCLUSION_LIFT = 0.5;

/// SS 6: the coarsest texel size packing tries before it refuses.
inline constexpr float OCCLUSION_TEXEL_CEILING = 1024;

/// SS 4.2: the side of the white block at the atlas origin, in texels.
inline constexpr std::uint32_t OCCLUSION_WHITE_BLOCK = 4;

/// SS 4.2 and SS 4.3: the level's occlusion atlas and one uv per vertex of
/// `geometry`. MalformedData when no texel size up to
/// OCCLUSION_TEXEL_CEILING fits OCCLUSION_ATLAS_LIMIT.
[[nodiscard]] Result<ubundle::Occlusion> bakeOcclusion(const ubundle::Geometry& geometry, JobSystem& jobs);

/// As above, starting from `texelSize` rather than OCCLUSION_TEXEL_SIZE, so a
/// test can reach the coarsening rule with a small level (INV-5).
[[nodiscard]] Result<ubundle::Occlusion> bakeOcclusion(const ubundle::Geometry& geometry, JobSystem& jobs,
                                                       float texelSize);

} // namespace uta::ubake
