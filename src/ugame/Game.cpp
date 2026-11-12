// The game's tick -- docs/specs/UTA-0019-weapon-set.md SS 4.5. The order of a
// tick is: each body switches weapon and moves; each fires; the shots fly
// and strike; then the damage and momentum owed land together.

#include "ugame/Game.h"

#include "ugame/Fire.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace uta::ugame {
namespace {

using detail::Owed;
using detail::Tick;

// Pawn.TakeDamage's handling of momentum, as UT99's script reads (UTA-0019
// SS 4.5). Unmeasured: INV-6's runs check them against the original.
constexpr double MASS = 100;               ///< Pawn.Mass: momentum over this is velocity
constexpr double SELF_MOMENTUM = 0.6;      ///< of a body's own momentum it takes
constexpr double WALKING_LIFT = 0.4;       ///< a walking body's push rises at least this much of its size

/// The order NextWeapon and PrevWeapon walk: by slot, the 0 key last, then
/// by file order.
int stepKey(const WeaponSet& set, int weapon) {
    const std::uint32_t slot = set.weapons[static_cast<std::size_t>(weapon)].slot;
    return static_cast<int>(slot == 0 ? 10 : slot) * 1000 + weapon;
}

/// The weapon a command asks for, or -1 for none.
int askedWeapon(const WeaponSet& set, const Body& body, const uworld::MoveCommand& command) {
    const int current = body.pending >= 0 ? body.pending : body.weapon;
    std::vector<int> carried;
    for (std::size_t i = 0; i < body.weapons.size(); ++i)
        if (body.weapons[i].has) carried.push_back(static_cast<int>(i));
    if (carried.empty()) return -1;
    std::ranges::sort(carried, {}, [&set](int weapon) { return stepKey(set, weapon); });

    if (command.weaponSlot >= 0) {
        std::vector<int> inSlot;
        for (const int weapon : carried)
            if (set.weapons[static_cast<std::size_t>(weapon)].slot == static_cast<std::uint32_t>(command.weaponSlot))
                inSlot.push_back(weapon);
        if (inSlot.empty()) return -1;
        // A second press of a slot's key moves to the next weapon in it.
        const auto at = std::ranges::find(inSlot, current);
        return at == inSlot.end() ? inSlot.front() : inSlot[static_cast<std::size_t>(at - inSlot.begin() + 1) % inSlot.size()];
    }
    if (command.weaponStep != 0) {
        const auto at = std::ranges::find(carried, current);
        const auto n = static_cast<std::ptrdiff_t>(carried.size());
        const std::ptrdiff_t from = at == carried.end() ? 0 : at - carried.begin();
        const std::ptrdiff_t to = ((from + command.weaponStep) % n + n) % n;
        return carried[static_cast<std::size_t>(to)];
    }
    return -1;
}

void switchWeapon(const WeaponSet& set, Body& body, const uworld::MoveCommand& command, double dt) {
    const int asked = askedWeapon(set, body, command);
    if (asked >= 0 && asked != (body.pending >= 0 ? body.pending : body.weapon)) {
        body.pending = asked;
        body.switchLeft = set.weapons[static_cast<std::size_t>(asked)].switchTime;
        body.charging = -1;
        body.charged = 0;
        body.stepHeld = 0;
        body.loaded = 0;
    }
    if (body.pending >= 0) {
        body.switchLeft -= dt;
        if (body.switchLeft <= 0) {
            body.weapon = body.pending;
            body.pending = -1;
            body.switchLeft = 0;
            body.refireLeft = 0;
        }
    }
}

/// The scale a charge held `held` seconds gives.
double chargeScale(const Charge& charge, double held) {
    const double full = std::min(held / charge.time, 1.0);
    return charge.min + (charge.max - charge.min) * full;
}

void fireBody(Tick& tick, int index, const uworld::MoveCommand& command) {
    Body& body = tick.game.bodies[static_cast<std::size_t>(index)];
    body.fireHeld = command.fire;
    body.altHeld = command.altFire;
    if (body.weapon < 0 || body.pending >= 0) {
        body.beamHeld = 0;
        return;
    }
    const Weapon& weapon = tick.set.weapons[static_cast<std::size_t>(body.weapon)];
    const auto modeOf = [&weapon](int which) -> const FireMode* {
        return which == 0 ? &weapon.primary : weapon.alt ? &*weapon.alt : nullptr;
    };

    // An idle weapon's refire is at rest on 0; one still cooling is above it.
    const bool idle = body.refireLeft <= 0;
    body.refireLeft -= tick.dt;
    bool fired = false, beamed = false;
    Carried& carried = body.weapons[static_cast<std::size_t>(body.weapon)];
    if (body.charging >= 0) {
        const FireMode& mode = *modeOf(body.charging);
        const Charge& charge = *mode.charge;
        const bool count = charge.scales == ChargeScales::Count;
        const bool held = body.charging == 0 ? command.fire : command.altFire;
        bool full = false;
        if (charge.step > 0) {
            // UT_BioRifle and UT_Eightball: a step passes once the hold
            // since the last one exceeds it, and spends a round.
            body.stepHeld += tick.dt;
            const bool room = count ? body.loaded < mode.count : body.charged < charge.time;
            if (room && body.stepHeld > charge.step && carried.ammo >= mode.ammo) {
                carried.ammo -= mode.ammo;
                if (count) ++body.loaded;
                else body.charged += body.stepHeld;
                body.stepHeld = 0;
                // The launcher fires once its last rocket is in or its ammo is out.
                full = count && (body.loaded >= mode.count || carried.ammo < mode.ammo);
            }
        } else {
            body.charged += tick.dt;
            full = count && body.charged >= charge.time;
        }
        // A melee goes off when it touches something; the hammer only once
        // its charge passes 1 (ImpactHammer.Firing).
        const bool ready = mode.rule != FireRule::Hammer || chargeScale(charge, body.charged) > 1;
        const bool touches = mode.archetype == Archetype::Melee && ready && detail::meleeTouches(tick, index, mode);
        if (!held || full || touches) {
            // A stepped count charge fires what it loaded: Fire.cpp floors
            // count x scale, exact for these small whole numbers.
            const double scale = count && charge.step > 0
                                     ? static_cast<double>(body.loaded) / mode.count
                                     : chargeScale(charge, body.charged);
            detail::fire(tick, index, mode, scale);
            body.refireLeft = mode.refire;
            body.charging = -1;
            body.charged = 0;
            body.stepHeld = 0;
            body.loaded = 0;
            fired = true;
        }
    } else if (body.refireLeft <= 0) {
        const int which = command.fire ? 0 : command.altFire ? 1 : -1;
        const FireMode* mode = which >= 0 ? modeOf(which) : nullptr;
        if (mode == nullptr) {
        } else if (mode->archetype == Archetype::Beam) {
            detail::beam(tick, index, *mode);
            fired = true;
            beamed = true;
        } else if (carried.ammo < mode->ammo) {
            // INV-7: too little ammo fires nothing and changes nothing.
        } else if (mode->charge) {
            // The press spends the first round (UT_BioRifle.AltFire, UT_Eightball.Fire).
            carried.ammo -= mode->ammo;
            body.charging = which;
            body.charged = 0;
            body.stepHeld = 0;
            body.loaded = 1;
        } else {
            detail::fire(tick, index, *mode, 1);
            body.refireLeft += mode->refire + (idle ? mode->refireLead : 0);
            fired = true;
        }
    }
    // A held trigger keeps its cadence; an idle one owes nothing.
    if (!fired && body.refireLeft < 0) body.refireLeft = 0;
    // A released beam is gone, and its store with it.
    if (!beamed) body.beamHeld = 0;
}

/// How far off the surface it struck a split shot's pieces start.
constexpr double SPLIT_STAND_OFF = 10;

/// A shot's strike: splash where it has one, else the body struck alone.
/// `away` points off what was struck: a split's pieces start along it from
/// `splitFrom`, the script's Location.
void explode(Tick& tick, std::size_t index, int struck, const Vec3& away, const Vec3& splitFrom) {
    Shot& shot = tick.game.shots[index];
    const Projectile& kind = tick.set.projectiles[shot.kind];
    tick.shotGone[index] = true;
    const double damage = kind.damage * shot.damageScale;
    const double momentum = kind.momentum * shot.damageScale;
    if (kind.splashRadius > 0 && (struck < 0 || !kind.bodyDirect)) {
        detail::splash(tick, shot.location, damage, momentum, kind.splashRadius, shot.owner, kind.splashUpMax);
    } else if (struck >= 0) {
        const double speed = uworld::length(shot.velocity);
        const Vec3 along = speed > 0 ? shot.velocity * (1 / speed) : Vec3{};
        tick.owed.push_back(Owed{struck, damage, along * momentum, shot.owner});
    }
    tick.game.events.push_back(Event{Event::Kind::Explosion, shot.location, shot.location, kind.splashRadius});
    if (kind.strike != Strike::Split) return;
    // flakslug.Explode: each piece starts a little off the surface struck and
    // flies on along the shell's own aim, turned by the piece's own draws.
    const std::uint32_t into = static_cast<std::uint32_t>(
        std::ranges::find(tick.set.projectiles, kind.splitInto, &Projectile::name) - tick.set.projectiles.begin());
    const double awayLength = uworld::length(away);
    const Vec3 start = splitFrom + (awayLength > 0 ? away * (SPLIT_STAND_OFF / awayLength) : Vec3{});
    const int owner = shot.owner;
    const std::int32_t yaw = shot.yaw, pitch = shot.pitch;
    for (std::uint32_t i = 0; i < kind.splitCount; ++i) {
        const std::int32_t yawOff = detail::spreadDraw(tick.game, kind.splitSpread);
        const std::int32_t pitchOff = detail::spreadDraw(tick.game, kind.splitSpread);
        detail::launch(tick, into, owner, start, yaw + yawOff, pitch + pitchOff, Vec3{}, 1, 1);
    }
}

/// How far a shot backs off a surface it bounces from or sticks to, so its
/// next trace does not start in solid.
constexpr double BACK_OFF = 0.5;

/// How far into a body a shot's hit is moved along its path, of the body's
/// radius (Pawn.AdjustHitLocation).
constexpr double HIT_INSET = 0.4;

void fly(Tick& tick, std::size_t index) {
    Shot& shot = tick.game.shots[index];
    const Projectile& kind = tick.set.projectiles[shot.kind];
    shot.age += tick.dt;
    if (shot.stuck) {
        if (shot.age >= kind.fuse) explode(tick, index, -1, Vec3{0, 0, 1}, shot.location);
        return;
    }
    if (shot.age >= shot.lifetime) {
        if (kind.expireExplodes) explode(tick, index, -1, shot.velocity * -1, shot.location);
        else tick.shotGone[index] = true;
        return;
    }
    shot.velocity.z += tick.rules.gravityZ * kind.gravity * tick.dt;
    // The engine's projectile physics: speed up, but no faster than its
    // most. A kind launched faster than that (the ripper's blade) is held
    // to it from its first move.
    shot.velocity = shot.velocity + shot.acceleration * tick.dt;
    const double speed = uworld::length(shot.velocity);
    if (kind.maxSpeed > 0 && speed > kind.maxSpeed) shot.velocity = shot.velocity * (kind.maxSpeed / speed);
    const Vec3 from = shot.location;
    const Vec3 delta = shot.velocity * tick.dt;
    const Vec3 to = from + delta;

    const uworld::BodyHit level = kind.radius > 0
                                      ? tick.world.sweep(uworld::BodySize{kind.radius, kind.radius}, from, to)
                                      : tick.world.trace(from, to);
    double first = level.startsInside ? 0 : level.fraction;
    int struck = -1;
    const std::vector<Body>& bodies = tick.game.bodies;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (static_cast<int>(i) == shot.owner || !bodies[i].alive()) continue;
        const double at = entersCylinder(from, to, bodies[i].move.location, bodies[i].size.radius + kind.radius,
                                         bodies[i].size.halfHeight + kind.radius);
        if (at <= first && at <= 1) {
            first = at;
            struck = static_cast<int>(i);
        }
    }
    if (struck >= 0) {
        // The script's Location is where the move ends, the level allowing;
        // its HitLocation is where the shot met the body.
        // Pawn.AdjustHitLocation moves it on into the body, along the shot.
        const double moved = level.startsInside ? 0 : std::min(level.fraction, 1.0);
        const Body& body = bodies[static_cast<std::size_t>(struck)];
        const double speed = uworld::length(shot.velocity);
        const Vec3 inset = speed > 0 ? shot.velocity * (HIT_INSET * body.size.radius / speed) : Vec3{};
        const Vec3 met = from + delta * first + inset;
        const Vec3 end = from + delta * moved;
        shot.location = kind.splashFromMoved ? end : met;
        // flakslug.ProcessTouch: off the body from where the shot met it, the
        // pieces starting from where its move ended.
        explode(tick, index, struck, met - body.move.location, end);
        return;
    }
    if (first >= 1) {
        shot.location = to;
        // The engine's falling physics gives an actor that does not bounce
        // the second half of a tick's gravity after its move.
        shot.velocity.z += tick.rules.gravityZ * kind.gravityAfter * tick.dt;
        return;
    }
    // The level: a shot starting in solid strikes where it is (SS 6).
    const double moved = uworld::length(delta) * first;
    const Vec3 along = moved > 0 ? delta * (1 / uworld::length(delta)) : Vec3{};
    shot.location = from + delta * first - along * std::min(moved, BACK_OFF);
    const Vec3 normal = level.normal;
    switch (kind.strike) {
    case Strike::Bounce:
        if (shot.bounces < kind.bounces && !level.startsInside) {
            ++shot.bounces;
            shot.velocity = (shot.velocity - normal * (2 * uworld::dot(shot.velocity, normal))) * kind.bounceKeep;
            return;
        }
        break;
    case Strike::Stick:
        if (!level.startsInside) {
            shot.stuck = true;
            shot.velocity = {};
            shot.age = 0;
            return;
        }
        break;
    case Strike::Explode:
    case Strike::Split: break;
    }
    explode(tick, index, -1, normal, shot.location);
}

