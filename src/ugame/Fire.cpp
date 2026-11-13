// The firing archetypes -- docs/specs/UTA-0019-weapon-set.md SS 4.4. Each
// takes its numbers from the mode it is given and none of its own: a number
// fixed here would be one INV-4 could not change from the file.

#include "ugame/Fire.h"

#include "core/Angles.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace uta::ugame {

Vec3 aimOf(std::int32_t yaw, std::int32_t pitch) {
    const double level = core::cosineOf(pitch);
    return {level * core::cosineOf(yaw), level * core::sineOf(yaw), core::sineOf(pitch)};
}

double entersCylinder(const Vec3& from, const Vec3& to, const Vec3& centre, double radius, double halfHeight) {
    constexpr double MISS = 2;
    const Vec3 d = to - from;
    double enter = 0, leave = 1;
    // Between the cylinder's top and bottom.
    if (d.z == 0) {
        if (from.z < centre.z - halfHeight || from.z > centre.z + halfHeight) return MISS;
    } else {
        double a = (centre.z - halfHeight - from.z) / d.z, b = (centre.z + halfHeight - from.z) / d.z;
        if (a > b) std::swap(a, b);
        enter = std::max(enter, a);
        leave = std::min(leave, b);
    }
    // Within its radius of the axis, seen from above.
    const double px = from.x - centre.x, py = from.y - centre.y;
    const double qa = d.x * d.x + d.y * d.y;
    const double qc = px * px + py * py - radius * radius;
    if (qa == 0) {
        if (qc > 0) return MISS;
    } else {
        const double qb = 2 * (px * d.x + py * d.y);
        const double disc = qb * qb - 4 * qa * qc;
        if (disc < 0) return MISS;
        const double root = std::sqrt(disc); // correctly rounded: allowed (design rule 9)
        enter = std::max(enter, (-qb - root) / (2 * qa));
        leave = std::min(leave, (-qb + root) / (2 * qa));
    }
    return enter <= leave ? enter : MISS;
}

