#include "uaudio/Spatial.h"

#include <algorithm>
#include <cmath>

namespace uta::uaudio {

Placed place(const Listener& listener, std::array<float, 3> source, float radius) noexcept {
    const std::array<float, 3> to{source[0] - listener.position[0], source[1] - listener.position[1],
                                  source[2] - listener.position[2]};
    const float distance = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
    if (!std::isfinite(distance) || !std::isfinite(radius) || radius <= 0.0f) return {};
    Placed placed;
    placed.gain = std::max(0.0f, 1.0f - distance / radius);
    if (distance > 0.0f) {
        const float side = (to[0] * listener.right[0] + to[1] * listener.right[1] + to[2] * listener.right[2]) / distance;
        placed.pan = MAX_PAN * std::clamp(side, -1.0f, 1.0f) * std::min(1.0f, distance / PAN_NEAR);
    }
    return placed;
}

} // namespace uta::uaudio
