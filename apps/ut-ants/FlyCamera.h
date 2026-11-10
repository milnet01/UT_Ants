// A free-flying camera -- UTA-0016. The mouse looks and the keys fly, with no
// gravity. Given the level's collision tree it stops at walls and slides along
// them, as UT99's pre-match spectator does (UTA-0158); matching UT99's movement
// is 0.2.0's.
//
// No SDL here, so the unit tests grade it against urender's own view.

#pragma once

#include "ubundle/Bundle.h"
#include "uinput/Input.h"
#include "urender/Renderer.h"

#include <array>
#include <cstdint>
#include <string>

namespace uta::client {

class FlyCamera {
public:
    static constexpr double SPEED = 800; ///< units a second; UT99 runs at 400
    static constexpr double FAST = 4;    ///< the multiplier the Walk action applies
    /// Just short of a quarter turn, so the view never tips over the top.
    static constexpr std::int32_t PITCH_LIMIT = 16000;
    /// UTA-0158: units kept between the camera and a wall, well beyond the
    /// view's near plane of 1, so the wall is never cut open.
    static constexpr double WALL_MARGIN = 8;

    /// UTA-0357: the field of view in degrees across the screen's width, on
    /// any screen, as UT469 draws it: FOV 90 on 16:9 is 90 across (measured
    /// by UT_MonsterHunt, 2026-10-14). UT99 opens at 90 (Default.ini's
    /// FovAngleDegrees); the player may change it within the limits.
    static constexpr double DEFAULT_FOV = 90;
    static constexpr double MIN_FOV = 60;
    static constexpr double MAX_FOV = 130;

    FlyCamera() = default;
    FlyCamera(std::array<float, 3> location, std::int32_t pitch, std::int32_t yaw) noexcept;

    [[nodiscard]] double fov() const noexcept { return fov_; }
    /// Clamped to MIN_FOV and MAX_FOV; a value that is not a number is ignored.
    void setFov(double fov) noexcept;

    /// Apply one frame's input over `seconds`. With `level`, the move stops
    /// WALL_MARGIN short of a wall and slides along it, the camera is then
    /// eased out to WALL_MARGIN from every surface near it (UTA-0174), and a
    /// move from empty space never ends in solid; a camera already in solid
    /// flies free, so it can leave. Without, it flies through anything.
    void update(const uinput::Frame& input, double seconds, const ubundle::CollisionTree* level = nullptr) noexcept;

    /// The camera for a `width` by `height` view, which the field of view
    /// spans across; a view with no pixels is taken as 4:3.
    [[nodiscard]] urender::Camera camera(std::uint32_t width, std::uint32_t height) const noexcept;

private:
    std::array<double, 3> location_{};
    double pitch_ = 0, yaw_ = 0; ///< UT angle units
    double fov_ = DEFAULT_FOV;
};

/// UTA-0190: `camera` as a line tools/ut-shot reads -- x y z pitch yaw roll
/// and the horizontal field of view in degrees, which a `width` by `height`
/// view makes of the camera's vertical one.
[[nodiscard]] std::string poseLine(const urender::Camera& camera, std::uint32_t width, std::uint32_t height);

} // namespace uta::client