namespace detail {

namespace {
/// ImpactHammer.Firing: how far ahead a held, charged hammer feels for a body.
constexpr double HAMMER_TOUCH = 25;
/// Seconds since its last touch, for a beam that has touched no body yet.
constexpr double BEAM_NEVER_HIT = 1e9;
} // namespace

std::uint64_t draw(Game& game) {
    std::uint64_t z = (game.random += 0x9E3779B97F4A7C15u);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9u;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBu;
    return z ^ (z >> 31);
}

std::int32_t spreadDraw(Game& game, std::uint32_t spread) {
    if (spread == 0) return 0;
    const std::uint64_t span = 2 * std::uint64_t{spread} + 1;
    return static_cast<std::int32_t>(draw(game) % span) - static_cast<std::int32_t>(spread);
}

Vec3 spreadAim(Game& game, std::int32_t yaw, std::int32_t pitch, std::uint32_t spread) {
    const std::int32_t yawOff = spreadDraw(game, spread);
    const std::int32_t pitchOff = spreadDraw(game, spread);
    return aimOf(yaw + yawOff, pitch + pitchOff);
}

double rangeDraw(Game& game, double range) {
    if (range == 0) return 0;
    // 53 bits as a fraction in [0, 1): exact, so the same on every compiler.
    const double unit = static_cast<double>(draw(game) >> 11) * 0x1p-53;
    return range * (2 * unit - 1);
}

Vec3 eyeOf(const Body& body) { return body.move.location + Vec3{0, 0, body.eyeHeight}; }

Vec3 startOf(const Body& body, const FireMode& mode) {
    // UT99's GetAxes of the view rotation: forward, right and up.
    const double sy = core::sineOf(body.yaw), cy = core::cosineOf(body.yaw);
    const double sp = core::sineOf(body.pitch), cp = core::cosineOf(body.pitch);
    const Vec3 forward{cp * cy, cp * sy, sp};
    const Vec3 right{-sy, cy, 0};
    const Vec3 up{-sp * cy, -sp * sy, cp};
    // PulseGun.ProjectileFire: each shot of a hold starts a turn further
    // round a circle about the offset, the first straight above it.
    const auto turned = static_cast<std::int32_t>(mode.spiralTurn * body.burst);
    const double across = mode.offset[1] - core::sineOf(turned) * mode.spiralRadius;
    const double above = mode.offset[2] + core::cosineOf(turned) * mode.spiralRadius;
    return eyeOf(body) + forward * mode.offset[0] + right * across + up * above;
}

void launch(Tick& tick, std::uint32_t kind, int owner, const Vec3& at, std::int32_t yaw, std::int32_t pitch,
            const Vec3& firerVelocity, double damageScale, double speedScale) {
    const Projectile& projectile = tick.set.projectiles[kind];
    const std::int32_t yawTurn = spreadDraw(tick.game, projectile.turnRandom);
    const std::int32_t pitchTurn = spreadDraw(tick.game, projectile.turnRandom);
    const Vec3 direction = aimOf(yaw + yawTurn, pitch + pitchTurn);
    const double speed = projectile.speed + rangeDraw(tick.game, projectile.speedRandom);
    const double lifetime = projectile.lifetime + rangeDraw(tick.game, projectile.lifetimeRandom);
    Vec3 velocity = direction * speed + direction * (uworld::dot(firerVelocity, direction) * projectile.inherit);
    velocity.z += projectile.lift;
    velocity = velocity * speedScale;
    tick.spawned.push_back(Shot{.kind = kind, .owner = owner, .location = at, .velocity = velocity,
                                .acceleration = direction * projectile.acceleration, .yaw = yaw, .pitch = pitch,
                                .lifetime = lifetime,
                                .damageScale = damageScale});
}

SegmentHit traceSegment(const Tick& tick, const Vec3& from, const Vec3& to, int ignore, bool combos) {
    SegmentHit first;
    const uworld::BodyHit level = tick.world.trace(from, to);
    if (level.fraction < 1 || level.startsInside) {
        first.fraction = level.fraction;
        first.normal = level.normal;
        first.surface = true;
    }
    const std::vector<Body>& bodies = tick.game.bodies;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (static_cast<int>(i) == ignore || !bodies[i].alive()) continue;
        const double at = entersCylinder(from, to, bodies[i].move.location, bodies[i].size.radius,
                                         bodies[i].size.halfHeight);
        if (at < first.fraction) first = SegmentHit{at, static_cast<int>(i), -1, {}, false};
    }
    if (combos) {
        const std::vector<Shot>& shots = tick.game.shots;
        for (std::size_t i = 0; i < shots.size(); ++i) {
            if (tick.shotGone[i]) continue;
            const Projectile& kind = tick.set.projectiles[shots[i].kind];
            if (kind.combo.empty()) continue;
            const double at = entersCylinder(from, to, shots[i].location, kind.radius, kind.radius);
            if (at < first.fraction) first = SegmentHit{at, -1, static_cast<int>(i), {}, false};
        }
    }
    return first;
}

void splash(Tick& tick, const Vec3& at, double damage, double momentum, double radius, int instigator,
            double upMax) {
    if (radius <= 0) return;
    const std::vector<Body>& bodies = tick.game.bodies;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (!bodies[i].alive()) continue;
        const Vec3 away = bodies[i].move.location - at;
        const double distance = uworld::length(away);
        if (distance >= radius) continue;
        const double scale = 1 - std::max(0.0, (distance - bodies[i].size.radius) / radius);
        Vec3 direction = distance > 0 ? away * (1 / distance) : Vec3{0, 0, 1};
        direction.z = std::min(direction.z, upMax);
        tick.owed.push_back(Owed{static_cast<int>(i), damage * scale, direction * (momentum * scale), instigator});
    }
}

void setOffCombo(Tick& tick, int shot, int instigator) {
    const Shot& ball = tick.game.shots[static_cast<std::size_t>(shot)];
    const Combo& combo = *tick.set.combo(tick.set.projectiles[ball.kind].combo);
    tick.shotGone[static_cast<std::size_t>(shot)] = true;
    splash(tick, ball.location, combo.damage, combo.momentum, combo.splashRadius, instigator);
    tick.game.events.push_back(Event{Event::Kind::Combo, ball.location, ball.location, combo.splashRadius});
}

