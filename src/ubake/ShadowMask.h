// The baked shadow mask -- docs/specs/UTA-0326-baked-shadow-mask.md SS 4.3
// and SS 4.4.
//
// For every lit polygon and every light that can light it, how much of that
// light each texel of the polygon sees past the level's own geometry. The
// renderer reads it in place of the shadow map for a level surface.
//
// DETERMINISTIC AT ANY WORKER COUNT. A texel depends on its pair alone, jobs
// split the charts, and each chart's pairs are gathered into its own slot
// before the atlas is assembled in chart order (INV-7).

#pragma once

#include "core/Error.h"
#include "core/Jobs.h"
#include "ubundle/Bundle.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace uta::ubake {

/// SS 4.3: the starting texel size, in UT units.
inline constexpr float SHADOW_MASK_TEXEL_SIZE = 8;

/// SS 4.3: how far above the surface a ray starts, in UT units.
inline constexpr double SHADOW_MASK_LIFT = 0.5;

/// SS 4.3: the coarsest texel size packing tries before it refuses.
inline constexpr float SHADOW_MASK_TEXEL_CEILING = 1024;

/// A masked material's cutout: 1 where its base picture's alpha is at least
/// half -- MASK_THRESHOLD, as shadow.frag tests it -- row-major, top row first,
/// at the picture's own size.
struct Cutout {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> solid;
};

/// Cutouts by MATS id. A PF_Masked surface whose material has none is solid.
using Cutouts = std::map<std::string, Cutout, std::less<>>;

/// SS 4.3 and SS 4.4: the level's shadow mask. InvalidArgument without GEOM
/// or LITE; MalformedData when no texel size up to SHADOW_MASK_TEXEL_CEILING
/// fits SHADOW_MASK_ATLAS_LIMIT.
[[nodiscard]] Result<ubundle::ShadowMask> bakeShadowMask(const ubundle::Bundle& bundle, JobSystem& jobs,
                                                         const Cutouts& cutouts = {});

/// As above, starting from `texelSize`, so a test reaches the coarsening rule.
[[nodiscard]] Result<ubundle::ShadowMask> bakeShadowMask(const ubundle::Bundle& bundle, JobSystem& jobs,
                                                         float texelSize, const Cutouts& cutouts = {});

} // namespace uta::ubake
