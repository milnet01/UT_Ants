// Whether a placed actor's collision cylinder sits inside solid BSP, and how
// deep -- UTA-0522, docs/specs/UTA-0012-ut-dump-output-shape.md SS 4.7g.
// UT_MonsterHunt's Map Repair asks it of every placed monster, from the file
// alone.

#pragma once

#include "ubundle/Bundle.h"
#include "uworld/CollisionQuery.h"

#include <optional>

namespace uta::dump {

/// How far below a centre `floorGap` looks: MHStuckMonsters' trace length.
inline constexpr double FLOOR_REACH = 10000;
/// A body whose centre is at least its half-height less this above the floor
/// stands on it. MHStuckMonsters writes 4; the engine's floor hit sits a
/// fraction higher than this tree's, and 3.5 matched its readings best:
/// against 613 monsters it read embedded on 437 maps (UT_MonsterHunt, engine
/// run 2026-09-06), 4 caught 474, 3.5 caught 553 and flagged no more free
/// ones, and 3 flagged 21 more (measured 2026-11-27).
inline constexpr double SINK_SLACK = 3.5;
/// The farthest `depth` pushes a cylinder before giving up.
inline constexpr double MAX_PUSH = 512;

struct EmbeddedReading {
    /// The centre is in solid.
    bool centreSolid = false;
    /// From the centre straight down to the first solid, within FLOOR_REACH;
    /// 0 where the centre is solid, and absent where nothing is below.
    std::optional<double> floorGap;
    /// MHStuckMonsters' embedded test: `floorGap` < halfHeight - SINK_SLACK.
    /// It is the engine's, so a body clipping a wall is not embedded by it.
    bool embedded = false;
    /// The shortest push, up, down or along one of eight level directions,
    /// that leaves the cylinder in empty space: 0 where it already is, and
    /// absent where no push up to MAX_PUSH does.
    std::optional<double> depth;
};

/// The reading of an upright cylinder of `radius`, reaching `halfHeight`
/// above and below `centre`; both finite and positive. The cylinder tested is
/// 1 smaller on every side, so a body resting on a floor or against a wall is
/// free.
[[nodiscard]] EmbeddedReading readEmbedded(const ubundle::CollisionTree& tree, const uworld::Vec3& centre,
                                           double radius, double halfHeight);

} // namespace uta::dump