namespace {

// SS 4.9's rocket and grenade launch, UT_Eightball's FireRockets state. Its
// angle steps by 1.0484 rad a projectile, summed as a 32-bit float; the
// sines and cosines of those sums are fixed here so no platform maths runs
// (INV-5).
constexpr std::array<double, 6> FAN_ANGLE{0.0, 1.0484000444412231, 2.0968000888824463,
                                          3.145200252532959, 4.193600177764893, 5.242000102996826};
constexpr std::array<double, 6> FAN_SIN{0.0, 0.8666260241298891, 0.8648204071720624,
                                        -0.0036075911178256744, -0.8684203629068389, -0.8630035957557444};
constexpr std::array<double, 6> FAN_COS{1.0, 0.4989582490557911, -0.5020813313983583,
                                        -0.9999934926219903, -0.4958286733225039, 0.5051977768286948};
constexpr double FAN_RADIUS = 4;      ///< rockets' circle
constexpr double CLUSTER_RADIUS = 7;  ///< grenades' circle
constexpr double FAN_STEP = 600;      ///< yaw per radian of the angle
constexpr double FAN_BACK = 4;        ///< each starts up to this far behind, at random
constexpr double FAN_SPEED = 0.1;     ///< a rocket after the first: speed x 1 +- this, at random
constexpr std::uint32_t CLUSTER_TURN = 750; ///< a grenade of several: pitch, yaw and roll each turned up to this

/// UT99's `V >> R`: `v` turned from the frame of `pitch`, `yaw` and `roll`
/// into the world's, as GetAxes builds it.
Vec3 turned(const Vec3& v, std::int32_t pitch, std::int32_t yaw, std::int32_t roll) {
    const double sp = core::sineOf(pitch), cp = core::cosineOf(pitch);
    const double sy = core::sineOf(yaw), cy = core::cosineOf(yaw);
    const double sr = core::sineOf(roll), cr = core::cosineOf(roll);
    const Vec3 x{cp * cy, cp * sy, sp};
    const Vec3 y{sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp};
    const Vec3 z{-(cr * sp * cy + sr * sy), -(cr * sp * sy - sr * cy), cr * cp};
    return x * v.x + y * v.y + z * v.z;
}

/// `count` projectiles of `kind` from `body`'s `start`, by `mode`'s rule.
void launchAll(Tick& tick, int index, const FireMode& mode, std::uint32_t kind, std::uint32_t count,
               const Vec3& start, double damageScale) {
    const Body& body = tick.game.bodies[static_cast<std::size_t>(index)];
    if (mode.rule == FireRule::None) {
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::int32_t yawOff = spreadDraw(tick.game, mode.spread);
            const std::int32_t pitchOff = spreadDraw(tick.game, mode.spread);
            launch(tick, kind, index, start, body.yaw + yawOff, body.pitch + pitchOff, body.move.velocity,
                   damageScale, 1);
        }
        return;
    }
    // UT_Eightball: one alone leaves from the start along the aim; several
    // leave around a circle, shifted 7.5 right and 7 down.
    const double sy = core::sineOf(body.yaw), cy = core::cosineOf(body.yaw);
    const double sp = core::sineOf(body.pitch), cp = core::cosineOf(body.pitch);
    const Vec3 forward{cp * cy, cp * sy, sp};
    const Vec3 right{-sy, cy, 0};
    const Vec3 up{-sp * cy, -sp * sy, cp};
    const bool several = count > 1;
    const double radius = mode.rule == FireRule::RocketFan ? FAN_RADIUS : CLUSTER_RADIUS;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t step = i % FAN_ANGLE.size();
        Vec3 at = start;
        if (several) {
            const double back = FAN_BACK * (rangeDraw(tick.game, 0.5) + 0.5);
            at = start - right * (FAN_SIN[step] * radius - 7.5) + up * (FAN_COS[step] * radius - 7) - forward * back;
        }
        if (mode.rule == FireRule::RocketFan) {
            // Left of the aim for the first three, right for the last two;
            // the fourth along it. The sum truncates to a whole angle.
            const double angle = FAN_ANGLE[step];
            double yaw = body.yaw;
            if (angle > 0 && angle < 3) yaw = body.yaw - angle * FAN_STEP;
            else if (angle > 3.5) yaw = body.yaw + (angle - 3) * FAN_STEP;
            const double speedScale = i > 0 ? 1 + rangeDraw(tick.game, FAN_SPEED) : 1;
            launch(tick, kind, index, at, static_cast<std::int32_t>(yaw), body.pitch, body.move.velocity, damageScale,
                   speedScale);
        } else {
            launch(tick, kind, index, at, body.yaw, body.pitch, body.move.velocity, damageScale, 1);
            if (several) {
                const std::int32_t pitch = spreadDraw(tick.game, CLUSTER_TURN);
                const std::int32_t yaw = spreadDraw(tick.game, CLUSTER_TURN);
                const std::int32_t roll = spreadDraw(tick.game, CLUSTER_TURN);
                Shot& shot = tick.spawned.back();
                shot.velocity = turned(shot.velocity, pitch, yaw, roll);
            }
        }
    }
}

