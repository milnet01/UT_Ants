// A weapon set: a hand-editable text file of weapon definitions --
// docs/specs/UTA-0019-weapon-set.md SS 4.2 and SS 4.4.
//
// A definition is numbers plus one of a few firing archetypes. A new weapon
// built from existing archetypes is a new file, not new code; a new
// archetype is a code change. Units are UT99's: unrealunits, game seconds,
// and angle units, 65536 to a turn.

#pragma once

#include "core/Error.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace uta::ugame {

/// The newest weapon-set version this game reads and writes.
inline constexpr std::uint32_t WEAPON_SET_VERSION = 1;

/// SS 4.4: the behaviours that genuinely differ.
enum class Archetype : std::uint8_t { InstantHit, Projectile, Beam, Melee };

/// What charging scales: an instant hit's or a melee's damage and momentum,
/// or how many projectiles a projectile mode spawns.
enum class ChargeScales : std::uint8_t { Damage, Count };

/// SS 4.4's charge modifier: held to build up, fired on release. A count
/// charge also fires once its count is loaded or its ammo is spent. The
/// press spends the mode's `ammo`.
///
/// Continuous (`step` 0): the effect is `min + (max - min) * min(held /
/// time, 1)` times the mode's, and a count charge spends the rest of its
/// rounds when it fires. Stepped: each time the hold since the last step
/// passes `step`, one more `ammo` is spent and the charge grows -- a count
/// charge by one projectile, a damage charge by that hold, as UT_BioRifle and
/// UT_Eightball load (SS 4.9). The press's own tick counts toward the first
/// step. A count charge's hold past a step counts toward the next (its
/// animation runs on); a damage charge's starts again from nothing
/// (UT_BioRifle's Count).
struct Charge {
    double time = 1;      ///< seconds from empty to full
    double min = 1, max = 1;
    double step = 0;      ///< seconds a step takes; 0 for a continuous charge
    double stepLast = 0;  ///< seconds the step that fills a count takes; 0 for `step`
    ChargeScales scales = ChargeScales::Damage;
    bool operator==(const Charge&) const = default;
};

/// SS 3 item 6: behaviour of the original's no key can say, by name. Each
/// belongs to one archetype; Fire.cpp holds what each does.
enum class FireRule : std::uint8_t {
    None,
    RocketFan,      ///< projectile: UT_Eightball's rockets, around a circle and fanned in yaw
    GrenadeCluster, ///< projectile: UT_Eightball's grenades, around a wider circle, each turned at random
    Hammer,         ///< melee: ImpactHammer's charge -- touch fires past a charge of 1, a surface counts it as 1
};

/// One fire mode: a `[fire WEAPON primary|alt]` section. Which of the
/// archetype-specific fields mean anything depends on `archetype`; the
/// parser refuses another archetype's key.
struct FireMode {
    Archetype archetype = Archetype::InstantHit;
    /// Where the shot starts from the eye, turned by the aim: forward,
    /// right, up. The weapon's view offset plus the part of its fire offset
    /// the original's class adds (SS 4.9).
    std::array<double, 3> offset{};
    double refire = 0;        ///< game seconds between shots while held
    /// Added once, after a shot fired from idle: the firing animation's
    /// blend-in, or below 0 a first interval shorter than the rest (ChainSaw).
    double refireLead = 0;
    double windUp = 0;        ///< game seconds from a press at idle to its shot, which a release does not stop
    std::uint32_t ammo = 0;   ///< spent per shot; per `ammoEvery` for a beam
    std::uint32_t pressAmmo = 0; ///< spent by the press that starts a wind-up or a beam, with no shot
    /// A hold on a wound-up mode presses again this often, counted from its
    /// press: it pays `pressAmmo` and winds up again. 0 for never.
    double repress = 0;
    /// A hold on a wound-up mode fires every `spinRefire` until it has been
    /// held this long, and every `refire` from the shot after. 0 for no spin-up.
    double spinUp = 0;
    double spinRefire = 0;
    std::uint32_t spread = 0; ///< the cone's half-angle, in angle units
    std::optional<Charge> charge;
    double damage = 0;   ///< instant-hit, melee; per second for a beam
    double momentum = 0; ///< instant-hit, melee (below 0 pulls); per second for a beam
    double range = 0;    ///< instant-hit, beam, melee
    double hitEvery = 0; ///< beam: seconds of hold it stores on a body before it hurts; 0 every tick
    double ammoEvery = 1; ///< beam: seconds of hold that spend `ammo`
    double extend = 0;    ///< beam: units a second it grows from nothing to `range`; 0 for at once
    double firstHit = 0;  ///< beam: seconds of hold a body takes as the beam first touches it
    std::uint32_t pellets = 1;            ///< instant-hit
    std::uint32_t damageRandom = 0;       ///< instant-hit: a whole number from 0 to this, added
    double pushChance = 0;                ///< instant-hit: of hits whose momentum is scaled by `pushScale`
    double pushScale = 1;                 ///< instant-hit
    std::optional<double> headDamage;     ///< instant-hit: a head hit's damage, in place of `damage`
    double headMomentum = 0;              ///< instant-hit: a head hit's momentum, with `headDamage`
    std::string projectile;               ///< projectile: the kind it spawns
    std::uint32_t count = 1;              ///< projectile
    double spiralRadius = 0;              ///< projectile: its start circles the offset at this radius
    std::uint32_t spiralTurn = 0;         ///< projectile: angle units round that circle a shot of one hold
    double momentumSide = 0;              ///< melee: momentum along the firer's right; below 0, its left
    bool rangeFromBody = false;           ///< melee: `range` runs from the firer's centre, not the shot's start
    double selfMomentum = 0;              ///< melee: pushes the firer back off a surface
    double selfDamage = 0;                ///< melee: what a surface hit costs the firer
    double reach = 0;                     ///< melee: no body farther than this from the firer is hurt; 0 any
    double distanceScale = 0;             ///< melee: the effect x the hit's distance / this; 0 none
    FireRule rule = FireRule::None;       ///< `rule`: a named behaviour of the archetype
    bool operator==(const FireMode&) const = default;
};

