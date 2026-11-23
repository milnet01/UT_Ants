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
    Vec3 velocity{}; ///< added first, as Pawn.AddVelocity (SS 4.11's wave)
};

struct Tick {
    const uworld::BodyWorld& world;
    const WeaponSet& set;
    const uworld::MoveRules& rules;
    Game& game;
    double dt;
    std::vector<Owed> owed;
    std::vector<Shot> spawned;     ///< fired or split this tick; they fly this tick too
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

/// Where `mode`'s shot starts: the eye plus the mode's offset, turned by the
/// aim. A doubled weapon's second gun is held in the other hand: its offset
/// to the right is mirrored (Enforcer.SetHand).
[[nodiscard]] Vec3 startOf(const Body& body, const FireMode& mode, bool second = false);

/// A projectile of kind `kind` leaving `at` along `yaw` and `pitch`, after
/// the kind's own launch rules: its turn, speed and lifetime draws, its
/// lift, and its share of `firerVelocity`; its velocity then x `speedScale`.
/// It flies this tick, after the shots already in flight.
void launch(Tick& tick, std::uint32_t kind, int owner, const Vec3& at, std::int32_t yaw, std::int32_t pitch,
            const Vec3& firerVelocity, double damageScale, double speedScale);

/// What a segment meets first: the level, a body, or a shot an instant hit
/// meets: one with a combo, or a disc (SS 4.10).
struct SegmentHit {
    double fraction = 1;
    int body = -1;
    int shot = -1;  ///< a live shot whose kind has a combo, or a disc
    Vec3 normal{};  ///< the level's, where the level is what it met
    bool surface = false;
};

/// The first thing from `from` to `to`. `ignore` is a body passed through
/// (the firer); `shots` says whether a shot with a combo, or a disc, can be met.
[[nodiscard]] SegmentHit traceSegment(const Tick& tick, const Vec3& from, const Vec3& to, int ignore, bool shots);

/// Whether shots of `kind` are discs that instant hits and splashes meet (SS 4.10).
[[nodiscard]] bool isTarget(const Projectile& kind);

/// Whether instant hits and splashes meet shots of `kind`: discs, and shells
/// that can be shot down (SS 4.11).
[[nodiscard]] bool canBeHit(const Projectile& kind);

/// SS 4.11, WarShell.TakeDamage: shot `shot` takes `damage`, hit at `at`;
/// past its kind's `shotDown`, it bursts there, as a splash of its kind's
/// damage over `shotDownRadius`, with no wave.
void hurtShot(Tick& tick, int shot, double damage, const Vec3& at);

/// A cylinder's half-height for `kind`: its own, or its radius.
[[nodiscard]] double halfHeightOf(const Projectile& kind);

/// SS 4.10, TranslocatorTarget.TakeDamage: disc `shot` takes `damage`,
/// unscaled, into its disruption, and `momentum` knocks it into flight.
void knockDisc(Tick& tick, int shot, double damage, const Vec3& momentum);

/// UT99's HurtRadius: every live body within `radius` of `at` takes
/// `damage` and `momentum`, less with distance past its own radius; so does
/// every disc (SS 4.10). The
/// push direction's upward part is at most `upMax`, left unnormalised, as
/// Razor2Alt.BlowUp does.
void splash(Tick& tick, const Vec3& at, double damage, double momentum, double radius, int instigator,
            double upMax = 1);

/// The combo shot `shot` sets off when an instant hit from `instigator` meets it.
void setOffCombo(Tick& tick, int shot, int instigator);

/// SS 4.4: body `index` fires `mode` of its weapon in hand, its charge's
/// effect `scale`, and spends its ammo. The caller has checked the weapon
/// holds at least `mode.ammo` (INV-7): Game.cpp's fireBody, before a shot
/// or a charge starts. `second`: a doubled weapon's second gun fires it,
/// from its own side, its spread grown by its own shots (UTA-0468). A projectile mode
/// with a lock fires its lock kind at `body.lockTarget` where it is set.
void fire(Tick& tick, int index, const FireMode& mode, double scale, bool second = false);

/// UTA-0468, Pawn.PickTarget: the live body other than `index` nearest
/// `mode`'s aim from its start, within its lock-aim and lock-range, in
/// sight of `index`'s eye; -1 for none.
[[nodiscard]] int lockPick(const Tick& tick, int index, const FireMode& mode);

/// SS 4.4's beam for one tick: ammo by the second, damage by the second.
void beam(Tick& tick, int index, const FireMode& mode);

/// Whether a charging melee's segment touches anything now, which fires it.
[[nodiscard]] bool meleeTouches(const Tick& tick, int index, const FireMode& mode);

} // namespace uta::ugame::detail