/// What a hit on a body costs it: damage and momentum along the shot.
void hitBody(Tick& tick, int body, int instigator, const Vec3& from, const Vec3& to, double damage,
             double momentum) {
    const Vec3 direction = to - from;
    const double length = uworld::length(direction);
    const Vec3 unitDir = length > 0 ? direction * (1 / length) : Vec3{};
    tick.owed.push_back(Owed{body, damage, unitDir * momentum, instigator});
}

/// An instant hit's damage and momentum on `body`: a head's where the mode
/// has one and the hit is above the set's head height, else its own with
/// the random part drawn and a chance of a bigger push (SS 4.9).
void instantHit(Tick& tick, int body, int instigator, const Vec3& from, const Vec3& to, double fraction,
                const FireMode& mode, double scale) {
    const Body& target = tick.game.bodies[static_cast<std::size_t>(body)];
    const double atZ = from.z + (to.z - from.z) * fraction;
    double damage = mode.damage, momentum = mode.momentum;
    if (mode.headDamage && atZ - target.move.location.z > tick.set.headHeight * target.size.halfHeight) {
        damage = *mode.headDamage;
        momentum = mode.headMomentum;
    } else {
        if (mode.damageRandom > 0) damage += static_cast<double>(draw(tick.game) % (mode.damageRandom + 1u));
        if (mode.pushChance > 0 && rangeDraw(tick.game, 0.5) + 0.5 < mode.pushChance) momentum *= mode.pushScale;
    }
    hitBody(tick, body, instigator, from, to, damage * scale, momentum * scale);
}

} // namespace

bool meleeTouches(const Tick& tick, int index, const FireMode& mode) {
    const Body& body = tick.game.bodies[static_cast<std::size_t>(index)];
    const Vec3 start = startOf(body, mode);
    if (mode.rule == FireRule::Hammer) {
        // ImpactHammer.Firing: a body, never the level, this far ahead.
        const SegmentHit hit = traceSegment(tick, start, start + aimOf(body.yaw, body.pitch) * HAMMER_TOUCH, index, false);
        return hit.body >= 0;
    }
    const SegmentHit hit = traceSegment(tick, start, start + aimOf(body.yaw, body.pitch) * mode.range, index, false);
    return hit.fraction < 1; // a trace meeting nothing reports 1
}

void fire(Tick& tick, int index, const FireMode& mode, double scale) {
    Body& body = tick.game.bodies[static_cast<std::size_t>(index)];
    Carried& carried = body.weapons[static_cast<std::size_t>(body.weapon)];

    const bool scalesCount = mode.charge && mode.charge->scales == ChargeScales::Count;
    const double damageScale = mode.charge && !scalesCount ? scale : 1;
    const Vec3 start = startOf(body, mode);
    // A charge paid at its press and its steps (Game.cpp's fireBody).
    const std::uint32_t spend = mode.charge ? 0 : mode.ammo;

    switch (mode.archetype) {
    case Archetype::InstantHit: {
        carried.ammo -= spend;
        for (std::uint32_t pellet = 0; pellet < mode.pellets; ++pellet) {
            const Vec3 aim = spreadAim(tick.game, body.yaw, body.pitch, mode.spread);
            const Vec3 end = start + aim * mode.range;
            const SegmentHit hit = traceSegment(tick, start, end, index, true);
            const Vec3 stop = start + (end - start) * std::min(hit.fraction, 1.0);
            tick.game.events.push_back(Event{Event::Kind::InstantHit, start, stop, 0});
            if (hit.body >= 0) instantHit(tick, hit.body, index, start, end, hit.fraction, mode, damageScale);
            if (hit.shot >= 0) setOffCombo(tick, hit.shot, index);
        }
        break;
    }
    case Archetype::Melee: {
        carried.ammo -= spend;
        const Vec3 aim = aimOf(body.yaw, body.pitch);
        const Vec3 end = start + aim * mode.range;
        const SegmentHit hit = traceSegment(tick, start, end, index, false);
        const Vec3 stop = start + (end - start) * std::min(hit.fraction, 1.0);
        tick.game.events.push_back(Event{Event::Kind::Melee, start, stop, 0});
        // ImpactHammer's alt: the farther the hit, the harder.
        const double distance = mode.distanceScale > 0 ? uworld::length(stop - start) / mode.distanceScale : 1;
        const double effect = damageScale * distance;
        if (hit.body >= 0) {
            const bool inReach = mode.reach == 0 || uworld::length(stop - body.move.location) <= mode.reach;
            if (inReach) hitBody(tick, hit.body, index, start, end, mode.damage * effect, mode.momentum * effect);
        } else if (hit.surface && (mode.selfMomentum > 0 || mode.selfDamage > 0)) {
            // The hammer against a wall or floor hurts its firer by distance
            // and throws it back by charge too (S2), the charge counted as at
            // least 1 (ImpactHammer.ProcessTraceHit).
            const double push = mode.rule == FireRule::Hammer ? std::max(effect, 1.0) : effect;
            tick.owed.push_back(Owed{index, mode.selfDamage * distance, aim * (-mode.selfMomentum * push), index});
        }
        break;
    }
    case Archetype::Projectile: {
        std::uint32_t count = mode.count;
        if (scalesCount)
            count = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::floor(mode.count * scale + 1e-9)));
        if (scalesCount && mode.charge->step == 0 && mode.ammo > 0) {
            // A continuous count charge pays for the rest of its rounds now.
            count = std::min(count, 1 + carried.ammo / mode.ammo);
            carried.ammo -= mode.ammo * (count - 1);
        }
        carried.ammo -= spend;
        const std::uint32_t kind = static_cast<std::uint32_t>(
            std::ranges::find(tick.set.projectiles, mode.projectile, &Projectile::name) - tick.set.projectiles.begin());
        launchAll(tick, index, mode, kind, count, start, damageScale);
        break;
    }
    case Archetype::Beam: beam(tick, index, mode); break;
    }
}