/// The damage and momentum owed, landed in the order they were owed.
/// Damage is UT99's whole numbers: each step truncates, as its `int` does.
void land(Tick& tick) {
    for (const Owed& owed : tick.owed) {
        Body& body = tick.game.bodies[static_cast<std::size_t>(owed.body)];
        if (!body.alive()) continue;
        const bool self = owed.instigator == owed.body;
        Vec3 momentum = owed.momentum * (self ? SELF_MOMENTUM : 1.0);
        if (body.move.physics == uworld::Physics::Walking)
            momentum.z = std::max(momentum.z, WALKING_LIFT * uworld::length(momentum));
        double damage = std::trunc(std::trunc(owed.damage) * tick.set.damageScale);
        if (self) damage = std::trunc(damage * tick.set.selfDamage);
        body.health -= damage;
        if (uworld::length(momentum) > 0) body.move = uworld::push(body.move, momentum * (1 / MASS));
    }
}

} // namespace

Body bodyAt(const WeaponSet& set, const Vec3& location, std::int32_t yaw) {
    Body body;
    body.move.location = location;
    body.yaw = yaw;
    body.weapons.resize(set.weapons.size());
    int first = -1;
    for (std::size_t i = 0; i < set.weapons.size(); ++i) {
        body.weapons[i] = Carried{true, set.weapons[i].ammoStart};
        if (first < 0 || stepKey(set, static_cast<int>(i)) < stepKey(set, first)) first = static_cast<int>(i);
    }
    body.weapon = first;
    return body;
}

