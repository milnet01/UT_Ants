// The camera line ut-shot, ut-bench and ut-ref read, and the limits a frame
// drawn from it must keep. One reader, so a change to the format reaches
// every tool (UTA-0423).
//
// Header-only, as Json.h is. A line is one camera in UT99's own units and
// angles: x y z pitch yaw roll horizontalFovDegrees. UT99's FOVAngle is
// horizontal; urender::Camera takes a vertical one, so it is converted at
// the frame's aspect.

#pragma once

#include "urender/Renderer.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace uta::tools {

/// The widest or tallest frame a tool will draw. Desktop devices report a
/// maxImageDimension2D of 16384; past it the frame cannot be made, and an
/// unchecked value sized the readback before the device said so (UTA-0224).
inline constexpr std::uint32_t MAX_DIMENSION = 16384;

/// Whether a horizontal field of view, in degrees, can be drawn: finite and
/// strictly between 0 and 180. tan() of half of anything else is zero,
/// negative or infinite (UTA-0224).
[[nodiscard]] inline bool usableHorizontalFov(double degrees) {
    return std::isfinite(degrees) && degrees > 0 && degrees < 180;
}

/// The vertical field of view a horizontal one gives on a frame `height` /
/// `width` tall.
[[nodiscard]] inline float verticalFovDegrees(double horizontalFovDegrees, double heightOverWidth) {
    const double halfRadians = horizontalFovDegrees * std::numbers::pi / 360.0;
    return static_cast<float>(std::atan(std::tan(halfRadians) * heightOverWidth) * 360.0 / std::numbers::pi);
}

/// Whether a line holds nothing but spaces, tabs and a Windows line end. Every
/// tool skips such a line rather than reading it as a camera.
[[nodiscard]] inline bool isBlankCameraLine(std::string_view line) {
    return line.find_first_not_of(" \t\r") == std::string_view::npos;
}

/// One camera line, read.
struct CameraLine {
    urender::Camera camera; ///< location and rotation; verticalFovDegrees is left alone
    double horizontalFovDegrees = 0;
};

/// The seven numbers of a camera line; anything after them is ignored.
/// nullopt where they do not read. The field of view is NOT checked here:
/// each tool names that refusal itself, through usableHorizontalFov.
[[nodiscard]] inline std::optional<CameraLine> readCameraLine(const std::string& line) {
    std::istringstream fields(line);
    CameraLine read;
    if (!(fields >> read.camera.location[0] >> read.camera.location[1] >> read.camera.location[2]
          >> read.camera.rotation[0] >> read.camera.rotation[1] >> read.camera.rotation[2]
          >> read.horizontalFovDegrees))
        return std::nullopt;
    return read;
}

} // namespace uta::tools
