// Which grass a frame draws -- docs/specs/UTA-0293-grass.md SS 4.4 and SS 4.6.
//
// A FILE OF ITS OWN because it changes for its own reason: the reach and the
// blade counts are tuned against the frame budget, and nothing else in the
// renderer moves when they are. grass.vert builds the blades; this decides
// which cells it is handed and how many blades each clump grows.
//
// HOST-SIDE AND PURE, so the culling is tested without a device.

#pragma once

#include "urender/ShaderTypes.h"
#include "urender/Tiers.h"
#include "ubundle/Bundle.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace uta::urender {

/// SS 4.4: blades a clump grows near the eye, and past GRASS_FAR.
inline constexpr std::uint32_t BLADES_NEAR = 12;
inline constexpr std::uint32_t BLADES_FAR = 4;
/// SS 4.4: a blade is a tapered strip of 7 vertices, drawn as its 5
/// triangles' 15 corners. grass.vert states the same count.
inline constexpr std::uint32_t VERTICES_PER_BLADE = 15;

/// SS 4.4 and SS 4.6: how far grass reaches at a tier, in world units.
struct GrassReach {
    float far = 0;       ///< GRASS_FAR: past it a cell grows BLADES_FAR
    float fadeStart = 0; ///< GRASS_FADE: blades shrink from here
    float end = 0;       ///< GRASS_END: nothing past it is drawn
};

/// High: GRASS_END 1536 units; Ultra: 2560. GRASS_FAR is a third of GRASS_END,
/// GRASS_FADE three fifths -- first values, SS 13. All zero below High.
[[nodiscard]] GrassReach grassReachOf(Tier tier) noexcept;

/// One VEGE cell as the renderer culls it. The baker puts a cell's clumps with
/// even seeds first, so High draws the first `evenCount`.
struct GrassRun {
    std::uint32_t first = 0, evenCount = 0, count = 0;
    std::array<float, 3> low{}, high{};
};

/// `grass`'s cells, each with its even seeds counted.
[[nodiscard]] std::vector<GrassRun> grassRunsOf(const ubundle::Grass& grass);

/// One instanced draw: a run of clumps, each growing `blades` blades.
struct GrassDraw {
    std::uint32_t firstInstance = 0, instanceCount = 0, blades = 0;
};

/// SS 4.4 and SS 4.6: the runs `viewProj` sees within the tier's GRASS_END of
/// `eye`, nearest box distance, each with its blade count. Nothing below High,
/// and at High only each run's even seeds. `viewProj` is column-major, as
/// gpu::Mat4 is.
[[nodiscard]] std::vector<GrassDraw> visibleGrass(std::span<const GrassRun> runs, const gpu::Mat4& viewProj,
                                                  const std::array<float, 3>& eye, Tier tier);

} // namespace uta::urender
