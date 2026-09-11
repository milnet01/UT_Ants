// The collision-tree checks ut-paths uses, under the names it uses.
//
// docs/specs/UTA-0112-baked-light-probes.md SS 4.10 moved Vec3, dot, length,
// isEmpty, Hit, trace and traceOut into src/ubake/CollisionQuery.h, so the
// baker can use them too. This header brings them back into uta::paths, so
// ut-paths' code and tests read them as they did -- docs/specs/
// UTA-0121-bot-path-seeds.md SS 4.4.
//
// `horizontal` stays here: it calls std::hypot, a library function UTA-0112
// SS 4.10 keeps out of the baker.

#pragma once

#include "ubake/CollisionQuery.h"

#include <cmath>

namespace uta::paths {

using ubake::dot;
using ubake::Hit;
using ubake::isEmpty;
using ubake::length;
using ubake::trace;
using ubake::traceOut;
using ubake::Vec3;

/// The distance between two points on X and Y alone.
[[nodiscard]] inline double horizontal(const Vec3& a, const Vec3& b) noexcept {
    return std::hypot(a.x - b.x, a.y - b.y);
}

} // namespace uta::paths
