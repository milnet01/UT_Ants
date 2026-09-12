// A level's probes on the GPU -- docs/specs/UTA-0014-vulkan-draw-path.md
// SS 4.7.
//
// The hash table shaders/probes.glsl finds a probe in. A table rather than a
// dense grid over the probes' bounding box: UTA-0112 seeds probes near
// geometry at 128-unit spacing, so a grid grows with the level's box -- to
// hundreds of megabytes on a map-sized one -- while a table grows with the
// probes.
//
// NONE OF UTA-0112 SS 4.9's EVALUATION IS HERE. The faces go up as the bundle
// stores them and probes.glsl reads them; INV-7 grades that shader.
//
// INTERNAL, device-free.

#pragma once

#include "ubundle/Bundle.h"
#include "urender/ShaderTypes.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace uta::urender {

/// probes.glsl's probeHash, which this must match exactly: a probe the builder
/// placed where the shader does not look is a probe no pixel ever finds.
[[nodiscard]] constexpr std::uint32_t probeHash(const std::array<std::int32_t, 3>& cell) noexcept {
    return (static_cast<std::uint32_t>(cell[0]) * 73856093u) ^ (static_cast<std::uint32_t>(cell[1]) * 19349663u)
           ^ (static_cast<std::uint32_t>(cell[2]) * 83492791u);
}

struct ProbeTable {
    std::uint32_t spacing = 0;         ///< LPRB's; 0 when there are no probes
    std::uint32_t tableMask = 0;       ///< the table's size minus one; the size is a power of two
    std::uint32_t longestRun = 0;      ///< the most slots past its hash any probe sits
    std::vector<gpu::ProbeCell> cells; ///< the table; an entry whose probe is -1 is empty
    std::vector<gpu::Probe> probes;    ///< in LPRB order
};

/// The table for `lightProbes`, with at least twice as many slots as probes so
/// no run grows long. An absent or empty section gives a table with no probes,
/// which lights nothing -- SS 6's no-probe rule applied uniformly.
[[nodiscard]] ProbeTable probeTable(const std::optional<ubundle::LightProbes>& lightProbes);

} // namespace uta::urender