void giveAll(const WeaponSet& set, Body& body) {
    body.weapons.resize(set.weapons.size());
    for (std::size_t i = 0; i < set.weapons.size(); ++i) body.weapons[i] = Carried{true, set.weapons[i].ammoMax};
}

void tick(const uworld::BodyWorld& world, const WeaponSet& set, const uworld::MoveRules& rules, Game& game,
          std::span<const uworld::MoveCommand> commands, double dt) {
    game.events.clear();
    Tick tick{world, set, rules, game, dt, {}, {}, std::vector<bool>(game.shots.size(), false)};
    const std::size_t count = std::min(game.bodies.size(), commands.size());
    for (std::size_t i = 0; i < count; ++i) {
        Body& body = game.bodies[i];
        if (!body.alive()) continue;
        switchWeapon(set, body, commands[i], dt);
        body.move = uworld::tick(world, body.size, rules, body.move, commands[i], dt);
        body.yaw = commands[i].yaw;
        body.pitch = commands[i].pitch;
    }
    for (std::size_t i = 0; i < count; ++i)
        if (game.bodies[i].alive()) fireBody(tick, static_cast<int>(i), commands[i]);
    // A shot flies in the tick it was fired, and so does a piece split from
    // one, as UT99 ticks an actor spawned mid-frame in that same frame.
    for (std::size_t i = 0;;) {
        game.shots.insert(game.shots.end(), tick.spawned.begin(), tick.spawned.end());
        tick.spawned.clear();
        tick.shotGone.resize(game.shots.size(), false);
        if (i == game.shots.size()) break;
        for (; i < game.shots.size(); ++i)
            if (!tick.shotGone[i]) fly(tick, i);
    }
    land(tick);

    std::vector<Shot> kept;
    for (std::size_t i = 0; i < game.shots.size(); ++i)
        if (!tick.shotGone[i]) kept.push_back(game.shots[i]);
    game.shots = std::move(kept);
    ++game.ticks;
}

