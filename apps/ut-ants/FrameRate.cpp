// UTA-0452: the viewer's frame-rate counter. FrameRate.h says what it counts.

#include "FrameRate.h"

#include <cmath>
#include <format>

namespace uta::client {

bool FrameRate::add(double seconds) {
    if (!(seconds >= 0) || !std::isfinite(seconds)) return false; // a clock that ran backwards counts nothing
    elapsed_ += seconds;
    ++frames_;
    if (elapsed_ < WINDOW_SECONDS) return false;

    const double perSecond = frames_ / elapsed_;
    const double milliseconds = 1000 * elapsed_ / frames_;
    text_ = std::format("{} FPS {:.1f} MS", std::lround(perSecond), milliseconds);
    elapsed_ = 0;
    frames_ = 0;
    return true;
}

} // namespace uta::client
