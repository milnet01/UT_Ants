// Per-zone facts the renderer works out at load -- docs/specs/UTA-0089-water-reflections.md
// SS 4.1. A zone's light is UTA-0156's and lives in Frame.cpp's upload; this
// file holds only what is derived from the level's geometry.
//
// INTERNAL, device-free.

#pragma once

#include "ubundle/Bundle.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace uta::urender {

/// One flag per zone, 1 where a PF_FAKE_BACKDROP triangle of `geometry` has
/// a vertex in that zone -- UTA-0089 SS 4.1. Sized `zoneCount`; a vertex zone
/// at or past it is ignored.
[[nodiscard]] std::vector<std::uint8_t> zonesSeeingSky(const ubundle::Geometry& geometry, std::size_t zoneCount);

} // namespace uta::urender