struct Weapon {
    std::string name;      ///< lower-case kebab words, unique in the set
    std::string className; ///< the UT99 class it stands for, `package.class`, folded
    std::uint32_t slot = 0; ///< its number key, 0 to 9; ties order by file order
    std::uint32_t ammoMax = 0, ammoStart = 0;
    double switchTime = 0; ///< seconds to bring it up
    FireMode primary;
    std::optional<FireMode> alt;
    bool operator==(const Weapon&) const = default;
};

/// What a projectile does on striking a surface or a body.
enum class Strike : std::uint8_t { Explode, Bounce, Stick, Split };

struct Projectile {
    std::string name;
    double speed = 0, radius = 0;
    double gravity = 0; ///< of the world's gravity, gained before a tick's move: 0 flies straight
    double gravityAfter = 0; ///< of the world's gravity, gained after a tick's move
    double acceleration = 0;   ///< along its launch direction, per second squared
    double maxSpeed = 0;       ///< what acceleration stops at; 0 for no limit
    double lift = 0;           ///< added upward at launch
    double inherit = 0;        ///< of the firer's velocity along the aim, added at launch
    double speedRandom = 0;    ///< a launch speed from `speed` less this to `speed` plus this
    std::uint32_t turnRandom = 0; ///< its yaw and pitch each turned by up to this, angle units
    double lifetime = 0;
    double lifetimeRandom = 0; ///< a lifetime from `lifetime` less this to `lifetime` plus this
    bool expireExplodes = false; ///< at the end of its lifetime: explodes, else vanishes
    double damage = 0, momentum = 0;
    double splashRadius = 0; ///< 0 hits only what it strikes
    double splashRadiusMax = 0; ///< above 0: the radius grows by a charge's scale, up to this
    /// Striking a body, the blast centres where its whole move this tick
    /// ends (the script's `Location`), not where it met the body.
    bool splashFromMoved = false;
    /// Striking a body hits that body alone, along its flight; the blast
    /// is for the level only (Razor2Alt).
    bool bodyDirect = false;
    double splashUpMax = 1; ///< the blast's push direction's upward part, at most this
    Strike strike = Strike::Explode;
    std::uint32_t bounces = 0; ///< bounce: before it explodes; then it explodes
    double bounceKeep = 0;     ///< bounce: of its speed along the surface, kept per bounce
    double bounceOff = 0;      ///< bounce: of its speed off the surface, kept per bounce: the middle of a draw
    double bounceOffRandom = 0; ///< bounce: kept from `bounceOff` less this to `bounceOff` plus this
    /// bounce: from its first bounce it falls at this of the world's gravity,
    /// in place of `gravity`, and in that bounce's tick moves a whole tick more.
    std::optional<double> bounceGravity;
    double fuse = 0;           ///< stick: seconds stuck before it explodes
    std::string splitInto;     ///< split: the kind it breaks into, after exploding
    std::uint32_t splitCount = 0;
    std::uint32_t splitSpread = 0; ///< split: the pieces' cone, angle units
    std::string combo;         ///< what an instant hit on it sets off; empty for none
    bool operator==(const Projectile&) const = default;
};

/// What a combo does where it goes off: an explosion.
struct Combo {
    std::string name;
    double damage = 0, momentum = 0, splashRadius = 0;
    bool operator==(const Combo&) const = default;
};

struct WeaponSet {
    std::string name;
    double selfDamage = 1;  ///< of the damage the firer's own shots do it
    double damageScale = 1; ///< every hit's damage, after truncating to a whole number: hardcore's 1.5
    double headHeight = 0;  ///< of a body's half-height above its centre where its head starts
    std::vector<Weapon> weapons;
    std::vector<Projectile> projectiles;
    std::vector<Combo> combos;
    bool operator==(const WeaponSet&) const = default;

    [[nodiscard]] const Projectile* projectile(std::string_view name) const;
    [[nodiscard]] const Combo* combo(std::string_view name) const;
};

/// SS 4.2: strict. An unknown section or key, a repeated one, an
/// out-of-range value, a missing required key, another archetype's key and a
/// reference to a missing name are `MalformedData` naming the line; a newer
/// version is `UnsupportedVersion`.
[[nodiscard]] Result<WeaponSet> parse(std::string_view text);

/// The canonical form; `parse(write(set))` equals `set` (INV-2).
[[nodiscard]] std::string write(const WeaponSet& set);

/// The directory the game's own sets ship in, `weapons/` (SS 4.2).
[[nodiscard]] std::filesystem::path shippedDirectory();

/// Reads and parses one set file.
[[nodiscard]] Result<WeaponSet> load(const std::filesystem::path& file);

} // namespace uta::ugame
