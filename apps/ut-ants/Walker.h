// Walking in ut-ants -- docs/specs/UTA-0017-movement-rules.md SS 4.8. A body
// moving by UT99's rules on the fixed tick, driven by the same input the fly
// camera reads: W, A, S and D or the left stick move, a double tap dodges,
// Space jumps, Ctrl ducks, Shift walks, and the mouse or right stick looks.
//
// No SDL here, so the unit tests can drive it.

#pragma once

#include "FlyCamera.h"

#include "uworld/FixedTick.h"
#include "uworld/Movement.h"

#include <array>

namespace uta::client {

class Walker {
public:
    /// UT99's BaseEyeHeight: the eye above the body's centre (the PAWN line).
    static constexpr double EYE_HEIGHT = 27;

    /// A body with its centre at `location`, falling until it meets a floor.
    Walker(std::array<double, 3> location, double pitch, double yaw) noexcept;

    /// One frame: the look now, then every tick `seconds` makes due, each
    /// with this frame's keys.
    void update(const FlyInput& input, double seconds, const uworld::BodyWorld& world) noexcept;

    /// The view from the body's eye with a `fov`-degree field of view, drawn
    /// as far from the last tick's place to this one's as the clock's blend.
    [[nodiscard]] FlyCamera eye(double fov) const noexcept;

    [[nodiscard]] const uworld::MoveState& state() const noexcept { return state_; }

private:
    uworld::FixedTick clock_;
    uworld::DoubleTap tap_;
    uworld::MoveRules rules_;
    uworld::MoveState state_, previous_;
    /// The eye above the body's centre after the last two ticks: it lags a
    /// step up or down and eases back to EYE_HEIGHT (spec SS 4.8).
    double eyeHeight_ = EYE_HEIGHT, previousEyeHeight_ = EYE_HEIGHT;
    double pitch_, yaw_; ///< UT angle units, as FlyCamera keeps them
};

} // namespace uta::client
