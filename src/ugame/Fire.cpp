// The firing archetypes -- docs/specs/UTA-0019-weapon-set.md SS 4.4. Each
// takes its numbers from the mode it is given and none of its own: a number
// fixed here would be one INV-4 could not change from the file.

#include "ugame/Fire.h"

#include "core/Angles.h"

#include <algorithm>
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

Vec3 eyeOf(const Body& body) { return body.move.location + Vec3{0, 0, EYE_HEIGHT}; }

Vec3 startOf(const Body& body, const FireMode& mode) {
    // UT99's GetAxes of the view rotation: forward, right and up.
    const double sy = core::sineOf(body.yaw), cy = core::cosineOf(body.yaw);
    const double sp = core::sineOf(body.pitch), cp = core::cosineOf(body.pitch);
    const Vec3 forward{cp * cy, cp * sy, sp};
    const Vec3 right{-sy, cy, 0};
    const Vec3 up{-sp * cy, -sp * sy, cp};
    return eyeOf(body) + forward * mode.offset[0] + right * mode.offset[1] + up * mode.offset[2];
}

void launch(Tick& tick, std::uint32_t kind, int owner, const Vec3& at, std::int32_t yaw, std::int32_t pitch,
            const Vec3& firerVelocity, double damageScale) {
    const Projectile& projectile = tick.set.projectiles[kind];
    const std::int32_t yawTurn = spreadDraw(tick.game, projectile.turnRandom);
    const std::int32_t pitchTurn = spreadDraw(tick.game, projectile.turnRandom);
    const Vec3 direction = aimOf(yaw + yawTurn, pitch + pitchTurn);
    const double speed = projectile.speed + rangeDraw(tick.game, projectile.speedRandom);
    const double lifetime = projectile.lifetime + rangeDraw(tick.game, projectile.lifetimeRandom);
    Vec3 velocity = direction * speed + direction * (uworld::dot(firerVelocity, direction) * projectile.inherit);
    velocity.z += projectile.lift;
    tick.spawned.push_back(Shot{.kind = kind, .owner = owner, .location = at, .velocity = velocity,
                                .acceleration = direction * projectile.acceleration, .lifetime = lifetime,
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

void splash(Tick& tick, const Vec3& at, double damage, double momentum, double radius, int instigator) {
    if (radius <= 0) return;
    const std::vector<Body>& bodies = tick.game.bodies;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (!bodies[i].alive()) continue;
        const Vec3 away = bodies[i].move.location - at;
        const double distance = uworld::length(away);
        if (distance >= radius) continue;
        const double scale = 1 - std::max(0.0, (distance - bodies[i].size.radius) / radius);
        const Vec3 direction = distance > 0 ? away * (1 / distance) : Vec3{0, 0, 1};
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
    const SegmentHit hit = traceSegment(tick, start, start + aimOf(body.yaw, body.pitch) * mode.range, index, false);
    return hit.fraction <= 1;
}

void fire(Tick& tick, int index, const FireMode& mode, double scale) {
    Body& body = tick.game.bodies[static_cast<std::size_t>(index)];
    Carried& carried = body.weapons[static_cast<std::size_t>(body.weapon)];

    const bool scalesCount = mode.charge && mode.charge->scales == ChargeScales::Count;
    const double damageScale = mode.charge && !scalesCount ? scale : 1;
    const Vec3 start = startOf(body, mode);

    switch (mode.archetype) {
    case Archetype::InstantHit: {
        carried.ammo -= mode.ammo;
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
        carried.ammo -= mode.ammo;
        const Vec3 aim = aimOf(body.yaw, body.pitch);
        const Vec3 end = start + aim * mode.range;
        const SegmentHit hit = traceSegment(tick, start, end, index, false);
        const Vec3 stop = start + (end - start) * std::min(hit.fraction, 1.0);
        tick.game.events.push_back(Event{Event::Kind::Melee, start, stop, 0});
        if (hit.body >= 0)
            hitBody(tick, hit.body, index, start, end, mode.damage * damageScale, mode.momentum * damageScale);
        else if (hit.surface && mode.selfMomentum > 0)
            // The hammer against a wall or floor throws its firer back (S2).
            tick.owed.push_back(Owed{index, 0, aim * (-mode.selfMomentum * damageScale), index});
        break;
    }
    case Archetype::Projectile: {
        std::uint32_t count = mode.count;
        if (scalesCount)
            count = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::floor(mode.count * scale + 1e-9)));
        if (mode.ammo > 0) {
            if (scalesCount) count = std::min(count, carried.ammo / mode.ammo);
            carried.ammo -= scalesCount ? mode.ammo * count : mode.ammo;
        }
        const std::uint32_t kind = static_cast<std::uint32_t>(
            std::ranges::find(tick.set.projectiles, mode.projectile, &Projectile::name) - tick.set.projectiles.begin());
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::int32_t yawOff = spreadDraw(tick.game, mode.spread);
            const std::int32_t pitchOff = spreadDraw(tick.game, mode.spread);
            launch(tick, kind, index, start, body.yaw + yawOff, body.pitch + pitchOff, body.move.velocity, damageScale);
        }
        break;
    }
    case Archetype::Beam: beam(tick, index, mode); break;
    }
}

void beam(Tick& tick, int index, const FireMode& mode) {
    Body& body = tick.game.bodies[static_cast<std::size_t>(index)];
    Carried& carried = body.weapons[static_cast<std::size_t>(body.weapon)];
    if (mode.ammo > 0) {
        if (carried.ammo == 0) return; // INV-7
        body.beamAmmo += mode.ammo * tick.dt;
        const auto rounds = static_cast<std::uint32_t>(body.beamAmmo);
        body.beamAmmo -= rounds;
        carried.ammo -= std::min(rounds, carried.ammo);
    }
    const Vec3 start = startOf(body, mode);
    const Vec3 aim = aimOf(body.yaw, body.pitch);
    const Vec3 end = start + aim * mode.range;
    const SegmentHit hit = traceSegment(tick, start, end, index, false);
    tick.game.events.push_back(Event{Event::Kind::Beam, start, start + (end - start) * std::min(hit.fraction, 1.0), 0});
    // The original's beam stores its hold and hurts once the store passes
    // `hit-every`, so its whole-number damage is not cut to nothing a tick.
    double held = tick.dt;
    if (mode.hitEvery > 0) {
        body.beamHeld += tick.dt;
        if (body.beamHeld <= mode.hitEvery) return;
        held = body.beamHeld;
        body.beamHeld = 0;
    }
    if (hit.body >= 0) tick.owed.push_back(Owed{hit.body, mode.damage * held, aim * (mode.momentum * held), index});
}

} // namespace detail
} // namespace uta::ugame
