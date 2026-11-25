// The placed actors that block a body -- docs/specs/UTA-0457-body-collision.md
// SS 4.7 (UTA-0377). UT99 stops a player at an actor whose bCollideActors and
// bBlockPlayers are both true, by that actor's own upright cylinder: barrels,
// chairs, BlockAll and BlockPlayer among them.

#pragma once

#include "ubundle/Bundle.h"
#include "uworld/CollisionQuery.h"

#include <vector>

namespace uta::uworld {

/// One actor's collision cylinder: `radius` across and `halfHeight` above and
/// below `location`, UT99's CollisionRadius and CollisionHeight. It neither
/// turns nor scales with the actor.
struct Blocker {
    Vec3 location{};
    double radius = 0, halfHeight = 0;
};

/// Every placed actor that blocks a player, in placement order. Left out: an
/// actor whose class did not resolve, one that is or descends from a pawn, a
/// mover or a projectile, and one whose cylinder is empty or not finite.
[[nodiscard]] std::vector<Blocker> blockersOf(const ubundle::Placements& placements);

} // namespace uta::uworld
