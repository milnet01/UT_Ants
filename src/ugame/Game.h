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
#include <string_view>
#include <vector>

namespace uta::ugame {

using uworld::Vec3;

/// A body's height of eye above its centre: TournamentPlayer.BaseEyeHeight.
inline constexpr double EYE_HEIGHT = 27;
/// A body's starting health: TournamentPlayer.Health.
inline constexpr double START_HEALTH = 100;
/// Game seconds per wall second while a lone human's weapon wheel is open
/// (docs/decisions/ADR-0009-weapon-wheel-slows-time-for-a-lone-player.md).
inline constexpr double WHEEL_TIME_SCALE = 0.2;

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
    int winding = -1;             ///< 0 primary, 1 alt, while a wind-up runs; else -1
    double windLeft = 0;          ///< seconds until the wind-up's shot
    int holding = -1;             ///< 0 primary, 1 alt, while the press that wound up is still held; else -1
    double heldFor = 0;           ///< seconds since that press
    double repressLeft = 0;       ///< seconds until that hold presses again (`repress`)
    int charging = -1;            ///< 0 primary, 1 alt, while a charge is held; else -1
    double charged = 0;           ///< seconds a charge has been held; a stepped damage charge's steps
    double stepHeld = 0;          ///< seconds a stepped charge has been held since its last step
    std::uint32_t loaded = 0;     ///< a count charge's projectiles loaded
    double beamAmmo = 0;          ///< seconds a beam has been held since it last spent (`ammo-every`)
    double beamHeld = 0;          ///< seconds a beam has stored on `beamVictim` toward its next hit (`hit-every`)
    double beamFor = 0;           ///< seconds the beam has been held; 0 with no beam
    int beamVictim = -1;          ///< the body the beam is on; -1 for none
    double beamSince = 0;         ///< seconds since the beam was last on a body
    std::uint32_t burst = 0;      ///< shots fired since both triggers were last up
    double eyeHeight = EYE_HEIGHT; ///< its eye above its centre
    std::int32_t yaw = 0, pitch = 0; ///< the last command's aim
    bool fireHeld = false, altHeld = false;
    bool human = false; ///< a person plays it; a dummy or a bot does not (UTA-0020 SS 4.3)

    [[nodiscard]] bool alive() const { return health > 0; }
};

struct Shot {
    std::uint32_t kind = 0; ///< an index into the set's projectiles
    int owner = -1;         ///< the body that fired it
    Vec3 location{}, velocity{};
    Vec3 previous{};        ///< where its last tick began (the engine's OldLocation)
    Vec3 acceleration{};    ///< its kind's, along its launch direction
    std::int32_t yaw = 0, pitch = 0; ///< the aim it was launched along, before its kind's turn
    double lifetime = 0;    ///< its kind's, with its random part drawn
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

/// What a tick did that a client plays a sound for --
/// docs/specs/UTA-0467-weapon-sounds.md SS 4.3. One is raised whether or not
/// the set names a sound for it; soundOf decides.
struct Cue {
    enum class Kind : std::uint8_t { Fire, Select, Explode, Combo };
    Kind kind;
    int owner = -1;          ///< the body; a shot's firer
    std::uint32_t index = 0; ///< Fire, Select: the weapon. Explode: the projectile kind. Combo: the combo.
    int mode = -1;           ///< Fire: 0 primary, 1 alt; else -1
    Vec3 at{};               ///< where it is heard from
    bool operator==(const Cue&) const = default;
};

struct Game {
    std::vector<Body> bodies;
    std::vector<Shot> shots;
    std::uint64_t ticks = 0;
    std::uint64_t random = 0x9E3779B97F4A7C15u; ///< the one generator's state (SS 4.5)
    std::vector<Event> events;                   ///< this tick's; cleared at its start
    std::vector<Cue> cues;                       ///< this tick's; cleared at its start; not hashed
    /// What the last tick set: game seconds per wall second, over the tick's
    /// own speed. The caller advances its clock at it (UTA-0020 SS 4.3).
    double timeScale = 1;
};

/// INV-5: a hash of every integer in `game` and the bits of every double,
/// in a fixed order, so two states hash alike only where they are alike.
[[nodiscard]] std::uint64_t stateHash(const Game& game);

/// The sound path `cue` plays, or empty where its weapon, mode, projectile
/// or combo names none, or `cue` names nothing in `set`.
[[nodiscard]] std::string_view soundOf(const WeaponSet& set, const Cue& cue);

/// The fire mode of the weapon in `body`'s hand whose `loop-sound` sounds
/// now, 0 or 1; else -1. A beam's sounds while the beam is on. Another
/// mode's sounds while it winds up, while the press that wound it up is
/// still held, and from its first shot of a hold until the trigger is up
/// or its ammo is short. Never for a dead body or during a switch
/// (UTA-0467 SS 4.3).
[[nodiscard]] int loopingMode(const WeaponSet& set, const Body& body);

/// A body standing at `location`, carrying every weapon in `set` with its
/// starting ammo, the first by slot in hand.
[[nodiscard]] Body bodyAt(const WeaponSet& set, const Vec3& location, std::int32_t yaw);

/// Gives `body` every weapon in `set` with its most ammo.
void giveAll(const WeaponSet& set, Body& body);

/// The weapons `body` carries, in NextWeapon's order: by slot, the 0 key
/// last, then by file order. The weapon wheel shows these, clockwise from
/// the top (UTA-0020 SS 4.5).
[[nodiscard]] std::vector<int> wheelWeapons(const WeaponSet& set, const Body& body);

/// The direction of `yaw` and `pitch`, a unit vector (core/Angles.h).
[[nodiscard]] Vec3 aimOf(std::int32_t yaw, std::int32_t pitch);

/// SS 4.5: one tick. `commands` holds one per body, in order; a dead body's
/// is ignored. Each body switches weapon and moves, then each fires; then
/// the shots fly; then damage and momentum land, so no body's order changes
/// what another's shot hits. Last, `game.timeScale` is set: WHEEL_TIME_SCALE
/// where exactly one body is human, it is alive and its command has the
/// wheel open; else 1 (UTA-0020 SS 4.3).
void tick(const uworld::BodyWorld& world, const WeaponSet& set, const uworld::MoveRules& rules, Game& game,
          std::span<const uworld::MoveCommand> commands, double dt);

// The parts of a shot SS 4.5 and INV-8 and INV-9 name, for tests.

/// Where a segment from `from` to `to` enters an upright cylinder centred at
/// `centre`, as a fraction along it; or a value above 1 where it misses. A
/// segment starting inside enters at 0.
[[nodiscard]] double entersCylinder(const Vec3& from, const Vec3& to, const Vec3& centre, double radius,
                                    double halfHeight);

} // namespace uta::ugame
