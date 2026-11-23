// The game's tick -- docs/specs/UTA-0019-weapon-set.md SS 4.5. The order of a
// tick is: each body switches weapon and moves; each fires; the shots fly
// and strike; then the damage and momentum owed land together.

#include "ugame/Game.h"

#include "ugame/Fire.h"

#include "core/Angles.h"

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
// UTA-0502: a switch asked for while a weapon comes up.
constexpr double SELECT_CUT = 0.75;        ///< Weapon.Active.PutDown: under this share of its select, it goes down at once
constexpr double SELECT_TWEEN = 0.4;       ///< TournamentWeapon.TweenDown: then in this many seconds per share run

/// The order NextWeapon and PrevWeapon walk: by slot, the 0 key last, then
/// by file order.
int stepKey(const WeaponSet& set, int weapon) {
    const std::uint32_t slot = set.weapons[static_cast<std::size_t>(weapon)].slot;
    return static_cast<int>(slot == 0 ? 10 : slot) * 1000 + weapon;
}

/// The weapon a body will end its switch with, or holds when not switching.
int switchingTo(const Body& body) {
    return body.next >= 0 ? body.next : body.pending >= 0 ? body.pending : body.weapon;
}

/// The weapon a command asks for, or -1 for none. A wheel's pick of a
/// carried weapon wins over the slot and the step (UTA-0020 SS 4.5).
int askedWeapon(const WeaponSet& set, const Body& body, const uworld::MoveCommand& command) {
    const int current = switchingTo(body);
    const std::vector<int> carried = wheelWeapons(set, body);
    if (carried.empty()) return -1;
    if (std::ranges::find(carried, int{command.weaponPick}) != carried.end()) return command.weaponPick;

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

/// What the weapon put down leaves behind: no wind-up, hold or charge.
void dropWeaponState(Body& body) {
    body.winding = -1;
    body.windLeft = 0;
    body.holding = -1;
    body.heldFor = 0;
    body.repressLeft = 0;
    body.charging = -1;
    body.charged = 0;
    body.stepHeld = 0;
    body.loaded = 0;
    // UTA-0468: the weapon put down looks for no target, and its second gun stops.
    body.lockLooking = false;
    body.lockCandidate = body.lockTarget = -1;
    body.secondDue = -1;
    body.secondWinding = body.secondHolding = -1;
    body.secondRefire = 0;
}

void switchWeapon(const WeaponSet& set, Game& game, int index, const uworld::MoveCommand& command, double dt) {
    Body& body = game.bodies[static_cast<std::size_t>(index)];
    // SS 4.11: a switch asked for while a shell is steered, or while the
    // weapon recovers, is dropped (Weapon.PutDown's flag, which only Finish reads).
    if (body.guide != Body::Guide::None) return;
    const int asked = askedWeapon(set, body, command);
    bool pressed = false;
    if (asked >= 0 && asked != switchingTo(body)) {
        // A weapon already going down keeps going; only what comes up changes.
        // One coming up goes down at once early in its select, the select
        // tweened back; late in it, it finishes, then goes down whole
        // (UTA-0502). One loading after its select goes down whole at once.
        if (body.pending >= 0 && body.switching == Body::Switch::BringUp) {
            const Weapon& up = set.weapons[static_cast<std::size_t>(body.pending)];
            const double rate = up.selectRate > 0 ? up.selectRate : up.bringUp > 0 ? 1 / up.bringUp : 0;
            const double risen = up.bringUp - body.switchLeft; // seconds up, before this tick
            if (rate > 0 && risen * rate < SELECT_CUT) {
                // The down state starts a tick on, from the select as this tick leaves it.
                body.switching = Body::Switch::PutDown;
                body.switchLeft = SELECT_TWEEN * rate * (risen + dt);
                body.next = -1;
                pressed = true;
            } else {
                body.next = asked;
            }
        } else if (body.pending < 0 || body.switching != Body::Switch::PutDown) {
            body.switching = Body::Switch::PutDown;
            body.switchLeft = body.weapon >= 0 ? set.weapons[static_cast<std::size_t>(body.weapon)].putDown : 0;
            pressed = true;
        }
        if (body.next < 0) body.pending = asked;
        dropWeaponState(body);
    }
    if (body.pending < 0) return;
    const Weapon& coming = set.weapons[static_cast<std::size_t>(body.pending)];
    // UTA-0501, as recorded at 30, 85 and 120 fps: the put-down counts from
    // the tick after the press. The tick it runs out on is the bring-up's
    // first, as the old weapon's AnimEnd raises the new one within the
    // tick. A later wait counts from the tick after the one before ends,
    // and the weapon fires on the tick after the last, as the Active
    // state's FinishAnim resumes a tick on.
    if (body.switching == Body::Switch::PutDown) {
        if (!pressed) body.switchLeft -= dt;
        if (body.switchLeft > 0) return;
        game.cues.push_back(Cue{Cue::Kind::Select, index, static_cast<std::uint32_t>(body.pending), -1,
                                body.move.location});
        body.previous = body.weapon; // SS 4.10: Weapon.RaiseUp's OldWeapon
        body.weapon = body.pending;
        body.switching = Body::Switch::BringUp;
        body.switchLeft = coming.bringUp - dt;
        return;
    }
    if (body.switchLeft > 0) {
        body.switchLeft -= dt;
        return;
    }
    if (body.switching == Body::Switch::BringUp && body.next >= 0) {
        // Weapon.Active: the select done, a switch asked for during it sends
        // the weapon down whole, before any loading, from the tick it ran out.
        body.switching = Body::Switch::PutDown;
        body.switchLeft = coming.putDown - dt;
        body.pending = body.next;
        body.next = -1;
        return;
    }
    if (body.switching == Body::Switch::BringUp && coming.postSelect > 0) {
        body.switching = Body::Switch::PostSelect;
        body.switchLeft = coming.postSelect - dt;
        return;
    }
    body.pending = -1;
    body.switching = Body::Switch::PutDown;
    body.switchLeft = 0;
    body.refireLeft = 0;
}

/// The scale a charge held `held` seconds gives.
double chargeScale(const Charge& charge, double held) {
    const double full = std::min(held / charge.time, 1.0);
    return charge.min + (charge.max - charge.min) * full;
}

/// UTA-0468, UT_Eightball: a mode about to fire keeps its weapon's lock only
/// where it locks and still finds the locked body (FireRockets.BeginState,
/// NormalFire.AnimEnd); another mode, such as the grenades, drops it.
void keepLock(const detail::Tick& tick, int index, const FireMode& mode) {
    Body& body = tick.game.bodies[static_cast<std::size_t>(index)];
    if (body.lockTarget < 0) return;
    if (mode.lockProjectile.empty() || detail::lockPick(tick, index, mode) != body.lockTarget) body.lockTarget = -1;
}

/// UTA-0468, UT_Eightball's Idle state: while the weapon in hand is idle it
/// looks for a target on entering, then every lock-every, the time past
/// each look counting toward the next (the actor's timer); the entering
/// tick counts. The same body twice running is locked on; a look that
/// loses the body it found keeps the lock (Idle.Timer), which the shot's
/// own look then drops.
void lookForLock(const detail::Tick& tick, int index, const FireMode* mode, bool idle) {
    Body& body = tick.game.bodies[static_cast<std::size_t>(index)];
    if (mode == nullptr || !idle) {
        body.lockLooking = false;
        return;
    }
    if (!body.lockLooking) {
        body.lockLooking = true;
        body.lockClock = 0;
        body.lockCandidate = detail::lockPick(tick, index, *mode);
        body.lockTarget = -1;
    }
    body.lockClock += tick.dt;
    if (body.lockClock < mode->lockEvery) return;
    body.lockClock -= mode->lockEvery;
    const int found = detail::lockPick(tick, index, *mode);
    if (found == body.lockCandidate) body.lockTarget = found;
    else if (body.lockCandidate < 0 || found >= 0) body.lockTarget = -1;
    body.lockCandidate = found;
}

/// UTA-0468, Enforcer's slave: a doubled weapon's second gun. Set off
/// `doubleDelay` after the first gun's shot (NormalFire's timer) or its
/// wind-up's start (AltFiring's), it fires that mode as the first gun
/// would from idle -- a shot where it is idle, or its own wind-up first --
/// and then a shot every refire while that trigger is held, on its own
/// cadence (Weapon.Idle fires again at once for a held trigger). It shares
/// the ammo.
void fireSecond(detail::Tick& tick, int index, const Weapon& weapon, const uworld::MoveCommand& command,
                bool setOff, int which) {
    Body& body = tick.game.bodies[static_cast<std::size_t>(index)];
    Carried& carried = body.weapons[static_cast<std::size_t>(body.weapon)];
    const auto modeOf = [&weapon](int mode) -> const FireMode& { return mode == 0 ? weapon.primary : *weapon.alt; };
    const auto shoot = [&](int mode, double overshoot) {
        const FireMode& fire = modeOf(mode);
        if (carried.ammo < fire.ammo) return false;
        detail::fire(tick, index, fire, 1, true);
        tick.game.cues.push_back(Cue{Cue::Kind::Fire, index, static_cast<std::uint32_t>(body.weapon), mode,
                                     body.move.location});
        ++body.secondBurst;
        body.secondRefire = fire.refire + overshoot;
        return true;
    };
    if (setOff) {
        body.secondDue = weapon.doubleDelay;
        body.secondDueMode = which;
    }
    body.secondRefire -= tick.dt;
    if (body.secondHolding >= 0 && !(body.secondHolding == 0 ? command.fire : command.altFire)) body.secondHolding = -1;
    if (body.secondDue >= 0) {
        body.secondDue -= tick.dt;
        if (body.secondDue <= 0) {
            const int mode = body.secondDueMode;
            const FireMode& fire = modeOf(mode);
            const double overshoot = body.secondDue;
            body.secondDue = -1;
            if (fire.windUp > 0) {
                // Enforcer.AltFire: a press with any ammo turns the gun first.
                if (carried.ammo > 0 && body.secondWinding < 0) {
                    body.secondWinding = mode;
                    body.secondWind = fire.windUp + overshoot + tick.dt;
                    body.secondHolding = mode;
                    body.secondBurst = 0;
                }
            } else if (body.secondRefire <= 0 && body.secondWinding < 0 && body.secondHolding < 0) {
                // Enforcer.Fire: ignored while the second gun still fires.
                body.secondBurst = 0;
                if (shoot(mode, 0)) body.secondHolding = mode;
            }
        }
    }
    if (body.secondWinding >= 0) {
        body.secondWind -= tick.dt;
        if (body.secondWind <= 0) {
            const int mode = body.secondWinding;
            body.secondWinding = -1;
            if (!shoot(mode, body.secondWind)) body.secondHolding = -1;
        }
    } else if (body.secondHolding >= 0 && body.secondRefire <= 0) {
        // Again while held: AltFiring's Repeater, or Idle's Fire.
        if (!shoot(body.secondHolding, body.secondRefire)) body.secondHolding = -1;
    }
    if (body.secondRefire < 0 && body.secondHolding < 0) body.secondRefire = 0;
}

/// SS 4.10: the disc body `index` threw that is still out, an index into the
/// game's shots; -1 for none.
int discOf(const Tick& tick, int index) {
    for (std::size_t i = 0; i < tick.game.shots.size(); ++i) {
        const Shot& shot = tick.game.shots[i];
        if (!tick.shotGone[i] && shot.owner == index && tick.set.projectiles[shot.kind].strike == Strike::Settle)
            return static_cast<int>(i);
    }
    return -1;
}

/// Whether two upright cylinders overlap.
bool overlaps(const Vec3& a, double aRadius, double aHalf, const Vec3& b, double bRadius, double bHalf) {
    const double dx = a.x - b.x, dy = a.y - b.y, reach = aRadius + bRadius;
    return dx * dx + dy * dy < reach * reach && std::abs(a.z - b.z) < aHalf + bHalf;
}

/// Whether body `index` stands on its own disc at rest, the disc `disc`.
bool onDisc(const Tick& tick, int index, int disc) {
    const Body& body = tick.game.bodies[static_cast<std::size_t>(index)];
    const Shot& shot = tick.game.shots[static_cast<std::size_t>(disc)];
    const Projectile& kind = tick.set.projectiles[shot.kind];
    return shot.stuck && overlaps(body.move.location, body.size.radius, body.size.halfHeight, shot.location,
                                  kind.radius, detail::halfHeightOf(kind));
}

/// Whether disc `disc` is disrupted: it has taken more than its kind's `disrupt`.
bool disrupted(const Tick& tick, int disc) {
    const Shot& shot = tick.game.shots[static_cast<std::size_t>(disc)];
    const Projectile& kind = tick.set.projectiles[shot.kind];
    return kind.mass > 0 && shot.disruption > kind.disrupt;
}

/// TranslocatorTarget.Throw: along the aim, its upward part raised by
/// `liftShare` of what it lacks of straight up, at the kind's speed.
void throwDisc(Tick& tick, int index, const FireMode& mode) {
    const Body& body = tick.game.bodies[static_cast<std::size_t>(index)];
    const auto kind = static_cast<std::uint32_t>(
        std::ranges::find(tick.set.projectiles, mode.projectile, &Projectile::name) - tick.set.projectiles.begin());
    const Projectile& projectile = tick.set.projectiles[kind];
    Vec3 direction = aimOf(body.yaw, body.pitch);
    direction.z += mode.liftShare * (1 - std::abs(direction.z));
    direction = direction * (1 / uworld::length(direction)); // never zero: the lift only adds to a unit aim's up
    const Vec3 at = detail::startOf(body, mode);
    tick.spawned.push_back(Shot{.kind = kind, .owner = index, .location = at, .velocity = direction * projectile.speed,
                                .previous = at, .yaw = body.yaw, .pitch = body.pitch,
                                .lifetime = projectile.lifetime});
}

/// Translocator.Translocate: body `index` goes to its disc, and the disc is
/// gone whatever happens.
void teleport(Tick& tick, int index, const FireMode& mode) {
    const int disc = discOf(tick, index);
    if (disc < 0) return;
    Body& body = tick.game.bodies[static_cast<std::size_t>(index)];
    const Shot& shot = tick.game.shots[static_cast<std::size_t>(disc)];
    tick.shotGone[static_cast<std::size_t>(disc)] = true;
    Vec3 to = shot.location;
    if (shot.stuck) to.z += mode.landUp;
    if (!tick.world.fits(body.size, to)) return;
    const Vec3 from = body.move.location;
    body.move.location = to;
    body.move.physics = uworld::Physics::Falling;
    // Pawn.EncroachedBy: every other body the thrower now overlaps dies.
    for (std::size_t i = 0; i < tick.game.bodies.size(); ++i) {
        Body& other = tick.game.bodies[i];
        if (static_cast<int>(i) != index && other.alive() &&
            overlaps(to, body.size.radius, body.size.halfHeight, other.move.location, other.size.radius,
                     other.size.halfHeight))
            other.health = GIBBED_HEALTH;
    }
    if (disrupted(tick, disc)) {
        body.health = GIBBED_HEALTH;
        return;
    }
    // FastTrace to the disc: where the level blocks it, the body goes back.
    const uworld::BodyHit line = tick.world.trace(to, shot.location);
    if (line.startsInside || line.fraction < 1) {
        body.move.location = from;
        return;
    }
    body.move.velocity.x = body.move.velocity.y = 0;
}

/// SS 4.10: the Translocator's tick. Both modes share NormalFire's wait: a
/// press at rest is handled at once, and the triggers are looked at again
/// once `refire` has passed, counted from the next tick, nothing carried over.
/// A look finds fire before alt fire.
void translocate(Tick& tick, int index, const Weapon& weapon, const uworld::MoveCommand& command) {
    Body& body = tick.game.bodies[static_cast<std::size_t>(index)];
    const bool waited = body.refireLeft > 0;
    body.refireLeft -= tick.dt;
    if (body.refireLeft > 0) return;
    body.refireLeft = 0;
    const auto cue = [&tick, &body, index](int which) {
        tick.game.cues.push_back(
            Cue{Cue::Kind::Fire, index, static_cast<std::uint32_t>(body.weapon), which, body.move.location});
    };
    if (waited && weapon.bothBack && command.fire && command.altFire && body.previous >= 0 &&
        body.weapons[static_cast<std::size_t>(body.previous)].has) {
        // ReturnToPreviousWeapon: down from the next tick, then the weapon before.
        body.pending = body.previous;
        body.switching = Body::Switch::PutDown;
        body.switchLeft = weapon.putDown;
        dropWeaponState(body);
        return;
    }
    if (command.fire) {
        const FireMode& mode = weapon.primary;
        const int disc = discOf(tick, index);
        if (disc < 0) {
            if (body.throwWait < 0) {
                throwDisc(tick, index, mode);
                body.throwWait = mode.rethrow + mode.throwWait;
                cue(0);
            }
        } else if (tick.game.shots[static_cast<std::size_t>(disc)].age > mode.recallAfter) {
            tick.shotGone[static_cast<std::size_t>(disc)] = true;
            if (disrupted(tick, disc)) body.health = GIBBED_HEALTH;
            body.throwWait = mode.rethrow;
            cue(1);
        }
        body.refireLeft = mode.refire;
    } else if (command.altFire && weapon.alt) {
        if (discOf(tick, index) >= 0) cue(1);
        teleport(tick, index, *weapon.alt);
        body.refireLeft = weapon.alt->refire;
    }
}

void explode(Tick& tick, std::size_t index, int struck, const Vec3& away, const Vec3& splitFrom);

/// The live shell body `index` steers, or -1 for none (SS 4.11).
int guidedOf(const Tick& tick, int index) {
    for (std::size_t i = 0; i < tick.game.shots.size(); ++i)
        if (!tick.shotGone[i] && tick.game.shots[i].guided && tick.game.shots[i].owner == index)
            return static_cast<int>(i);
    return -1;
}

/// SS 4.11, WarheadLauncher's Guiding and Finishing: while body `index`
/// steers a shell, a press of either trigger once it is armed blows it up;
/// then the weapon waits. Returns whether the weapon is still busy.
bool guideTick(Tick& tick, int index, const FireMode& mode, bool pressed) {
    Body& body = tick.game.bodies[static_cast<std::size_t>(index)];
    if (body.guide == Body::Guide::Steering) {
        body.guideClock += tick.dt;
        const int shell = guidedOf(tick, index);
        if (shell >= 0 && pressed && body.guideClock >= mode.arm) {
            explode(tick, static_cast<std::size_t>(shell), -1, Vec3{0, 0, 1},
                    tick.game.shots[static_cast<std::size_t>(shell)].location);
            body.guide = Body::Guide::Recovering;
            body.guideClock = mode.recover;
        }
        return true;
    }
    body.guideClock -= tick.dt;
    if (body.guideClock > 0) return true;
    body.guide = Body::Guide::None;
    body.guideClock = 0;
    body.refireLeft = 0;
    return false;
}

void fireBody(Tick& tick, int index, const uworld::MoveCommand& command) {
    Body& body = tick.game.bodies[static_cast<std::size_t>(index)];
    const bool pressed = (command.fire && !body.fireHeld) || (command.altFire && !body.altHeld);
    // UTA-0467: one cue a shot, however many projectiles or pellets it is.
    const auto cueShot = [&tick, &body, index](int which) {
        tick.game.cues.push_back(
            Cue{Cue::Kind::Fire, index, static_cast<std::uint32_t>(body.weapon), which, body.move.location});
    };
    body.fireHeld = command.fire;
    body.altHeld = command.altFire;
    // Translocator.FireDelay is a time, not a weapon's state: it runs on
    // whatever is in hand. Held at -1, so a long wait stays one value.
    body.throwWait = std::max(body.throwWait - tick.dt, -1.0);
    if (!command.fire && !command.altFire) body.burst = 0;
    if (body.weapon < 0 || body.pending >= 0) {
        body.beamHeld = body.beamFor = 0;
        body.beamVictim = -1;
        body.lockLooking = false;
        return;
    }
    const Weapon& weapon = tick.set.weapons[static_cast<std::size_t>(body.weapon)];
    if (weapon.primary.archetype == Archetype::Throw) {
        body.lockLooking = false;
        translocate(tick, index, weapon, command);
        return;
    }
    const auto modeOf = [&weapon](int which) -> const FireMode* {
        return which == 0 ? &weapon.primary : weapon.alt ? &*weapon.alt : nullptr;
    };
    if (body.guide != Body::Guide::None) {
        const FireMode* guiding = weapon.primary.archetype == Archetype::Guide ? &weapon.primary : modeOf(1);
        body.lockLooking = false;
        if (guideTick(tick, index, *guiding, pressed)) return;
    }

    // An idle weapon's refire is at rest on 0; one still cooling is above it.
    const bool idle = body.refireLeft <= 0;
    body.refireLeft -= tick.dt;
    bool fired = false, beamed = false;
    int secondSetOff = -1; // UTA-0468: the mode whose shot or wind-up this tick sets the second gun off
    Carried& carried = body.weapons[static_cast<std::size_t>(body.weapon)];
    // A press at idle on a mode with a wind-up starts it, and pays its
    // press-ammo; the press's own tick counts toward it.
    if (idle && body.winding < 0 && body.charging < 0) {
        const int which = command.fire ? 0 : command.altFire ? 1 : -1;
        const FireMode* mode = which >= 0 ? modeOf(which) : nullptr;
        // INV-7: a press it cannot pay for changes nothing.
        if (mode != nullptr && mode->windUp > 0 && carried.ammo >= mode->pressAmmo + mode->ammo) {
            secondSetOff = which;
            carried.ammo -= mode->pressAmmo;
            body.winding = which;
            body.windLeft = mode->windUp;
            body.holding = which;
            body.heldFor = 0;
            body.repressLeft = mode->repress;
        }
    }
    // A hold lasts while its own trigger is down. Minigun2.NormalFire's
    // AnimEnd: it presses again every `repress`, paying the press's ammo and
    // winding up again; the time past that moment counts toward the wind-up.
    if (body.holding >= 0 && !(body.holding == 0 ? command.fire : command.altFire)) body.holding = -1;
    if (body.holding >= 0) {
        const FireMode& mode = *modeOf(body.holding);
        body.heldFor += tick.dt;
        body.repressLeft -= tick.dt;
        if (mode.repress > 0 && body.repressLeft <= 0) {
            if (carried.ammo >= mode.pressAmmo + mode.ammo) {
                carried.ammo -= mode.pressAmmo;
                body.winding = body.holding;
                body.windLeft = mode.windUp + body.repressLeft + tick.dt;
                body.repressLeft += mode.repress;
            } else {
                body.holding = -1;
            }
        }
    }
    // Minigun2.AltFiring: slower shots until the hold has spun it up.
    const auto refireOf = [&body](const FireMode& mode, int which) {
        return mode.spinUp > 0 && body.holding == which && body.heldFor < mode.spinUp ? mode.spinRefire : mode.refire;
    };
    if (body.winding >= 0) {
        // The shot leaves when the wind-up ends, held or not, and the time
        // past its end counts toward the refire.
        const FireMode& mode = *modeOf(body.winding);
        body.windLeft -= tick.dt;
        if (body.windLeft <= 0) {
            detail::fire(tick, index, mode, 1);
            cueShot(body.winding);
            ++body.burst;
            body.refireLeft = refireOf(mode, body.winding) + body.windLeft;
            body.winding = -1;
            body.windLeft = 0;
            fired = true;
        }
    } else if (body.charging >= 0) {
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
            const bool last = count && charge.stepLast > 0 && body.loaded + 1 >= mode.count;
            const double step = last ? charge.stepLast : charge.step;
            if (room && body.stepHeld > step && carried.ammo >= mode.ammo) {
                carried.ammo -= mode.ammo;
                // UT_Eightball loads on its animation, which runs on past
                // the step; UT_BioRifle's Count starts again from nothing.
                if (count) ++body.loaded, body.stepHeld -= step;
                else body.charged += body.stepHeld, body.stepHeld = 0;
                keepLock(tick, index, mode);
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
            keepLock(tick, index, mode);
            detail::fire(tick, index, mode, scale);
            cueShot(body.charging);
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
            // The press's own tick counts toward the first step: the weapon
            // ticks after its owner's press, in the same frame.
            body.stepHeld = tick.dt;
            body.loaded = 1;
        } else {
            keepLock(tick, index, *mode);
            detail::fire(tick, index, *mode, 1);
            cueShot(which);
            ++body.burst;
            body.refireLeft += refireOf(*mode, which) + (idle ? mode->refireLead : 0);
            fired = true;
            secondSetOff = which;
            if (mode->archetype == Archetype::Guide) {
                // SS 4.11: steering from the next tick; the aim to return to.
                body.guide = Body::Guide::Steering;
                body.guideClock = 0;
                body.guideYaw = body.yaw;
                body.guidePitch = body.pitch;
                body.refireLeft = 0;
            }
        }
    }
    // A held trigger keeps its cadence; an idle one owes nothing.
    if (!fired && body.refireLeft < 0) body.refireLeft = 0;
    const FireMode* locking = !weapon.primary.lockProjectile.empty()            ? &weapon.primary
                              : weapon.alt && !weapon.alt->lockProjectile.empty() ? &*weapon.alt
                                                                                  : nullptr;
    lookForLock(tick, index, locking, !fired && body.refireLeft <= 0 && body.winding < 0 && body.charging < 0);
    if (carried.doubled && weapon.doubleDelay > 0)
        fireSecond(tick, index, weapon, command, secondSetOff >= 0, secondSetOff);
    // A released beam is gone, and its store with it.
    if (!beamed) {
        body.beamHeld = body.beamFor = 0;
        body.beamVictim = -1;
    }
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
    // UT_BioGel.Timer: a charged glob's blast is as much wider as it is stronger, up to a cap.
    const double radius = kind.splashRadiusMax > 0 ? std::min(kind.splashRadius * shot.damageScale, kind.splashRadiusMax)
                                                   : kind.splashRadius;
    if (kind.splashRadius > 0 && (struck < 0 || !kind.bodyDirect)) {
        detail::splash(tick, shot.location, damage, momentum, radius, shot.owner, kind.splashUpMax);
    } else if (struck >= 0) {
        const double speed = uworld::length(shot.velocity);
        const Vec3 along = speed > 0 ? shot.velocity * (1 / speed) : Vec3{};
        tick.owed.push_back(Owed{struck, damage, along * momentum, shot.owner});
    }
    tick.game.events.push_back(Event{Event::Kind::Explosion, shot.location, shot.location, radius});
    tick.game.cues.push_back(Cue{Cue::Kind::Explode, shot.owner, shot.kind, -1, shot.location});
    if (!kind.wave.empty()) {
        // SS 4.11, WarShell.Explode: the wave starts off what the shell struck.
        const double awayLength = uworld::length(away);
        const Vec3 at = shot.location + (awayLength > 0 ? away * (kind.waveOff / awayLength) : Vec3{});
        const auto wave = static_cast<std::uint32_t>(tick.set.wave(kind.wave) - tick.set.waves.data());
        tick.game.waves.push_back(Spread{.kind = wave, .owner = shot.owner, .at = at});
    }
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

/// TranslocatorTarget.HitWall: a surface whose normal rises more than this is a floor.
constexpr double FLOOR_NORMAL = 0.7;

/// How far short of a surface it strikes a shot stops, along its path.
/// Recorded: a flak chunk's bounces, at 30, 85 and 120 frames a second.
constexpr double BACK_OFF = 2.5;
/// A disc's stop short of a surface (UTA-0466): a share of its move, and a
/// distance along its path. Recorded at 30, 85 and 120 frames a second: the
/// original's floor strikes lie a tenth of the move back at every rate; the
/// distance, fitted over the tl- runs, also takes up where our floor lies.
constexpr double DISC_BACK_SHARE = 0.1;
constexpr double DISC_BACK_OFF = 1.5;

/// How far past its move's end a shot looks for a surface, along its path.
/// Recorded: a flak chunk turned off a floor its move ended 1.4 above, and
/// the replay fails fewer checks at 2 than at 2.5.
constexpr double LOOK_ON = 2.0;

/// How early a trace meets a body, of the trace's own length
/// (UPrimitive::LineCheck; Fire.cpp's, recorded on the hammer).
constexpr double HIT_EARLY = 0.001;

/// The bounces a shot makes in one tick at most. Recorded: a flak chunk
/// jammed in a corner keeps 0.8 x 0.8 x 0.8 of its speed a tick, and stays put.
constexpr int BOUNCES_A_TICK = 3;

/// How far a launched shot's move, seen from above, must head in towards a
/// body's axis it starts inside to meet it: the move times the shot's offset
/// from the axis, in units squared. Recorded: of a flak slug's five pieces
/// started inside a body, the two heading in hit it and the three heading
/// out flew on; the recording fixes the sign, not this margin.
constexpr double LAUNCHED_INSIDE_IN = 0.1;

/// The upward speed a falling shot must start a tick above, and the downward
/// speed it must end it below, for the tick's move to fall only after the top
/// of its arc. Recorded at 30 frames a second: every flak chunk, grenade and
/// gel turning over in a tick follows the rule when both pass 7.13 and the
/// plain move when either is under 6.97.
constexpr double APEX_SPEED = 7.05;

/// The longest step PHYS_Falling moves a shot in. Recorded: a gel in flight
/// over a 0.157 s tick fell in four steps, 0.05, 0.05 and the rest in two
/// halves, each at the speed its own gravity gave.
constexpr double FALLING_STEP = 0.05;

/// A difference of `rawDifference`'s as the shortest turn, -32768 to 32768:
/// GuidedWarshell.Tick's wrap.
std::int32_t shortTurn(std::int32_t units) {
    if (units < -32768) return units + 65536;
    if (units > 32768) return units - 65536;
    return units;
}

/// The difference `a - b` of two angles each taken modulo a turn, as
/// GuidedWarshell.Tick takes it: from -65535 to 65535, unwrapped.
std::int32_t rawDifference(std::int32_t a, std::int32_t b) { return (a & 0xFFFF) - (b & 0xFFFF); }

/// Roll caps and smoothing in GuidedWarshell.MoveRocket.
constexpr std::int32_t ROLL_MAX = 12000;
constexpr std::int32_t ROLL_MIN = 53535;
constexpr double ROLL_GAIN = 10;
constexpr double ROLL_SMOOTH = 5;

/// SS 4.11, GuidedWarshell.Tick then MoveRocket: the shell follows the change
/// of its owner's aim, then turns towards the way it is steered and keeps
/// its steering speed.
void steerShot(Tick& tick, Shot& shot, const Projectile& kind) {
    const Body& owner = tick.game.bodies[static_cast<std::size_t>(shot.owner)];
    const std::int32_t deltaYaw = shortTurn(rawDifference(owner.yaw, shot.aimYaw));
    const std::int32_t deltaPitch = shortTurn(rawDifference(owner.pitch, shot.aimPitch));
    const std::int32_t heading = core::angleOf(shot.velocity.x, shot.velocity.y);
    const std::int32_t yawDiff = rawDifference(heading, shot.steerYaw) - deltaYaw;
    const auto lead = static_cast<std::int32_t>(kind.steerLead);
    const bool follows = deltaYaw < 0 ? (yawDiff > 0 && yawDiff < lead) || yawDiff < lead - 65536
                                      : (yawDiff < 0 && yawDiff > -lead) || yawDiff > 65536 - lead;
    if (follows) shot.steerYaw += deltaYaw;
    shot.steerPitch += deltaPitch;
    shot.aimYaw = owner.yaw;
    shot.aimPitch = owner.pitch;

    const std::int32_t oldRoll = shot.roll & 0xFFFF;
    const Vec3 oldVelocity = shot.velocity;
    const Vec3 sum = shot.velocity + aimOf(shot.steerYaw, shot.steerPitch) * (kind.steer * tick.dt);
    const double length = uworld::length(sum);
    if (length > 0) shot.velocity = sum * (kind.steerSpeed / length);
    // The roll leans into the turn: the speed gained along the shell's right.
    const std::int32_t yaw = core::angleOf(shot.velocity.x, shot.velocity.y);
    const Vec3 right{-core::sineOf(yaw), core::cosineOf(yaw), 0};
    const auto rollMag = static_cast<std::int32_t>(ROLL_GAIN * uworld::dot(right, shot.velocity - oldVelocity) / tick.dt);
    std::int32_t roll = rollMag > 0 ? std::min(ROLL_MAX, rollMag) : std::max(ROLL_MIN, 65536 + rollMag);
    std::int32_t from = oldRoll;
    if (roll > 32768) {
        if (from < 32768) from += 65536;
    } else if (from > 32768) {
        from -= 65536;
    }
    const double smooth = std::min(1.0, ROLL_SMOOTH * tick.dt);
    shot.roll = static_cast<std::int32_t>(roll * smooth + from * (1 - smooth));
}

void fly(Tick& tick, std::size_t index) {
    Shot& shot = tick.game.shots[index];
    const Projectile& kind = tick.set.projectiles[shot.kind];
    const bool launched = shot.age == 0;
    shot.age += tick.dt;
    if (shot.guided && kind.steer > 0) {
        // MoveRocket: a shell whose steerer is dead blows up where it is.
        if (!tick.game.bodies[static_cast<std::size_t>(shot.owner)].alive()) {
            explode(tick, index, -1, Vec3{0, 0, 1}, shot.location);
            return;
        }
        steerShot(tick, shot, kind);
    }
    if (shot.stuck) {
        // A disc at rest stays until it is taken, recalled or knocked (SS 4.10).
        if (kind.strike == Strike::Stick && shot.age >= kind.fuse) explode(tick, index, -1, Vec3{0, 0, 1}, shot.location);
        return;
    }
    if (shot.lifetime > 0 && shot.age >= shot.lifetime) {
        if (kind.expireExplodes) {
            // UT_Grenade.Timer: the blast goes off above the shot.
            shot.location.z += kind.expireUp;
            explode(tick, index, -1, shot.velocity * -1, shot.location);
        } else {
            tick.shotGone[index] = true;
        }
        return;
    }
    // UTA-0468, UT_SeekingRocket.Timer, before the move: every seek-every it
    // turns half its speed's worth towards its body while that body lies
    // ahead of the way it first flew, keeping its speed.
    if (kind.seekEvery > 0 && shot.seeking >= 0) {
        shot.seekClock += tick.dt;
        if (shot.seekClock >= kind.seekEvery) {
            shot.seekClock -= kind.seekEvery;
            const double speed = uworld::length(shot.velocity);
            if (shot.seekFrom == Vec3{} && speed > 0) shot.seekFrom = shot.velocity * (1 / speed);
            const Body& target = tick.game.bodies[static_cast<std::size_t>(shot.seeking)];
            const Vec3 way = target.move.location - shot.location;
            const double distance = uworld::length(way);
            if (target.alive() && shot.seeking != shot.owner && distance > 0 &&
                uworld::dot(way, shot.seekFrom) > 0) {
                const Vec3 turned = way * (0.5 * speed / distance) + shot.velocity;
                const double length = uworld::length(turned);
                if (length > 0) {
                    const Vec3 heading = turned * (1 / length);
                    shot.velocity = heading * speed;
                    shot.acceleration = heading * kind.seekAcceleration;
                }
            }
        }
    }
    const Vec3 old = shot.previous;
    shot.previous = shot.location;
    // PHYS_Falling moves a long tick in steps: none longer than FALLING_STEP,
    // and the last two halving what is left. Straight flight takes it whole.
    double left = tick.dt;
    for (bool opening = true; left > 0; opening = false) {
        const bool falling = kind.gravity > 0 || (shot.bounces > 0 && kind.bounceGravity);
        const double step = !falling || left <= FALLING_STEP ? left : std::min(FALLING_STEP, left / 2);
        left -= step;
        // A bounce ends one move and starts another in the same step, over
        // the time the first left. `more` is time it moves for and does not
        // fall for.
        double time = step, more = 0;
        for (int move = 0; move < BOUNCES_A_TICK; ++move) {
            const double falls = shot.bounces > 0 && kind.bounceGravity ? *kind.bounceGravity : kind.gravity;
            const double rising = shot.velocity.z;
            shot.velocity.z += tick.rules.gravityZ * falls * time;
            // The engine's projectile physics: speed up, but no faster than its
            // most. A kind launched faster than that (the ripper's blade) is held
            // to it from its first move.
            shot.velocity = shot.velocity + shot.acceleration * time;
            const double speed = uworld::length(shot.velocity);
            if (kind.maxSpeed > 0 && speed > kind.maxSpeed) shot.velocity = shot.velocity * (kind.maxSpeed / speed);
            const Vec3 from = shot.location;
            Vec3 delta = shot.velocity * (time + more);
            // Turning over fast enough in one step, the original's shot keeps
            // its height until the top of its arc and falls only for the rest.
            if (move == 0 && kind.gravityAfter == 0 && rising > APEX_SPEED && shot.velocity.z < -APEX_SPEED) {
                const double top = time * rising / (rising - shot.velocity.z);
                delta.z = shot.velocity.z * (time - top);
            }
            const Vec3 to = from + delta;

            // The level is looked for a little past the move's end; `first` and
            // `met` are fractions of the move itself, so may pass 1.
            const double reach = uworld::length(delta);
            const double looked = reach > 0 ? (reach + LOOK_ON) / reach : 1;
            const Vec3 past = from + delta * looked;
            const uworld::BodyHit level = kind.radius > 0
                                              ? tick.world.sweep(uworld::BodySize{kind.radius, detail::halfHeightOf(kind)}, from, past)
                                              : tick.world.trace(from, past);
            const bool surface = level.startsInside || level.fraction < 1;
            const double met = level.startsInside ? 0 : level.fraction * looked;
            double first = met;
            int struck = -1;
            const std::vector<Body>& bodies = tick.game.bodies;
            for (std::size_t i = 0; i < bodies.size(); ++i) {
                if (static_cast<int>(i) == shot.owner || !bodies[i].alive()) continue;
                const double at = entersCylinder(from, to, bodies[i].move.location, bodies[i].size.radius + kind.radius,
                                                 bodies[i].size.halfHeight + detail::halfHeightOf(kind));
                // TranslocatorTarget.Touch comes as a body is first met; a disc
                // inside one already passes through it.
                if (kind.strike == Strike::Settle && at == 0) continue;
                // A shot launched inside a body meets it only where its move heads
                // in, towards the body's axis; heading out, it flies on. Recorded:
                // a flak slug's pieces, started inside the body it struck.
                if (launched && opening && at == 0) {
                    const Vec3 off = from - bodies[i].move.location;
                    if (delta.x * off.x + delta.y * off.y >= -LAUNCHED_INSIDE_IN) continue;
                }
                if (at <= first && at <= 1) {
                    first = at;
                    struck = static_cast<int>(i);
                }
            }
            if (struck >= 0 && kind.strike == Strike::Settle) {
                // TranslocatorTarget.Touch: it stops over the body's axis, at its
                // own height, and falls on through it.
                const Vec3& axis = bodies[static_cast<std::size_t>(struck)].move.location;
                const Vec3 end = from + delta * std::min(met, 1.0);
                shot.location = Vec3{axis.x, axis.y, end.z};
                shot.velocity = {};
                return;
            }
            if (struck >= 0) {
                // The script's Location is where the move ends, the level allowing;
                // its HitLocation is where the shot met the body.
                // The body's AdjustHitLocation moves it on into it, along the shot.
                const double moved = std::min(met, 1.0);
                const Body& body = bodies[static_cast<std::size_t>(struck)];
                const double moving = uworld::length(shot.velocity);
                const Vec3 inset = moving > 0 ? shot.velocity * (body.hitInset * body.size.radius / moving) : Vec3{};
                Vec3 end = from + delta * moved;
                // Projectile.Touch's trace, OldLocation to Location, meets the
                // body a little early (UPrimitive::LineCheck).
                Vec3 met = from + delta * first + inset - (end - shot.previous) * HIT_EARLY;
                if (opening && move == 0 && first == 0 && !launched) {
                    // The body moved onto the shot. Projectile.Touch traces the
                    // shot's last move, OldLocation to Location, for where it met
                    // the body where the body now stands; a move that starts
                    // inside it finds nothing, and the hit is on its side, towards
                    // the shot, not moved in. The shot does not move on.
                    const double back = entersCylinder(old, from, body.move.location, body.size.radius + kind.radius,
                                                       body.size.halfHeight + kind.radius);
                    const Vec3 off = from - body.move.location;
                    const double away = uworld::length(off);
                    if (back > 0 && back <= 1) met = old + (from - old) * (back - HIT_EARLY) + inset;
                    else met = away > 0 ? body.move.location + off * (body.size.radius / away) : from;
                    end = from;
                } else if (kind.radius > 0) {
                    // Projectile.Touch traces a line, not the shot's cylinder,
                    // OldLocation to Location, against the body's own cylinder;
                    // a line that misses it hits its side facing the shot
                    // (UTA-0465: a Redeemer shell's blast on the body it struck).
                    const double line = entersCylinder(from, end, body.move.location, body.size.radius,
                                                       body.size.halfHeight);
                    const Vec3 off = end - body.move.location;
                    const double away = uworld::length(off);
                    if (line <= 1) met = from + (end - from) * (line - HIT_EARLY) + inset;
                    else if (away > 0) met = body.move.location + off * (body.size.radius / away);
                }
                shot.location = kind.splashFromMoved ? end : met;
                // flakslug.ProcessTouch: off the body from where the shot met it, the
                // pieces starting from where its move ended.
                explode(tick, index, struck, met - body.move.location, end);
                return;
            }
            if (!surface) {
                shot.location = to;
                // The engine's falling physics gives an actor that does not bounce
                // the second half of a tick's gravity after its move.
                shot.velocity.z += tick.rules.gravityZ * kind.gravityAfter * time;
                break;
            }
            // The level: a shot starting in solid strikes where it is (SS 6).
            // It stops short of the surface, and the time it would have taken
            // to close that gap is left to it.
            if (kind.strike == Strike::Settle)
                first = std::max(0.0, first - DISC_BACK_SHARE - (reach > 0 ? DISC_BACK_OFF / reach : 0));
            else if (reach > 0) first = std::max(0.0, first - BACK_OFF / reach);
            shot.location = from + delta * first;
            const Vec3 normal = level.normal;
            if (kind.strike == Strike::Settle) {
                // TranslocatorTarget.HitWall: it bounces until it has struck a
                // floor; the strike after that stops it on a floor, and off a
                // wall it slides on, bouncing no more.
                const Vec3 into = normal * uworld::dot(shot.velocity, normal);
                if (level.startsInside || (shot.floored && normal.z > FLOOR_NORMAL)) {
                    shot.stuck = true;
                    shot.velocity = {};
                    return;
                }
                if (shot.floored) {
                    shot.velocity = shot.velocity - into;
                } else {
                    shot.velocity = (shot.velocity - into) * kind.bounceKeep - into * kind.bounceOff;
                    shot.floored = normal.z > FLOOR_NORMAL;
                }
                time = (time + more) * (1 - first);
                more = 0;
                ++shot.bounces;
                continue;
            }
            if (kind.strike == Strike::Stick && !level.startsInside) {
                shot.stuck = true;
                shot.velocity = {};
                shot.age = 0;
                return;
            }
            if (kind.strike != Strike::Bounce || shot.bounces >= kind.bounces || level.startsInside) {
                explode(tick, index, -1, normal, shot.location);
                return;
            }
            // UTChunk.HitWall and UT_Grenade.HitWall: its speed along the surface
            // and its speed off it are each kept in part, the second by a draw.
            const Vec3 into = normal * uworld::dot(shot.velocity, normal);
            const double off = kind.bounceOff + detail::rangeDraw(tick.game, kind.bounceOffRandom);
            shot.velocity = (shot.velocity - into) * kind.bounceKeep - into * off;
            // The move goes on over the time left. A kind that starts to fall at
            // its first bounce falls for the time left and moves for a whole tick
            // more: recorded, the original's change of physics inside HitWall.
            time = (time + more) * (1 - first);
            more = shot.bounces == 0 && kind.bounceGravity ? tick.dt : 0;
            ++shot.bounces;
        }
    }
}

/// SS 4.11, ShockWave.Timer: one act of wave `wave`, `left` seconds of its life left.
void waveAct(Tick& tick, Spread& wave, double left) {
    const Wave& kind = tick.set.waves[wave.kind];
    const double reach = reachOf(kind, kind.lifetime - left);
    // VisibleCollidingActors: within the reach of the actor's own radius,
    // and in sight of the centre. Recorded (rd-near): the hits the original
    // makes, and misses, need the radius; the last act's reach does not take it.
    const auto meets = [&tick, &wave, reach](const Vec3& at, double radius, Vec3& direction, double& distance) {
        const Vec3 away = at - wave.at;
        if (uworld::length(away) >= reach + radius) return false;
        const uworld::BodyHit line = tick.world.trace(wave.at, at);
        if (line.startsInside || line.fraction < 1) return false;
        distance = std::max(1.0, uworld::length(away));
        direction = away * (1 / distance);
        return true;
    };
    for (std::size_t i = 0; i < tick.game.bodies.size(); ++i) {
        const Body& body = tick.game.bodies[i];
        Vec3 direction{};
        double distance = 0;
        if (!body.alive() || !meets(body.move.location, body.size.radius, direction, distance)) continue;
        direction.z += kind.lift;
        if (distance <= wave.reached && uworld::dot(direction, body.move.velocity) >= 0) continue;
        const double scale = std::max(0.0, kind.damage - kind.falloff * distance);
        tick.owed.push_back(Owed{static_cast<int>(i), scale, direction * kind.momentum, wave.owner,
                                 direction * (scale + kind.push)});
    }
    for (std::size_t i = 0; i < tick.game.shots.size(); ++i) {
        Shot& shot = tick.game.shots[i];
        const Projectile& shotKind = tick.set.projectiles[shot.kind];
        Vec3 direction{};
        double distance = 0;
        if (tick.shotGone[i] || !meets(shot.location, shotKind.radius, direction, distance)) continue;
        direction.z += kind.lift;
        if (distance <= wave.reached && uworld::dot(direction, shot.velocity) >= 0) continue;
        const double scale = std::max(0.0, kind.damage - kind.falloff * distance);
        shot.velocity = shot.velocity + direction * (scale + kind.push);
        const Vec3 hitAt = shot.location - direction * (0.5 * (detail::halfHeightOf(shotKind) + shotKind.radius));
        if (detail::isTarget(shotKind)) detail::knockDisc(tick, static_cast<int>(i), scale, direction * kind.momentum);
        else if (shotKind.shotDownRadius > 0) detail::hurtShot(tick, static_cast<int>(i), scale, hitAt);
    }
    wave.reached = reach;
}

/// SS 4.11: each wave ages, acts every `every` of it, and ends with its life.
void spread(Tick& tick) {
    std::vector<Spread> kept;
    for (std::size_t i = 0; i < tick.game.waves.size(); ++i) {
        Spread wave = tick.game.waves[i];
        const Wave& kind = tick.set.waves[wave.kind];
        // The timer acts on the life left before this tick's, and the wave
        // ends after it. Recorded: the original's reach at each act is the one
        // a tick younger (rd-near), and its last act comes in the tick its
        // life runs out (rd-fire-up).
        wave.clock += tick.dt;
        if (wave.clock >= kind.every) {
            wave.clock -= kind.every;
            waveAct(tick, wave, kind.lifetime - wave.age);
        }
        wave.age += tick.dt;
        if (wave.age < kind.lifetime) kept.push_back(wave);
    }
    tick.game.waves = std::move(kept);
}

/// The damage and momentum owed, landed in the order they were owed.
/// Damage is UT99's whole numbers: each step truncates, as its `int` does.
void land(Tick& tick) {
    for (const Owed& owed : tick.owed) {
        Body& body = tick.game.bodies[static_cast<std::size_t>(owed.body)];
        if (!body.alive()) continue;
        const bool self = owed.instigator == owed.body;
        // SS 4.11: a wave's AddVelocity comes before its TakeDamage, and leaves the body falling.
        if (uworld::length(owed.velocity) > 0) body.move = uworld::push(body.move, owed.velocity);
        Vec3 momentum = owed.momentum * (self ? SELF_MOMENTUM : 1.0);
        if (body.move.physics == uworld::Physics::Walking)
            momentum.z = std::max(momentum.z, WALKING_LIFT * uworld::length(momentum));
        double damage = std::trunc(std::trunc(owed.damage) * tick.set.damageScale);
        if (self) damage = std::trunc(damage * tick.set.selfDamage);
        body.health -= damage;
        if (uworld::length(momentum) > 0) body.move = uworld::push(body.move, momentum * (1 / MASS));
    }
}

/// UTA-0493 SS 4.5: Pawn.PainTimer, in tenths so every compiler agrees. The
/// feet decide whether the clock runs; the centre and head add to the depth.
void painTick(const uworld::ZoneWorld& zones, Body& body, double dt) {
    const Vec3 centre = body.move.location;
    const auto pains = [&zones](const Vec3& point) {
        const ubundle::Zone* zone = zones.zoneAt(point);
        return zone != nullptr && zone->pain != 0 ? zone : nullptr;
    };
    const ubundle::Zone* feet = pains(centre - Vec3{0, 0, body.size.halfHeight});
    if (feet == nullptr) {
        body.painClock = -1;
        return;
    }
    if (body.painClock < 0) {
        // It counts from the next tick, as the engine's timer does from the
        // next frame.
        body.painClock = PAIN_FIRST_WAIT;
        return;
    }
    body.painClock -= dt;
    if (body.painClock > 0) return;
    const std::int64_t tenths =
        4 + (pains(centre) != nullptr ? 4 : 0) + (pains(centre + Vec3{0, 0, body.eyeHeight}) != nullptr ? 2 : 0);
    const std::int64_t amount = static_cast<std::int64_t>(feet->damagePerSec) * tenths / 10;
    if (feet->damagePerSec > 0) body.health -= static_cast<double>(amount);
    else if (body.health < START_HEALTH)
        body.health = std::min(START_HEALTH, body.health - static_cast<double>(amount));
    if (body.alive()) body.painClock = PAIN_WAIT;
}

} // namespace

double landingDamage(double landedZ, double jumpZ) {
    if (landedZ > -750 - jumpZ) return 0;
    if (landedZ < -1650 - jumpZ) return LANDING_DEATH_DAMAGE;
    return std::trunc(-0.15 * (landedZ + 700 + jumpZ));
}

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

std::vector<int> wheelWeapons(const WeaponSet& set, const Body& body) {
    std::vector<int> carried;
    for (std::size_t i = 0; i < body.weapons.size(); ++i)
        if (body.weapons[i].has) carried.push_back(static_cast<int>(i));
    std::ranges::sort(carried, {}, [&set](int weapon) { return stepKey(set, weapon); });
    return carried;
}

void giveAll(const WeaponSet& set, Body& body) {
    body.weapons.resize(set.weapons.size());
    for (std::size_t i = 0; i < set.weapons.size(); ++i) body.weapons[i] = Carried{true, set.weapons[i].ammoMax};
}

void tick(const uworld::BodyWorld& world, const WeaponSet& set, const uworld::MoveRules& rules, Game& game,
          std::span<const uworld::MoveCommand> commands, double dt, const uworld::ZoneWorld* zones) {
    game.events.clear();
    game.cues.clear();
    Tick tick{world, set, rules, game, dt, {}, {}, std::vector<bool>(game.shots.size(), false)};
    const std::size_t count = std::min(game.bodies.size(), commands.size());
    for (std::size_t i = 0; i < count; ++i) {
        Body& body = game.bodies[i];
        if (!body.alive()) continue;
        switchWeapon(set, game, static_cast<int>(i), commands[i], dt);
        // SS 4.10: a body moving onto its disc at rest takes it back; one
        // already over it does not (Touch comes as the two first meet).
        const int disc = discOf(tick, static_cast<int>(i));
        const bool wasOn = disc >= 0 && onDisc(tick, static_cast<int>(i), disc);
        body.move = uworld::tick(world, body.size, rules, body.move, commands[i], dt);
        if (disc >= 0 && !wasOn && onDisc(tick, static_cast<int>(i), disc))
            tick.shotGone[static_cast<std::size_t>(disc)] = true;
        if (rules.landingDamage) body.health -= landingDamage(body.move.landedZ, rules.jumpZ);
        if (zones != nullptr && body.alive()) {
            // UTA-0493 SS 4.4: outside the level is death, not damage, so it
            // passes neither the landing nor its flag. Unknown never kills.
            if (zones->at(body.move.location).kind == umap::ZoneHit::Kind::Outside)
                body.health = OUT_OF_WORLD_HEALTH;
            else
                painTick(*zones, body, dt);
        }
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
    spread(tick);
    // SS 4.11: a steered shell gone, however it went, sends its weapon to recover.
    for (std::size_t i = 0; i < count; ++i) {
        Body& body = game.bodies[i];
        if (body.guide != Body::Guide::Steering || guidedOf(tick, static_cast<int>(i)) >= 0) continue;
        const Weapon& weapon = set.weapons[static_cast<std::size_t>(body.weapon)];
        const FireMode& mode = weapon.primary.archetype == Archetype::Guide ? weapon.primary : *weapon.alt;
        body.guide = Body::Guide::Recovering;
        body.guideClock = mode.recover;
    }
    land(tick);

    std::vector<Shot> kept;
    for (std::size_t i = 0; i < game.shots.size(); ++i)
        if (!tick.shotGone[i]) kept.push_back(game.shots[i]);
    game.shots = std::move(kept);
    ++game.ticks;

    // UTA-0020 SS 4.3: the wheel slows time only for a lone human, alive, so
    // its command is read; a dead body's bit, or a dummy's, is not.
    int humans = 0;
    std::size_t human = 0;
    for (std::size_t i = 0; i < game.bodies.size(); ++i) {
        if (!game.bodies[i].human) continue;
        ++humans;
        human = i;
    }
    const bool slowed = humans == 1 && human < count && game.bodies[human].alive() && commands[human].wheelOpen;
    game.timeScale = slowed ? WHEEL_TIME_SCALE : 1;
}

double reachOf(const Wave& kind, double age) {
    const double left = kind.lifetime - age;
    return kind.scale * (kind.grow * age + kind.start / (left / kind.lifetime + kind.edge));
}

std::string_view soundOf(const WeaponSet& set, const Cue& cue) {
    switch (cue.kind) {
    case Cue::Kind::Fire:
    case Cue::Kind::Select: {
        if (cue.index >= set.weapons.size()) return {};
        const Weapon& weapon = set.weapons[cue.index];
        if (cue.kind == Cue::Kind::Select) return weapon.selectSound;
        if (cue.mode == 0) return weapon.primary.sound;
        return cue.mode == 1 && weapon.alt ? std::string_view{weapon.alt->sound} : std::string_view{};
    }
    case Cue::Kind::Explode: return cue.index < set.projectiles.size() ? set.projectiles[cue.index].sound : std::string_view{};
    case Cue::Kind::Combo: return cue.index < set.combos.size() ? set.combos[cue.index].sound : std::string_view{};
    }
    return {};
}

int loopingMode(const WeaponSet& set, const Body& body) {
    // During a switch the old weapon is still `weapon`, and `burst` still
    // counts its shots: fireBody leaves both alone until the new one is up.
    if (!body.alive() || body.weapon < 0 || body.pending >= 0) return -1;
    const Weapon& weapon = set.weapons[static_cast<std::size_t>(body.weapon)];
    // fireBody gives the primary trigger the beam where both are down.
    const int held = body.fireHeld ? 0 : body.altHeld ? 1 : -1;
    const int beaming = body.beamFor > 0 ? held : -1;
    const std::uint32_t ammo = body.weapons[static_cast<std::size_t>(body.weapon)].ammo;
    for (int which = 0; which < 2; ++which) {
        const FireMode* mode = which == 0 ? &weapon.primary : weapon.alt ? &*weapon.alt : nullptr;
        if (mode == nullptr || mode->loopSound.empty()) continue;
        // A mode fired shot after shot sounds from its first shot of the
        // hold until the trigger is up or it can no longer pay for one.
        const bool firing = held == which && body.burst > 0 && ammo >= mode->ammo;
        const bool sounds = mode->archetype == Archetype::Beam
                                ? beaming == which
                                : body.winding == which || body.holding == which || firing;
        if (sounds) return which;
    }
    return -1;
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
    addDouble(game.timeScale);
    add(game.bodies.size());
    for (const Body& body : game.bodies) {
        addVec(body.move.location);
        addVec(body.move.velocity);
        add(static_cast<std::uint64_t>(body.move.physics));
        add(static_cast<std::uint64_t>(body.move.dodge));
        addDouble(body.move.dodgeDoneTime);
        add(body.move.jumpHeld ? 1 : 0);
        add(static_cast<std::uint64_t>(body.move.airJumpsUsed));
        addDouble(body.move.coyoteLeft);
        addVec(body.move.mantleTo);
        addDouble(body.move.landedZ);
        addDouble(body.health);
        addDouble(body.painClock); // UTA-0493
        for (const Carried& carried : body.weapons) {
            add(carried.has ? 1 : 0);
            add(carried.ammo);
            add(carried.doubled ? 1 : 0); // UTA-0468
        }
        add(static_cast<std::uint64_t>(body.weapon));
        add(static_cast<std::uint64_t>(body.pending));
        add(static_cast<std::uint64_t>(body.switching)); // UTA-0501
        addDouble(body.switchLeft);
        add(static_cast<std::uint64_t>(body.next)); // UTA-0502
        addDouble(body.refireLeft);
        add(static_cast<std::uint64_t>(body.winding));
        addDouble(body.windLeft);
        add(static_cast<std::uint64_t>(body.holding));
        addDouble(body.heldFor);
        addDouble(body.repressLeft);
        add(static_cast<std::uint64_t>(body.charging));
        addDouble(body.charged);
        addDouble(body.stepHeld);
        add(body.loaded);
        addDouble(body.beamAmmo);
        addDouble(body.beamHeld);
        addDouble(body.beamFor);
        add(static_cast<std::uint64_t>(body.beamVictim));
        addDouble(body.beamSince);
        add(body.burst);
        addDouble(body.eyeHeight);
        add(body.human ? 1 : 0);
        // UTA-0468
        add(body.lockLooking ? 1 : 0);
        addDouble(body.lockClock);
        add(static_cast<std::uint64_t>(body.lockCandidate));
        add(static_cast<std::uint64_t>(body.lockTarget));
        addDouble(body.secondDue);
        add(static_cast<std::uint64_t>(body.secondDueMode));
        addDouble(body.secondRefire);
        add(static_cast<std::uint64_t>(body.secondWinding));
        addDouble(body.secondWind);
        add(static_cast<std::uint64_t>(body.secondHolding));
        add(body.secondBurst);
        addDouble(body.throwWait); // SS 4.10
        add(static_cast<std::uint64_t>(body.previous));
        add(static_cast<std::uint64_t>(body.guide)); // SS 4.11
        addDouble(body.guideClock);
        add(static_cast<std::uint64_t>(body.guideYaw));
        add(static_cast<std::uint64_t>(body.guidePitch));
    }
    add(game.shots.size());
    for (const Shot& shot : game.shots) {
        add(shot.kind);
        add(static_cast<std::uint64_t>(shot.owner));
        addVec(shot.location);
        addVec(shot.velocity);
        addVec(shot.previous);
        addVec(shot.acceleration);
        add(static_cast<std::uint64_t>(shot.yaw));
        add(static_cast<std::uint64_t>(shot.pitch));
        addDouble(shot.lifetime);
        addDouble(shot.age);
        add(shot.bounces);
        add(shot.stuck ? 1 : 0);
        addDouble(shot.damageScale);
        add(static_cast<std::uint64_t>(shot.seeking)); // UTA-0468
        addDouble(shot.seekClock);
        addVec(shot.seekFrom);
        add(shot.floored ? 1 : 0); // SS 4.10
        addDouble(shot.disruption);
        add(shot.guided ? 1 : 0); // SS 4.11
        add(static_cast<std::uint64_t>(shot.steerYaw));
        add(static_cast<std::uint64_t>(shot.steerPitch));
        add(static_cast<std::uint64_t>(shot.aimYaw));
        add(static_cast<std::uint64_t>(shot.aimPitch));
        add(static_cast<std::uint64_t>(shot.roll));
    }
    add(game.waves.size()); // SS 4.11
    for (const Spread& wave : game.waves) {
        add(wave.kind);
        add(static_cast<std::uint64_t>(wave.owner));
        addVec(wave.at);
        addDouble(wave.age);
        addDouble(wave.clock);
        addDouble(wave.reached);
    }
    return hash;
}

} // namespace uta::ugame
