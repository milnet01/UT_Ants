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

/// SS 4.4's charge modifier: held to build up, fired on release or once full.
/// The effect is `min + (max - min) * min(held / time, 1)` times the mode's.
struct Charge {
    double time = 1;      ///< seconds from empty to full
    double min = 1, max = 1;
    ChargeScales scales = ChargeScales::Damage;
    bool operator==(const Charge&) const = default;
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
    std::uint32_t ammo = 0;   ///< spent per shot; per second for a beam
    std::uint32_t spread = 0; ///< the cone's half-angle, in angle units
    std::optional<Charge> charge;
    double damage = 0;   ///< instant-hit, melee; per second for a beam
    double momentum = 0; ///< instant-hit, melee; per second for a beam
    double range = 0;    ///< instant-hit, beam, melee
    double hitEvery = 0; ///< beam: seconds of hold it stores before it hurts; 0 every tick
    std::uint32_t pellets = 1;            ///< instant-hit
    std::uint32_t damageRandom = 0;       ///< instant-hit: a whole number from 0 to this, added
    double pushChance = 0;                ///< instant-hit: of hits whose momentum is scaled by `pushScale`
    double pushScale = 1;                 ///< instant-hit
    std::optional<double> headDamage;     ///< instant-hit: a head hit's damage, in place of `damage`
    double headMomentum = 0;              ///< instant-hit: a head hit's momentum, with `headDamage`
    std::string projectile;               ///< projectile: the kind it spawns
    std::uint32_t count = 1;              ///< projectile
    double selfMomentum = 0;              ///< melee: pushes the firer back off a surface
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
    double gravity = 0; ///< of the world's gravity: 0 flies straight, 1 falls
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
    Strike strike = Strike::Explode;
    std::uint32_t bounces = 0; ///< bounce: before it explodes; then it explodes
    double bounceKeep = 0;     ///< bounce: of its speed kept per bounce
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
