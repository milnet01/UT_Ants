// The simulation's fixed tick -- FixedTick.h.

#include "uworld/FixedTick.h"

#include <cmath>

namespace uta::uworld {

FixedTick::FixedTick(double gameSpeed) noexcept : dt_(gameSpeed / TICKS_PER_SECOND) {}

int FixedTick::advance(double wallSeconds) noexcept {
    if (!(wallSeconds > 0) || !std::isfinite(wallSeconds)) return 0;
    owed_ += wallSeconds * TICKS_PER_SECOND;
    const double whole = std::floor(owed_);
    owed_ -= whole;
    return whole > MAX_TICKS_PER_ADVANCE ? MAX_TICKS_PER_ADVANCE : static_cast<int>(whole);
}

} // namespace uta::uworld
