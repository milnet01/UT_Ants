// A body's movement by UT99's rules -- Movement.h.
//
// NO PLATFORM MATHS LIBRARY (docs/design.md SS Determinism): the view's axes
// come from core/Angles.h, and lengths from std::sqrt, which IEEE 754 rounds
// correctly.

#include "uworld/Movement.h"

#include "core/Angles.h"


namespace uta::uworld {
namespace {

Vec3 flat(const Vec3& v) { return {v.x, v.y, 0}; }

/// The view's forward and right axes, level with the floor.
struct Axes {
    Vec3 forward, right;
};

Axes axesOf(std::int32_t yaw) {
    const double c = core::cosineOf(yaw), s = core::sineOf(yaw);
    return {{c, s, 0}, {-s, c, 0}};
}

/// The direction the command's keys push, level and of unit length; zero when
/// none are held.
Vec3 pushOf(const MoveCommand& command) {
    const Axes axes = axesOf(command.yaw);
    const Vec3 push = axes.forward * command.forward + axes.right * command.strafe;
    const double l = length(push);
    return l > 0 ? push * (1.0 / l) : Vec3{};
}

/// Walking's new level velocity: braking when no key is held, else turning
/// towards the push at the old speed and accelerating along it, capped.
/// Fitted to run-release, strafe, walk and duck-walk (spec SS 4.3).
Vec3 walkingVelocity(const MoveRules& rules, const Vec3& old, const Vec3& push, bool slow, double dt) {
    const Vec3 v = flat(old);
    if (push == Vec3{}) {
        const Vec3 braked = v - v * (2 * rules.groundFriction * dt);
        if (dot(braked, v) < 0 || dot(braked, braked) < rules.stopSpeed * rules.stopSpeed) return {};
        return braked;
    }
    const double scale = slow ? rules.walkingPct : 1.0;
    const Vec3 turned = v - (v - push * length(v)) * (rules.groundFriction * dt);
    const Vec3 sped = turned + push * (rules.accelRate * scale * dt);
    const double cap = rules.groundSpeed * scale, l = length(sped);
    return l > cap ? sped * (cap / l) : sped;
}

/// Falling's new level velocity: air control along the push. Below airSpeed
/// it adds freely; at or above, it turns the velocity without speeding it up.
/// Fitted to running-jump, air-strafe and the dodges (spec SS 4.3).
Vec3 airVelocity(const MoveRules& rules, const Vec3& old, const Vec3& push, double dt) {
    const Vec3 v = flat(old);
    const Vec3 sped = v + push * (rules.airControl * rules.accelRate * dt);
    const double before = length(v), after = length(sped);
    if (before >= rules.airSpeed && after > before) return sped * (before / after);
    return sped;
}

/// A dodge's launch velocity: dodgeSpeedXY along the dodge, keeping the
/// velocity across it, and dodgeSpeedZ up.
Vec3 dodgeVelocity(const MoveRules& rules, const Vec3& v, DodgeDir dir, std::int32_t yaw) {
    const Axes axes = axesOf(yaw);
    const bool along = dir == DodgeDir::Forward || dir == DodgeDir::Back;
    const Vec3 axis = along ? axes.forward : axes.right;
    const Vec3 across = along ? axes.right : axes.forward;
    const double sign = dir == DodgeDir::Forward || dir == DodgeDir::Right ? 1.0 : -1.0;
    Vec3 out = axis * (sign * rules.dodgeSpeedXY) + across * dot(v, across);
    out.z = rules.dodgeSpeedZ;
    return out;
}

bool walkable(const MoveRules& rules, const BodyHit& hit) {
    return hit.fraction < 1 && !hit.startsInside && hit.normal.z >= rules.walkableFloorZ;
}

double levelDistance(const Vec3& a, const Vec3& b) { return length(flat(b - a)); }

/// Falling for one tick from `s`, whose velocity already holds any jump or
/// dodge. Moves by the mean of the old and new velocity, as the original does.
MoveState fall(const BodyWorld& world, const BodySize& size, const MoveRules& rules, MoveState s,
               const Vec3& push, double dt) {
    const Vec3 old = s.velocity;
    Vec3 v = airVelocity(rules, old, push, dt);
    v.z = old.z + rules.gravityZ * dt;
    const BodyMove move = world.slide(size, s.location, (old + v) * (0.5 * dt));
    if (!move.hits.empty() && move.hits.front().startsInside) {
        s.velocity = v;
        return s;
    }
    bool landed = false;
    for (const BodyHit& hit : move.hits) {
        if (hit.fraction >= 1) continue;
        if (walkable(rules, hit)) {
            landed = true;
        } else {
            // Nothing keeps pushing into what stopped it.
            const double into = dot(v, hit.normal);
            if (into < 0) v = v - hit.normal * into;
        }
    }
    s.location = move.end;
    s.velocity = v;
    if (landed) {
        s.physics = Physics::Walking;
        s.airJumpsUsed = 0;
        if (s.dodge == DodgeState::Active) {
            s.velocity = s.velocity * rules.dodgeLandingKeep;
            s.dodge = DodgeState::Done;
            s.dodgeDoneTime = 0;
        }
    }
    return s;
}

/// Walking for one tick: along the floor, up a step if a wall stops it, then
/// down to the floor; no floor in reach starts a fall with the velocity kept.
MoveState walk(const BodyWorld& world, const BodySize& size, const MoveRules& rules, MoveState s,
               const Vec3& push, bool slow, double dt) {
    const Vec3 v = walkingVelocity(rules, s.velocity, push, slow, dt);
    const Vec3 start = s.location;
    const Vec3 delta = v * dt;
    const BodyMove plain = world.slide(size, start, delta);
    if (!plain.hits.empty() && plain.hits.front().startsInside) {
        s.velocity = v;
        return s;
    }
    Vec3 end = plain.end;
    // Short of the whole move: a wall, or a lip of floor the body's rim
    // catches edge-on, which a slide along it cannot pass.
    const bool blocked = levelDistance(start, end) < length(flat(delta)) - SKIN / 8;
    if (blocked) {
        // A step: rise, move, and come back down. Meeting nothing on the way
        // down, the floor probe below finds the floor, or starts a fall.
        const Vec3 rise{0, 0, rules.maxStepHeight};
        const Vec3 raised = start + rise * world.sweep(size, start, start + rise).fraction;
        const BodyMove high = world.slide(size, raised, delta);
        const Vec3 drop = Vec3{0, 0, start.z - raised.z};
        const BodyHit down = world.sweep(size, high.end, high.end + drop);
        const bool lands = !down.startsInside && (down.fraction >= 1 || walkable(rules, down));
        if (lands && levelDistance(start, high.end) > levelDistance(start, end) + SKIN)
            end = high.end + drop * down.fraction;
    }

    s.velocity = v;
    // Stopped short, the velocity is what the body managed (spec SS 4.4).
    if (blocked && dt > 0) s.velocity = flat(end - start) * (1.0 / dt);

    const Vec3 reach{0, 0, -rules.maxStepHeight};
    BodyHit floor = world.sweep(size, end, end + reach);
    if (floor.fraction < 1 && !floor.startsInside && !walkable(rules, floor)) {
        // A wall whose foot leans out stops the probe before the floor under
        // a body walking beside it: step SKIN off the wall and look again.
        const Vec3 off = flat(floor.normal);
        const Vec3 nudged = end + off * (SKIN / length(off));
        const BodyHit again = world.sweep(size, nudged, nudged + reach);
        if (walkable(rules, again)) {
            end = nudged;
            floor = again;
        }
    }
    s.location = end;
    if (walkable(rules, floor)) {
        s.location = end + reach * floor.fraction;
    } else if (!floor.startsInside) {
        s.physics = Physics::Falling;
    }
    return s;
}

} // namespace

MoveState tick(const BodyWorld& world, const BodySize& size, const MoveRules& rules, const MoveState& state,
               const MoveCommand& command, double dt) {
    MoveState s = state;
    const bool jumpPressed = command.jump && !state.jumpHeld;
    s.jumpHeld = command.jump;

    if (s.dodge == DodgeState::Done) {
        s.dodgeDoneTime += dt;
        if (s.dodgeDoneTime > rules.dodgeDoneTime) s.dodge = DodgeState::None;
    }

    const Vec3 push = pushOf(command);
    if (s.physics == Physics::Walking) {
        if (command.dodge != DodgeDir::None && !command.duck && s.dodge == DodgeState::None) {
            s.velocity = dodgeVelocity(rules, s.velocity, command.dodge, command.yaw);
            s.physics = Physics::Falling;
            s.dodge = DodgeState::Active;
        } else if (jumpPressed) {
            s.velocity = flat(s.velocity);
            s.velocity.z = rules.jumpZ;
            s.physics = Physics::Falling;
        }
    } else if (jumpPressed && s.airJumpsUsed < rules.airJumps) {
        // The same rise as a jump from the floor, whatever the fall's speed.
        s.velocity.z = rules.jumpZ;
        ++s.airJumpsUsed;
    }
    if (s.physics == Physics::Falling) return fall(world, size, rules, s, push, dt);
    return walk(world, size, rules, s, push, command.walk || command.duck, dt);
}

DodgeDir DoubleTap::update(const DirectionKeys& held, double dt, bool ready) noexcept {
    const DirectionKeys was = was_;
    was_ = held;
    if (!ready) {
        pending_ = DodgeDir::None;
        return DodgeDir::None;
    }
    if (pending_ != DodgeDir::None) {
        left_ -= dt;
        if (left_ < 0) pending_ = DodgeDir::None;
    }
    const auto pressed = [](bool now, bool before) { return now && !before; };
    DodgeDir press = DodgeDir::None;
    if (pressed(held.forward, was.forward)) press = DodgeDir::Forward;
    else if (pressed(held.back, was.back)) press = DodgeDir::Back;
    else if (pressed(held.left, was.left)) press = DodgeDir::Left;
    else if (pressed(held.right, was.right)) press = DodgeDir::Right;
    if (press == DodgeDir::None) return DodgeDir::None;
    if (press == pending_) {
        pending_ = DodgeDir::None;
        return press;
    }
    pending_ = press;
    left_ = clickTime_;
    return DodgeDir::None;
}

} // namespace uta::uworld
