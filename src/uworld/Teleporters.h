// Teleporters in one level -- UTA-0205. UT99's Teleporter.Touch and Accept
// (Engine.u, UT 469): a body stepping into an enabled teleporter whose URL
// names another teleporter's Tag lands at that teleporter, turned and with its
// velocity turned as the destination's flags say.
//
// A map's own subclass may replace Touch (UTA-0463). Those that replace only
// the effect send as Teleporter does. swJumpPad (Switch`, 2005) and its copy
// BCEJumpPad launch the body at the actor their URL names instead, by their
// own CalcVelocity, which the map carries as source. Any other subclass is a
// destination only: the bundle carries no script to say what it does.
//
// Not here: a URL naming another level, Trigger switching a teleporter on or
// off (UTA-0142), the Event a teleport or a jump fires, and the effects and
// sound.

#pragma once

#include "ubundle/Bundle.h"
#include "uworld/BodyCollision.h"
#include "uworld/CollisionQuery.h"
#include "uworld/Movement.h"

#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace uta::uworld {

/// A jump pad's own settings: swJumpPad's `JumpPad` group, and BCEJumpPad's
/// bBotsOnly.
struct JumpPad {
    double angle = 45, angleRand = 0;    ///< JumpAngle, AngleRand: degrees
    std::uint8_t angleMode = 0;          ///< AngleRandMode: AM_Random, AM_Extremes, AM_Owned
    double minAngle = 1, maxAngle = 89;  ///< the angle's clamp, degrees
    double minAngleCurve = 3, minAngleFinder = 0.75;
    double targetZOffset = 0;
    Vec3 targetRand{};                   ///< scales a random unit vector added to the target
    bool traceGround = true;             ///< aim at the ground below the target
    bool teamOnly = false, botsOnly = false;
    std::uint8_t teamNumber = 0;
    double wait = 1;                     ///< the class's JumpWait: no relaunch of the same body sooner
    /// The actor the URL names: the last placed actor whose Tag matches,
    /// itself excepted, as PostTouch's AllActors loop leaves it. None: the pad
    /// launches nothing.
    std::optional<Vec3> target;
    double targetHeight = 0;             ///< that actor's CollisionHeight
};

/// One teleporter, its fields resolved from the actor and its class.
struct Teleporter {
    Vec3 location{};
    std::int32_t yaw = 0;          ///< its Rotation's, 65536 to a turn
    std::string tag, url;          ///< case-folded; UT compares them with ~=
    bool enabled = true;           ///< bEnabled; off, Touch does nothing
    /// Whether touching it can send a body on: bCollideActors, a URL, and a
    /// class whose Touch this reads -- stock, effects-only or a jump pad.
    bool sends = true;
    double radius = 0, height = 0; ///< its collision cylinder: radius, half height
    bool changesYaw = false, changesVelocity = false;
    bool reversesX = false, reversesY = false, reversesZ = false;
    Vec3 targetVelocity{};
    /// Set for a jump pad: touching it launches rather than teleports, and
    /// its Accept refuses every arrival.
    std::optional<JumpPad> jumpPad;
};

/// Where a body lands from a teleport, or how a jump pad launched it.
struct Arrival {
    Vec3 location{}, velocity{};
    double yaw = 0; ///< the view's new yaw, 65536 to a turn
    /// A jump pad's launch: the body stays where it is, now falling with
    /// `velocity`, and the view does not jump.
    bool launched = false;
};

class Teleporters {
public:
    /// Every placed actor of class Engine.Teleporter or a subclass.
    /// Botpack.VisibleTeleporter, UnrealShare.FavoritesTeleporter and the
    /// effects-only subclasses keep Teleporter's Touch, so they send as it
    /// does; a jump pad finds its target among every placed actor.
    [[nodiscard]] static Teleporters fromPlacements(const ubundle::Placements& placements);

    Teleporters() = default;
    explicit Teleporters(std::vector<Teleporter> list);

    [[nodiscard]] const std::vector<Teleporter>& list() const noexcept { return list_; }

    /// After a tick that left a body of `size` at `location`: the teleport,
    /// if it stepped into an enabled teleporter it was not already touching
    /// and that teleporter's URL names another's Tag. Among several such
    /// destinations one is picked at random, as UT does. A destination the
    /// body does not fit in leaves it where it is, as Pawn.SetLocation fails.
    /// The body then counts as touching whatever overlaps it where it ends,
    /// so it does not bounce straight back.
    ///
    /// UT turns the body's velocity by its Rotation, whose pitch is 0 while
    /// walking; this uses 0 falling too, and leaves the view's pitch alone
    /// where UT clamps it to the body's.
    ///
    /// A jump pad instead launches the body at its target, by `rules`'
    /// gravity, and not again within its wait of game time; `dt` is the
    /// tick's, which that clock counts. The player has no team (UT's 255) and
    /// is not a bot.
    [[nodiscard]] std::optional<Arrival> touch(const BodyWorld& world, const BodySize& size, const Vec3& location,
                                               const Vec3& velocity, double yaw, const MoveRules& rules = {},
                                               double dt = 0);

private:
    void markTouching(const BodySize& size, const Vec3& at);
    [[nodiscard]] std::optional<Arrival> launch(std::size_t pad, const BodyWorld& world, const BodySize& size,
                                                const Vec3& location, const MoveRules& rules);

    std::vector<Teleporter> list_;
    std::vector<bool> touching_;
    std::vector<bool> extremeSwitch_;  ///< a jump pad's bSwitchAngle, for AM_Extremes
    std::vector<double> launchedAt_;   ///< game time of a jump pad's last launch
    double now_ = 0;                   ///< game seconds, summed from touch's `dt`
    std::minstd_rand pick_{1}; ///< UT's rand(); seeded, so a run repeats
};

} // namespace uta::uworld
