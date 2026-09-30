// Strip lights -- docs/specs/UTA-0162-strip-lights.md SS 4.2. A row of
// identical lights along one fixture is marked as one strip, led by its
// lowest-numbered light, so the bake and the renderer light a band rather
// than a string of round pools.

#pragma once

#include "ubake/CollisionQuery.h"
#include "ubundle/Bundle.h"

#include <functional>
#include <vector>

namespace uta::ubake {

inline constexpr double STRIP_LINE_TOLERANCE = 16.0; ///< units from the row's line
inline constexpr double STRIP_GAP_REACH = 1.5;       ///< a gap's bound, in radii

/// Whether something solid crosses the straight line between two points.
using Blocked = std::function<bool(const Vec3& from, const Vec3& to)>;

/// Marks each row of `lights` as one strip, in place. `lights` is strictly
/// ascending by exportIndex, as LITE holds it, and carries no strip yet.
/// `blocked` is asked of each member and the next along the row (rule 5).
void markStrips(std::vector<ubundle::Light>& lights, const Blocked& blocked);

} // namespace uta::ubake
