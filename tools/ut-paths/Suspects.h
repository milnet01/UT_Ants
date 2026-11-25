// UTA-0514: the standing spots where UT is most likely to leave part of the
// frame undrawn, for UT_MonsterHunt's undrawn-frame probe (its GAME-0298).
//
// A spot is suspect when a standing player's eye there lies on the empty side
// of the map's render BSP but in no leaf: the AncientCaves control's
// condition, which the 2026-10-03 census found on 31 maps. A census, not a
// proof: nothing here shows UT draws badly at such a spot.

#pragma once

#include "Walkable.h"
#include "upkg/Geometry.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace uta::paths {

/// How far above a spot's centre a standing player's eye is: TMale1's
/// BaseEyeHeight, and the probe's own offset.
inline constexpr double EYE_HEIGHT = 27;
/// The side of the cubes suspect spots are grouped in, so one place is not
/// reported many times over.
inline constexpr double SUSPECT_CELL = 256;
/// The most spots the probe reads: MHUndrawnProbe.ini takes 16 AddSpots.
inline constexpr std::size_t MAX_SUSPECTS = 16;

/// Where a point falls in a model's render BSP.
struct BspSide {
    bool empty = false;     ///< on a front (empty) side
    std::int32_t leaf = -1; ///< that side's leaf; -1 for none
};

/// The side of the last plane `point` reaches, descending from node 0. A model
/// with no nodes, or one whose child index is out of range or loops, reads as
/// solid.
[[nodiscard]] BspSide bspSideAt(const upkg::Model& model, const Vec3& point);

struct Suspects {
    std::size_t standing = 0;  ///< every spot in the graph
    std::size_t leafless = 0;  ///< of those, the suspect ones
    std::size_t clusters = 0;  ///< SUSPECT_CELL cubes holding a suspect eye
    std::vector<Vec3> spots;   ///< one spot centre per cube, most suspects first
};

/// The suspect spots among all of `graph`'s, one per SUSPECT_CELL cube of the
/// eye, at most `most`. Equal cubes keep the lower X, then Y, then Z; each
/// cube is shown by its first spot in `graph` order. Spots no walk from the
/// start reaches count too: the walk graph has no lifts, jumps or
/// teleporters, and on AncientCaves, the known hole's map, no suspect spot is
/// reached by walking (measured 2026-11-25).
[[nodiscard]] Suspects suspectSpots(const upkg::Model& model, const WalkGraph& graph,
                                    std::size_t most = MAX_SUSPECTS);

/// `spots` as MHUndrawnProbe.ini lines, `AddSpots=(X=1,Y=2,Z=3)`, one per
/// spot, each coordinate rounded to a whole unit.
[[nodiscard]] std::string addSpotsLines(const std::vector<Vec3>& spots);

} // namespace uta::paths
