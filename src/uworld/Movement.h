// A body's movement by UT99's rules -- docs/specs/UTA-0017-movement-rules.md.
// Walking, falling, air control, jumping, dodging, friction, stepping up and
// walking off ledges. A tick takes a body's state and one input command and
// returns the next state, using BodyWorld (UTA-0457) to stop at what it hits.
//
// Every rule and constant was fitted to the original's recordings (UTA-0456)
// at 60 and 85 fps; the spec's SS 4.3 says how each was measured. Positions
// are a body's centre, in UT99's units and on its axes; time is game seconds.

#pragma once

#include "uworld/BodyCollision.h"

#include <cstdint>

namespace uta::uworld {

enum class DodgeDir : std::uint8_t { None, Forward, Back, Left, Right };

/// One tick's movement input (design rule 12). Axes are -1 to 1; only their
/// direction counts, as in the original, which accelerates at its full rate
/// whichever keys are held.
struct MoveCommand {
    double forward = 0, strafe = 0; ///< strafe is positive to the right
    std::int32_t yaw = 0;           ///< the view's, 65536 to a turn
    bool jump = false;              ///< held; a jump starts on the tick it is pressed
    bool walk = false, duck = false;
    DodgeDir dodge = DodgeDir::None; ///< a dodge asked for this tick
};

enum class Physics : std::uint8_t { Walking, Falling };

/// Where a dodge is: none, in the air, or landed and not yet ready again.
enum class DodgeState : std::uint8_t { None, Active, Done };

struct MoveState {
    Vec3 location{}, velocity{};
    Physics physics = Physics::Walking;
    DodgeState dodge = DodgeState::None;
    double dodgeDoneTime = 0; ///< game seconds spent in DodgeState::Done
    bool jumpHeld = false;    ///< the last command's jump, so a held key jumps once
};

/// The movement constants. The defaults are the player's, each from the
/// recordings' PAWN line or fitted to their rows (spec SS 4.3).
struct MoveRules {
    double groundSpeed = 400, airSpeed = 400;
    double accelRate = 2048;
    double groundFriction = 8;   ///< braking takes 2 * friction * dt of the speed a tick
    double stopSpeed = 10;       ///< braking below this stops dead
    double gravityZ = -950;
    double jumpZ = 357.5;
    double airControl = 0.35;    ///< of accelRate, in the air
    double maxStepHeight = 25;
    double walkingPct = 0.3;     ///< of speed and acceleration, walking or ducking
    double dodgeClickTime = 0.25;
    double dodgeSpeedXY = 600, dodgeSpeedZ = 210;
    double dodgeLandingKeep = 0.1; ///< of the velocity, landing from a dodge
    double dodgeDoneTime = 0.35;   ///< after a dodge lands, before the next
    double walkableFloorZ = 0.7;   ///< the least normal.z a body can stand on
};

/// The next state: `command` applied to `state` over `dt` game seconds. A
/// pure function. A body that starts inside something does not move and keeps
/// its physics.
[[nodiscard]] MoveState tick(const BodyWorld& world, const BodySize& size, const MoveRules& rules,
                             const MoveState& state, const MoveCommand& command, double dt);

/// The direction keys held on one tick.
struct DirectionKeys {
    bool forward = false, back = false, left = false, right = false;
};

/// Turns direction-key presses into dodges: a second press of the same key
/// inside clickTime of the first, as the original's PlayerPawn does.
class DoubleTap {
public:
    explicit DoubleTap(double clickTime = MoveRules{}.dodgeClickTime) noexcept : clickTime_(clickTime) {}

    /// This tick's keys and `dt`; returns the dodge a second press makes.
    /// While `ready` is false (a dodge in the air or just landed) presses are
    /// forgotten, so no dodge chains off another.
    [[nodiscard]] DodgeDir update(const DirectionKeys& held, double dt, bool ready) noexcept;

private:
    double clickTime_;
    DodgeDir pending_ = DodgeDir::None; ///< the first press waiting for its second
    double left_ = 0;                   ///< of clickTime, for pending_
    DirectionKeys was_{};
};

/// Whether a body may start a dodge: DoubleTap's `ready`.
[[nodiscard]] inline bool dodgeReady(const MoveState& state) noexcept {
    return state.dodge == DodgeState::None;
}

} // namespace uta::uworld
