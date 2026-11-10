// Walking in ut-ants -- Walker.h.

#include "Walker.h"

#include <algorithm>
#include <cmath>

namespace uta::client {
namespace {

/// A stick or key pushed this far along an axis counts as the key held, for
/// a double tap.
constexpr float TAP_PRESS = 0.5f;

/// The eye's height above the body's centre after one tick from `before` to
/// `after`: UT99's PlayerPawn.UpdateEyeHeight, run once a tick. Walking, the
/// eye holds its height in the world while the body steps, then closes ten
/// times a second's share of the gap to EYE_HEIGHT; otherwise, and on the
/// tick it lands, it closes at least 35% of the gap.
double eyeAfterTick(double eye, const uworld::MoveState& before, const uworld::MoveState& after) noexcept {
    // The original's DeltaTime / TimeDilation: a tick in real seconds.
    const double realDt = 1.0 / uworld::TICKS_PER_SECOND;
    const bool justLanded = before.physics == uworld::Physics::Falling;
    if (after.physics != uworld::Physics::Walking || justLanded) {
        const double smooth = std::clamp(10 * realDt, 0.35, 1.0);
        return eye * (1 - smooth) + Walker::EYE_HEIGHT * smooth;
    }
    const double smooth = std::min(1.0, 10 * realDt);
    const double rise = after.location.z - before.location.z;
    eye = (eye - rise) * (1 - smooth) + Walker::EYE_HEIGHT * smooth;
    const double halfHeight = uworld::BodySize{}.halfHeight;
    const double drop = std::clamp(-rise, 0.0, uworld::MoveRules{}.maxStepHeight);
    return std::clamp(eye, -0.5 * halfHeight, halfHeight + drop);
}

} // namespace

Walker::Walker(std::array<double, 3> location, double pitch, double yaw) noexcept : pitch_(pitch), yaw_(yaw) {
    rules_.airJumps = 1; // a double jump (user, 2026-11-10)
    state_.location = {location[0], location[1], location[2]};
    state_.physics = uworld::Physics::Falling;
    previous_ = state_;
}

void Walker::update(const FlyInput& input, double seconds, const uworld::BodyWorld& world) noexcept {
    // The look as the fly camera's, so switching between them keeps the view.
    yaw_ = std::fmod(yaw_ + input.lookRight * FlyCamera::LOOK_UNITS_PER_PIXEL, 65536.0);
    pitch_ = std::clamp(pitch_ + input.lookUp * FlyCamera::LOOK_UNITS_PER_PIXEL, -double(FlyCamera::PITCH_LIMIT),
                        double(FlyCamera::PITCH_LIMIT));

    uworld::MoveCommand command;
    command.forward = input.forward;
    command.strafe = input.right;
    command.yaw = static_cast<std::int32_t>(std::lround(yaw_));
    command.jump = input.up > 0;
    command.duck = input.up < 0;
    command.walk = input.fast;
    const uworld::DirectionKeys keys{input.forward > TAP_PRESS, input.forward < -TAP_PRESS, input.right < -TAP_PRESS,
                                     input.right > TAP_PRESS};

    const int due = clock_.advance(seconds);
    for (int t = 0; t < due; ++t) {
        command.dodge = tap_.update(keys, clock_.dt(), uworld::dodgeReady(state_));
        previous_ = state_;
        state_ = uworld::tick(world, uworld::BodySize{}, rules_, state_, command, clock_.dt());
        previousEyeHeight_ = eyeHeight_;
        eyeHeight_ = eyeAfterTick(eyeHeight_, previous_, state_);
    }
}

FlyCamera Walker::eye(double fov) const noexcept {
    const double blend = clock_.blend();
    const uworld::Vec3 at = previous_.location + (state_.location - previous_.location) * blend;
    const double eyeHeight = previousEyeHeight_ + (eyeHeight_ - previousEyeHeight_) * blend;
    FlyCamera view({static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z + eyeHeight)},
                   static_cast<std::int32_t>(std::lround(pitch_)), static_cast<std::int32_t>(std::lround(yaw_)));
    view.setFov(fov);
    return view;
}

} // namespace uta::client
