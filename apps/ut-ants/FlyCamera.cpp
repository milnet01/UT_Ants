#include "FlyCamera.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace uta::client {

namespace {

double radians(double angle) noexcept { return angle * 2.0 * std::numbers::pi / 65536.0; }

} // namespace

FlyCamera::FlyCamera(std::array<float, 3> location, std::int32_t pitch, std::int32_t yaw) noexcept
    : location_{location[0], location[1], location[2]},
      pitch_(std::clamp(pitch, -PITCH_LIMIT, PITCH_LIMIT)),
      yaw_(yaw) {}

void FlyCamera::update(const FlyInput& input, double seconds) noexcept {
    // Wrapped so a long session's yaw stays a small number.
    yaw_ = std::fmod(yaw_ + input.lookRight * LOOK_UNITS_PER_PIXEL, 65536.0);
    pitch_ = std::clamp(pitch_ + input.lookUp * LOOK_UNITS_PER_PIXEL, -double(PITCH_LIMIT),
                        double(PITCH_LIMIT));

    // urender's view looks along UT's rotated +X, with its right a quarter
    // turn of yaw further round -- tests/unit/RenderCameraTest.cpp.
    const double pitch = radians(pitch_), yaw = radians(yaw_);
    const std::array<double, 3> forward{std::cos(pitch) * std::cos(yaw),
                                        std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
    const std::array<double, 3> right{-std::sin(yaw), std::cos(yaw), 0};

    std::array<double, 3> move{};
    for (std::size_t i = 0; i < 3; ++i) move[i] = forward[i] * input.forward + right[i] * input.right;
    move[2] += input.up;
    // A diagonal is no faster than a straight line.
    if (const double length = std::hypot(move[0], move[1], move[2]); length > 1) {
        for (double& axis : move) axis /= length;
    }

    const double distance = SPEED * (input.fast ? FAST : 1) * seconds;
    for (std::size_t i = 0; i < 3; ++i) location_[i] += move[i] * distance;
}

urender::Camera FlyCamera::camera() const noexcept {
    urender::Camera camera;
    camera.location = {static_cast<float>(location_[0]), static_cast<float>(location_[1]),
                       static_cast<float>(location_[2])};
    camera.rotation = {static_cast<std::int32_t>(std::lround(pitch_)),
                       static_cast<std::int32_t>(std::lround(yaw_)), 0};
    return camera;
}

} // namespace uta::client