void beam(Tick& tick, int index, const FireMode& mode) {
    Body& body = tick.game.bodies[static_cast<std::size_t>(index)];
    Carried& carried = body.weapons[static_cast<std::size_t>(body.weapon)];
    const bool begins = body.beamFor == 0;
    // INV-7: a press it cannot pay for starts nothing; a beam out of ammo stops.
    if (begins ? carried.ammo < mode.pressAmmo + mode.ammo : mode.ammo > 0 && carried.ammo == 0) return;
    if (begins) {
        carried.ammo -= mode.pressAmmo;
        body.beamSince = BEAM_NEVER_HIT;
    }
    // PulseGun.AltFiring: `ammo` each time the hold passes `ammo-every`,
    // what is past it kept toward the next.
    body.beamAmmo += tick.dt;
    if (mode.ammo > 0 && body.beamAmmo > mode.ammoEvery) {
        body.beamAmmo -= mode.ammoEvery;
        carried.ammo -= std::min(mode.ammo, carried.ammo);
    }
    // PBolt grows a piece a tick: `extend` a second, up to its range.
    body.beamFor += tick.dt;
    const double reach = mode.extend > 0 ? std::min(mode.range, mode.extend * body.beamFor) : mode.range;
    const Vec3 start = startOf(body, mode);
    const Vec3 aim = aimOf(body.yaw, body.pitch);
    const Vec3 end = start + aim * reach;
    const SegmentHit hit = traceSegment(tick, start, end, index, false);
    tick.game.events.push_back(Event{Event::Kind::Beam, start, start + (end - start) * std::min(hit.fraction, 1.0), 0});

    // PBolt.CheckBeam. The beam stores its hold on a body and hurts it once
    // the store passes `hit-every`, so its whole-number damage is not cut to
    // nothing a tick. A body it leaves takes what was stored; one it first
    // touches takes `first-hit` of hold at once, less if it touched one
    // within twice that.
    const auto hurt = [&](int victim, double held) {
        if (held > 0) tick.owed.push_back(Owed{victim, mode.damage * held, aim * (mode.momentum * held), index});
    };
    body.beamSince += tick.dt;
    if (hit.body != body.beamVictim) {
        if (body.beamVictim >= 0) hurt(body.beamVictim, body.beamHeld);
        else hurt(hit.body, std::min(0.5 * body.beamSince, mode.firstHit));
        body.beamHeld = 0;
        body.beamVictim = hit.body;
    }
    if (hit.body < 0) return;
    body.beamSince = 0;
    body.beamHeld += tick.dt;
    if (body.beamHeld > mode.hitEvery) {
        hurt(hit.body, body.beamHeld);
        body.beamHeld = 0;
    }
}

} // namespace detail
} // namespace uta::ugame
