// Walking in ut-ants -- Walker.h.

#include "Walker.h"

#include <algorithm>
#include <cmath>

namespace uta::client {
namespace {

/// A stick or key pushed this far along an axis counts as the key held, for
/// a double tap.
constexpr float TAP_PRESS = 0.5f;

} // namespace

Walker::Walker(std::array<double, 3> location, double pitch, double yaw) noexcept : pitch_(pitch), yaw_(yaw) {
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
        state_ = uworld::tick(world, uworld::BodySize{}, uworld::MoveRules{}, state_, command, clock_.dt());
    }
}

FlyCamera Walker::eye(double fov) const noexcept {
    const uworld::Vec3 at = previous_.location + (state_.location - previous_.location) * clock_.blend();
    FlyCamera view({static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z + EYE_HEIGHT)},
                   static_cast<std::int32_t>(std::lround(pitch_)), static_cast<std::int32_t>(std::lround(yaw_)));
    view.setFov(fov);
    return view;
}

} // namespace uta::client