std::uint64_t stateHash(const Game& game) {
    // FNV-1a over each value's bytes, little-endian whatever the machine.
    std::uint64_t hash = 0xCBF29CE484222325u;
    const auto add = [&hash](std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            hash ^= (value >> shift) & 0xFF;
            hash *= 0x100000001B3u;
        }
    };
    const auto addDouble = [&add](double value) { add(std::bit_cast<std::uint64_t>(value)); };
    const auto addVec = [&addDouble](const Vec3& v) {
        addDouble(v.x);
        addDouble(v.y);
        addDouble(v.z);
    };
    add(game.ticks);
    add(game.random);
    add(game.bodies.size());
    for (const Body& body : game.bodies) {
        addVec(body.move.location);
        addVec(body.move.velocity);
        add(static_cast<std::uint64_t>(body.move.physics));
        add(static_cast<std::uint64_t>(body.move.dodge));
        addDouble(body.move.dodgeDoneTime);
        add(body.move.jumpHeld ? 1 : 0);
        add(static_cast<std::uint64_t>(body.move.airJumpsUsed));
        addDouble(body.health);
        for (const Carried& carried : body.weapons) {
            add(carried.has ? 1 : 0);
            add(carried.ammo);
        }
        add(static_cast<std::uint64_t>(body.weapon));
        add(static_cast<std::uint64_t>(body.pending));
        addDouble(body.switchLeft);
        addDouble(body.refireLeft);
        add(static_cast<std::uint64_t>(body.charging));
        addDouble(body.charged);
        addDouble(body.stepHeld);
        add(body.loaded);
        addDouble(body.beamAmmo);
        addDouble(body.beamHeld);
    }
    add(game.shots.size());
    for (const Shot& shot : game.shots) {
        add(shot.kind);
        add(static_cast<std::uint64_t>(shot.owner));
        addVec(shot.location);
        addVec(shot.velocity);
        addVec(shot.acceleration);
        add(static_cast<std::uint64_t>(shot.yaw));
        add(static_cast<std::uint64_t>(shot.pitch));
        addDouble(shot.lifetime);
        addDouble(shot.age);
        add(shot.bounces);
        add(shot.stuck ? 1 : 0);
        addDouble(shot.damageScale);
    }
    return hash;
}

} // namespace uta::ugame
