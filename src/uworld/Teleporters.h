// Teleporters in one level -- UTA-0205. UT99's Teleporter.Touch and Accept
// (Engine.u, UT 469): a body stepping into an enabled teleporter whose URL
// names another teleporter's Tag lands at that teleporter, turned and with its
// velocity turned as the destination's flags say.
//
// Not here: a URL naming another level, Trigger switching a teleporter on or
// off (UTA-0142), the Event a teleport fires, and the effects and sound.

#pragma once

#include "ubundle/Bundle.h"
#include "uworld/BodyCollision.h"
#include "uworld/CollisionQuery.h"

#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace uta::uworld {

/// One teleporter, its fields resolved from the actor and its class.
struct Teleporter {
    Vec3 location{};
    std::int32_t yaw = 0;          ///< its Rotation's, 65536 to a turn
    std::string tag, url;          ///< case-folded; UT compares them with ~=
    bool enabled = true;           ///< bEnabled; off, Touch does nothing
    /// Whether touching it can send a body on: bCollideActors, a URL, and a
    /// stock class. A map's own subclass may replace Touch -- jump pads do --
    /// so it is a destination only until UTA-0463.
    bool sends = true;
    double radius = 0, height = 0; ///< its collision cylinder: radius, half height
    bool changesYaw = false, changesVelocity = false;
    bool reversesX = false, reversesY = false, reversesZ = false;
    Vec3 targetVelocity{};
};

/// Where a body lands from a teleport.
struct Arrival {
    Vec3 location{}, velocity{};
    double yaw = 0; ///< the view's new yaw, 65536 to a turn
};

class Teleporters {
public:
    /// Every placed actor of class Engine.Teleporter or a subclass.
    /// Botpack.VisibleTeleporter and UnrealShare.FavoritesTeleporter keep
    /// Teleporter's Touch, so they send as it does.
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
    [[nodiscard]] std::optional<Arrival> touch(const BodyWorld& world, const BodySize& size, const Vec3& location,
                                               const Vec3& velocity, double yaw);

private:
    void markTouching(const BodySize& size, const Vec3& at);

    std::vector<Teleporter> list_;
    std::vector<bool> touching_;
    std::minstd_rand pick_{1}; ///< UT's rand(); seeded, so a run repeats
};

} // namespace uta::uworld
