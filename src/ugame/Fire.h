// What Game.cpp and Fire.cpp share inside one tick. Not part of ugame's
// interface: nothing outside src/ugame includes it.

#pragma once

#include "ugame/Game.h"

#include <vector>

namespace uta::ugame::detail {

/// Damage and momentum owed to a body, landed at the end of the tick.
struct Owed {
    int body;
    double damage;
    Vec3 momentum;
    int instigator;
};

struct Tick {
    const uworld::BodyWorld& world;
    const WeaponSet& set;
    const uworld::MoveRules& rules;
    Game& game;
    double dt;
    std::vector<Owed> owed;
    std::vector<Shot> spawned;     ///< fly from the next tick
    std::vector<bool> shotGone;    ///< per game.shots, set when one is used up
};

/// The next draw from the game's generator: SplitMix64, integers only.
[[nodiscard]] std::uint64_t draw(Game& game);

/// An angle in [-spread, spread] units from the game's generator.
[[nodiscard]] std::int32_t spreadDraw(Game& game, std::uint32_t spread);

/// `yaw` and `pitch` each moved by a spread draw, the yaw's drawn first,
/// as a direction. The draws are sequenced here: two in one call's
/// arguments would come in an order each compiler picks (INV-5).
[[nodiscard]] Vec3 spreadAim(Game& game, std::int32_t yaw, std::int32_t pitch, std::uint32_t spread);

/// A decimal in [-range, range] from the game's generator; no draw for 0.
[[nodiscard]] double rangeDraw(Game& game, double range);

/// The eye of `body`.
[[nodiscard]] Vec3 eyeOf(const Body& body);

/// Where `mode`'s shot starts: the eye plus the mode's offset, turned by the aim.
[[nodiscard]] Vec3 startOf(const Body& body, const FireMode& mode);

/// A projectile of kind `kind` leaving `at` along `yaw` and `pitch`, after
/// the kind's own launch rules: its turn, speed and lifetime draws, its
/// lift, and its share of `firerVelocity`; its velocity then x `speedScale`.
/// It flies from the next tick.
void launch(Tick& tick, std::uint32_t kind, int owner, const Vec3& at, std::int32_t yaw, std::int32_t pitch,
            const Vec3& firerVelocity, double damageScale, double speedScale);

/// What a segment meets first: the level, a body, or a shot with a combo.
struct SegmentHit {
    double fraction = 1;
    int body = -1;
    int shot = -1;  ///< a live shot whose kind has a combo
    Vec3 normal{};  ///< the level's, where the level is what it met
    bool surface = false;
};

/// The first thing from `from` to `to`. `ignore` is a body passed through
/// (the firer); `combos` says whether a shot with a combo can be met.
[[nodiscard]] SegmentHit traceSegment(const Tick& tick, const Vec3& from, const Vec3& to, int ignore, bool combos);

/// UT99's HurtRadius: every live body within `radius` of `at` takes
/// `damage` and `momentum`, less with distance past its own radius.
void splash(Tick& tick, const Vec3& at, double damage, double momentum, double radius, int instigator);

/// The combo shot `shot` sets off when an instant hit from `instigator` meets it.
void setOffCombo(Tick& tick, int shot, int instigator);

/// SS 4.4: body `index` fires `mode` of its weapon in hand, its charge's
/// effect `scale`, and spends its ammo. The caller has checked the weapon
/// holds at least `mode.ammo` (INV-7): Game.cpp's fireBody, before a shot
/// or a charge starts.
void fire(Tick& tick, int index, const FireMode& mode, double scale);

/// SS 4.4's beam for one tick: ammo by the second, damage by the second.
void beam(Tick& tick, int index, const FireMode& mode);

/// Whether a charging melee's segment touches anything now, which fires it.
[[nodiscard]] bool meleeTouches(const Tick& tick, int index, const FireMode& mode);

} // namespace uta::ugame::detail
