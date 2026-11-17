// The visual platforming assists -- docs/specs/UTA-0021-landing-marker.md
// SS 4.3 and 4.4. uworld projects where a falling body lands; this draws a
// marker there and works out how far the drawn camera tips down to show it.
// Neither touches the simulation: the aim, and every command, stay as they are.

#pragma once

#include "urender/Shapes.h"

#include <array>

namespace uta::uui {

inline constexpr double MAX_TIP_DEGREES = 15;
/// The share of the way from the view's centre to its bottom edge a landing
/// must be within to count as in frame.
inline constexpr double IN_FRAME = 0.8;
/// Seconds of wall time the tip takes to close most of its gap.
inline constexpr double TIP_SECONDS = 0.12;

/// The landing marker: a translucent disc of `radius`, lying on the floor
/// under `centre`, a body `halfHeight` tall's centre on landing.
[[nodiscard]] urender::Shape landingMarker(std::array<double, 3> centre, double radius, double halfHeight);

/// The tip, in degrees down, that brings `landing` into frame from an eye at
/// `eye` aimed at `pitch` and `yaw` (UT angle units, 65536 to a turn, pitch
/// up positive): 0 when it is already in frame, above the view, behind the
/// eye, or outside the view's width; else just enough, at most
/// MAX_TIP_DEGREES. `aspect` is width over height; the field is horizontal.
[[nodiscard]] double tipTarget(std::array<double, 3> eye, double pitch, double yaw, std::array<double, 3> landing,
                               double horizontalFovDegrees, double aspect);

/// The tip after `seconds` of wall time, easing from `tip` toward `target`:
/// it closes 1 - exp(-seconds / TIP_SECONDS) of the gap, so never passes it.
[[nodiscard]] double easeTip(double tip, double target, double seconds);

} // namespace uta::uui
