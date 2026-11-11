// The game state and its tick -- docs/specs/UTA-0019-weapon-set.md SS 4.5.
//
// Bodies move by uworld's rules, carry weapons from a WeaponSet, and fire
// them; projectiles fly, strike and explode. Everything the tick decides is
// here and nothing reads a clock or a global generator, so a fixed script
// gives the same state on every run and every compiler (INV-5).

#pragma once

#include "ugame/WeaponSet.h"
#include "uworld/Movement.h"

#include <cstdint>
#include <span>
#include <vector>

namespace uta::ugame {

using uworld::Vec3;

/// A body's height of eye above its centre: TournamentPlayer.BaseEyeHeight.
inline constexpr double EYE_HEIGHT = 27;
/// A body's starting health: TournamentPlayer.Health.
inline constexpr double START_HEALTH = 100;

/// One weapon a body carries: whether it has it, and its ammo.
struct Carried {
    bool has = false;
    std::uint32_t ammo = 0;
    bool operator==(const Carried&) const = default;
};

struct Body {
    uworld::MoveState move;
    uworld::BodySize size;
    double health = START_HEALTH;
    std::vector<Carried> weapons; ///< one per weapon in the set, in its order
    int weapon = -1;              ///< the weapon in hand, an index into the set; -1 for none
    int pending = -1;             ///< the weapon being brought up; -1 for none
    double switchLeft = 0;        ///< seconds until `pending` is in hand
    double refireLeft = 0;        ///< seconds until the weapon in hand may fire again
    int charging = -1;            ///< 0 primary, 1 alt, while a charge is held; else -1
    double charged = 0;           ///< seconds a charge has been held
    double beamAmmo = 0;          ///< a beam's ammo owed, below one round
    std::int32_t yaw = 0, pitch = 0; ///< the last command's aim
    bool fireHeld = false, altHeld = false;

    [[nodiscard]] bool alive() const { return health > 0; }
};

struct Shot {
    std::uint32_t kind = 0; ///< an index into the set's projectiles
    int owner = -1;         ///< the body that fired it
    Vec3 location{}, velocity{};
    double age = 0;         ///< seconds since it was fired
    std::uint32_t bounces = 0;
    bool stuck = false;
    double damageScale = 1; ///< a charge's
    bool operator==(const Shot&) const = default;
};

/// What a tick did that a client draws: a shot's line, an explosion.
struct Event {
    enum class Kind : std::uint8_t { InstantHit, Beam, Melee, Explosion, Combo };
    Kind kind;
    Vec3 from{}, to{};  ///< a line's ends; an explosion's centre is `from`
    double radius = 0;  ///< an explosion's
    bool operator==(const Event&) const = default;
};

struct Game {
    std::vector<Body> bodies;
    std::vector<Shot> shots;
    std::uint64_t ticks = 0;
    std::uint64_t random = 0x9E3779B97F4A7C15u; ///< the one generator's state (SS 4.5)
    std::vector<Event> events;                   ///< this tick's; cleared at its start
};

/// INV-5: a hash of every integer in `game` and the bits of every double,
/// in a fixed order, so two states hash alike only where they are alike.
[[nodiscard]] std::uint64_t stateHash(const Game& game);

/// A body standing at `location`, carrying every weapon in `set` with its
/// starting ammo, the first by slot in hand.
[[nodiscard]] Body bodyAt(const WeaponSet& set, const Vec3& location, std::int32_t yaw);

/// Gives `body` every weapon in `set` with its most ammo.
void giveAll(const WeaponSet& set, Body& body);

/// The direction of `yaw` and `pitch`, a unit vector (core/Angles.h).
[[nodiscard]] Vec3 aimOf(std::int32_t yaw, std::int32_t pitch);

/// SS 4.5: one tick. `commands` holds one per body, in order; a dead body's
/// is ignored. Each body switches weapon and moves, then each fires; then
/// the shots fly; then damage and momentum land, so no body's order changes
/// what another's shot hits.
void tick(const uworld::BodyWorld& world, const WeaponSet& set, const uworld::MoveRules& rules, Game& game,
          std::span<const uworld::MoveCommand> commands, double dt);

// The parts of a shot SS 4.5 and INV-8 and INV-9 name, for tests.

/// Where a segment from `from` to `to` enters an upright cylinder centred at
/// `centre`, as a fraction along it; or a value above 1 where it misses. A
/// segment starting inside enters at 0.
[[nodiscard]] double entersCylinder(const Vec3& from, const Vec3& to, const Vec3& centre, double radius,
                                    double halfHeight);

} // namespace uta::ugame
