// The collision-tree checks ut-paths uses, under the names it uses.
//
// docs/specs/UTA-0112-baked-light-probes.md SS 4.10 moved Vec3, dot, length,
// isEmpty, Hit, trace and traceOut into ubake, so the baker can use them too,
// and UTA-0158 on into src/uworld/CollisionQuery.h, so the game can. This
// header brings them back into uta::paths, so ut-paths' code and tests read
// them as they did -- docs/specs/UTA-0121-bot-path-seeds.md SS 4.4.
//
// `horizontal` stays here: it calls std::hypot, a library function UTA-0112
// SS 4.10 keeps out of the baker.

#pragma once

#include "uworld/CollisionQuery.h"

#include <cmath>

namespace uta::paths {

using uworld::dot;
using uworld::Hit;
using uworld::isEmpty;
using uworld::length;
using uworld::trace;
using uworld::traceOut;
using uworld::Vec3;

/// The distance between two points on X and Y alone.
[[nodiscard]] inline double horizontal(const Vec3& a, const Vec3& b) noexcept {
    return std::hypot(a.x - b.x, a.y - b.y);
}

} // namespace uta::paths
