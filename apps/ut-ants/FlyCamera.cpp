#include "FlyCamera.h"

#include "uworld/CollisionQuery.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace uta::client {

namespace {

double radians(double angle) noexcept { return angle * 2.0 * std::numbers::pi / 65536.0; }

/// UTA-0158: how many walls one move may meet and slide on. An edge between
/// two walls takes two, and a room's corner three.
constexpr int SLIDES = 3;

/// UTA-0174: passes of easing the camera out to WALL_MARGIN after a move. A
/// corner's second wall can move the camera toward its first, so one pass
/// may not settle both.
constexpr int EASE_PASSES = 2;

/// Where the camera goes to keep WALL_MARGIN from every surface near it, not
/// only from the one its move ran into: a move gliding past a corner hits
/// nothing, and ended beside the corner's face, close enough for the near
/// plane to cut it open. Rays of WALL_MARGIN in 26 directions find each nearby
/// plane; each pushes the camera out along its normal to WALL_MARGIN, one push
/// per plane however many rays saw it. The push is traced, so easing out never
/// enters solid either.
uworld::Vec3 easedOut(const ubundle::CollisionTree& level, uworld::Vec3 at) noexcept {
    for (int pass = 0; pass < EASE_PASSES; ++pass) {
        struct Push {
            uworld::Vec3 normal;
            double depth = 0;
        };
        std::array<Push, 26> pushes{};
        std::size_t count = 0;
        for (int x = -1; x <= 1; ++x) {
            for (int y = -1; y <= 1; ++y) {
                for (int z = -1; z <= 1; ++z) {
                    if (x == 0 && y == 0 && z == 0) continue;
                    uworld::Vec3 direction{double(x), double(y), double(z)};
                    direction = direction * (1 / uworld::length(direction));
                    const uworld::Hit hit = uworld::trace(level, at, at + direction * FlyCamera::WALL_MARGIN);
                    if (hit.fraction >= 1) continue;
                    // The gap to the plane square to it; `normal` faces the camera.
                    const double gap = hit.fraction * FlyCamera::WALL_MARGIN * -uworld::dot(direction, hit.normal);
                    const double depth = FlyCamera::WALL_MARGIN - gap;
                    if (depth <= 0) continue;
                    const auto same = std::find_if(pushes.begin(), pushes.begin() + count, [&](const Push& push) {
                        return uworld::dot(push.normal, hit.normal) > 0.999;
                    });
                    if (same != pushes.begin() + count) {
                        same->depth = std::max(same->depth, depth);
                    } else {
                        pushes[count++] = {hit.normal, depth};
                    }
                }
            }
        }
        if (count == 0) break;
        uworld::Vec3 push{};
        for (std::size_t i = 0; i < count; ++i) push = push + pushes[i].normal * pushes[i].depth;
        // A push another surface blocks goes halfway to it, never onto it: a
        // point on a plane can round to its solid side. That happens in
        // pockets narrower than twice WALL_MARGIN, where the pushes cannot all
        // be met.
        const uworld::Hit blocked = uworld::trace(level, at, at + push);
        at = at + push * (blocked.fraction >= 1 ? 1.0 : 0.5 * blocked.fraction);
    }
    return at;
}

/// How far past PAD_DEAD_ZONE `travel` is, rescaled to run from 0 to 1.
double pastDeadZone(double travel) noexcept {
    return std::clamp((travel - PAD_DEAD_ZONE) / (1.0 - PAD_DEAD_ZONE), 0.0, 1.0);
}

/// A stick's direction, its length past the dead zone raised to `power`. The
/// zone is round, so a diagonal push starts moving where a straight one does.
std::array<double, 2> stick(float x, float y, int power) noexcept {
    const double length = std::hypot(x, y);
    if (length == 0) return {};
    const double scale = std::pow(pastDeadZone(length), power) / length;
    return {x * scale, y * scale};
}

} // namespace

void addPad(FlyInput& input, const PadInput& pad, double seconds) noexcept {
    const auto move = stick(pad.leftX, pad.leftY, 1);
    input.right += static_cast<float>(move[0]);
    input.forward -= static_cast<float>(move[1]); // SDL's y grows downward
    input.up += static_cast<float>(pastDeadZone(pad.rise) - pastDeadZone(pad.sink));
    input.fast = input.fast || pad.fast;

    const auto look = stick(pad.rightX, pad.rightY, 2);
    const double pixels = PAD_LOOK_RATE * seconds / FlyCamera::LOOK_UNITS_PER_PIXEL;
    input.lookRight += static_cast<float>(look[0] * pixels);
    input.lookUp -= static_cast<float>(look[1] * pixels);
}

FlyCamera::FlyCamera(std::array<float, 3> location, std::int32_t pitch, std::int32_t yaw) noexcept
    : location_{location[0], location[1], location[2]},
      pitch_(std::clamp(pitch, -PITCH_LIMIT, PITCH_LIMIT)),
      yaw_(yaw) {}

void FlyCamera::update(const FlyInput& input, double seconds, const ubundle::CollisionTree* level) noexcept {
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
    uworld::Vec3 at{location_[0], location_[1], location_[2]};
    uworld::Vec3 remaining = uworld::Vec3{move[0], move[1], move[2]} * distance;
    // UTA-0158: without a level, or from inside solid, the camera flies free.
    if (level == nullptr || !uworld::isEmpty(*level, at)) {
        at = at + remaining;
    } else {
        const uworld::Vec3 start = at;
        for (int slide = 0; slide < SLIDES; ++slide) {
            const double along = uworld::length(remaining);
            if (along == 0) break;
            const uworld::Hit hit = uworld::trace(*level, at, at + remaining);
            if (hit.fraction >= 1) {
                at = at + remaining;
                break;
            }
            // Stop WALL_MARGIN short of the wall's plane, measured square to
            // it, so a move meeting the wall at a shallow angle stops further
            // back along itself. `normal` faces the camera.
            const double approach = -uworld::dot(hit.normal, remaining) / along;
            const double travel = approach > 0 ? std::max(0.0, hit.fraction * along - WALL_MARGIN / approach) : 0.0;
            at = at + remaining * (travel / along);
            // What is left of the move slides on, less its part into the wall.
            const uworld::Vec3 left = remaining * (1 - hit.fraction);
            remaining = left - hit.normal * uworld::dot(left, hit.normal);
        }
        at = easedOut(*level, at);
        // UTA-0174: a move from empty space never ends in solid. Rounding on a
        // plane could put it there, and a camera in solid flies free.
        if (!uworld::isEmpty(*level, at)) at = start;
    }
    location_ = {at.x, at.y, at.z};
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
