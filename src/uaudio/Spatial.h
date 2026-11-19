// Where a sound is heard from -- docs/specs/UTA-0480-map-sounds.md SS 4.2.
//
// Turns a listener and a source into the gain and pan a mixer voice takes.
// Loudness falls in a straight line with distance and is nothing at the
// radius, as UT99's did. Walls do not muffle. Pure: no mixer, no SDL.
#pragma once

#include <array>

namespace uta::uaudio {

struct Listener {
    std::array<float, 3> position{};
    /// The listener's right-hand direction, unit length.
    std::array<float, 3> right{1.0f, 0.0f, 0.0f};
};

struct Placed {
    float gain = 0.0f; ///< 0 to 1
    float pan = 0.0f;  ///< -MAX_PAN to MAX_PAN
};

/// A sound is never in one ear only.
inline constexpr float MAX_PAN = 0.8f;
/// World units within which the pan fades to the centre, so a sound does
/// not jump sides as the listener walks through it.
inline constexpr float PAN_NEAR = 64.0f;

/// `source` as `listener` hears it, audible out to `radius` world units. A
/// radius of 0 or less, or any value that is not finite, is silence.
[[nodiscard]] Placed place(const Listener& listener, std::array<float, 3> source, float radius) noexcept;

} // namespace uta::uaudio
