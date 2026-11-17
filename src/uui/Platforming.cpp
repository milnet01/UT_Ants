#include "uui/Platforming.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace uta::uui {

namespace {

constexpr double UNITS_PER_RADIAN = 65536.0 / (2 * std::numbers::pi);
constexpr double DEGREES_PER_RADIAN = 180.0 / std::numbers::pi;

/// Above the floor, so the disc is not lost in it.
constexpr double MARKER_LIFT = 1.5;
constexpr double MARKER_HALF_THICKNESS = 1;
constexpr std::array<float, 4> MARKER_COLOUR{0.25f, 0.85f, 1.0f, 0.45f};

} // namespace

urender::Shape landingMarker(std::array<double, 3> centre, double radius, double halfHeight) {
    urender::Shape shape;
    shape.kind = urender::Shape::Kind::Cylinder;
    shape.a = {static_cast<float>(centre[0]), static_cast<float>(centre[1]),
               static_cast<float>(centre[2] - halfHeight + MARKER_LIFT)};
    shape.radius = static_cast<float>(radius);
    shape.halfHeight = static_cast<float>(MARKER_HALF_THICKNESS);
    shape.colour = MARKER_COLOUR;
    return shape;
}

double tipTarget(std::array<double, 3> eye, double pitch, double yaw, std::array<double, 3> landing,
                 double horizontalFovDegrees, double aspect) {
    if (!(aspect > 0) || !(horizontalFovDegrees > 0)) return 0;
    const double dx = landing[0] - eye[0], dy = landing[1] - eye[1], dz = landing[2] - eye[2];
    // Into the aim's own frame: turn by -yaw about Z, then by -pitch about the side axis.
    const double y = yaw / UNITS_PER_RADIAN, p = pitch / UNITS_PER_RADIAN;
    const double level = dx * std::cos(y) + dy * std::sin(y);
    const double side = -dx * std::sin(y) + dy * std::cos(y);
    const double ahead = level * std::cos(p) + dz * std::sin(p);
    const double up = -level * std::sin(p) + dz * std::cos(p);
    if (ahead <= 0) return 0; // behind the eye: tipping down cannot show it
    const double tanHalfWidth = std::tan(horizontalFovDegrees / 2 / DEGREES_PER_RADIAN);
    if (std::abs(side) / ahead > tanHalfWidth) return 0; // off to one side
    const double tanHalfHeight = tanHalfWidth / aspect;
    const double below = std::atan(-up / ahead) * DEGREES_PER_RADIAN;
    const double limit = std::atan(IN_FRAME * tanHalfHeight) * DEGREES_PER_RADIAN;
    return std::clamp(below - limit, 0.0, MAX_TIP_DEGREES);
}

double easeTip(double tip, double target, double seconds) {
    if (!(seconds > 0)) return tip;
    return tip + (target - tip) * (1 - std::exp(-seconds / TIP_SECONDS));
}

} // namespace uta::uui
