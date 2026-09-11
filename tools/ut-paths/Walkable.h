// Where a player can stand, and where they can walk --
// docs/specs/UTA-0121-bot-path-seeds.md SS 4.5, and SS 4.6's placing.
//
// SAMPLED, NOT SWEPT (that spec's SS 8): a body is a set of points tested
// empty, and a join a set of segments traced clear.

#pragma once

#include "Trace.h"
#include "ubundle/Bundle.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace uta::paths {

/// SS 3 decision 4's body: Botpack.TMale1's CollisionRadius, its
/// CollisionHeight (a half-height) and its MaxStepHeight.
inline constexpr double RADIUS = 17;
inline constexpr double HALF_HEIGHT = 39;
inline constexpr double STEP = 25;
/// SS 3 decision 6: a floor's normal has at least this Z.
inline constexpr double FLOOR_Z = 0.7;
/// SS 4.5: the columns' spacing on X and on Y.
inline constexpr double COLUMN = 32;

/// Where a body stands, HALF_HEIGHT above its floor.
struct Spot {
    Vec3 centre;
    Vec3 floorNormal;        ///< the floor hit's, facing up
    std::int32_t column = 0; ///< its column's index on X
    std::int32_t row = 0;    ///< and on Y
};

/// Every spot, and which join which.
struct WalkGraph {
    struct Range {
        std::uint32_t begin = 0, end = 0;
    };

    Vec3 origin;                ///< column 0 and row 0's X and Y
    std::int32_t columns = 0;
    std::int32_t rows = 0;
    std::vector<Spot> spots;    ///< by column, then row, each cell's top down
    std::vector<std::vector<std::uint32_t>> joins; ///< per spot, the spots it joins
    std::vector<std::uint32_t> cellStart;          ///< where each cell's run starts, and one past the last

    /// The spots of one cell; none outside the grid.
    [[nodiscard]] Range cell(std::int32_t column, std::int32_t row) const noexcept;
};

/// SS 4.5, over the bounding box of `tree`'s points.
[[nodiscard]] WalkGraph walkGraph(const ubundle::CollisionTree& tree);

/// SS 4.6's placing: the spot nearest `location` among spots within 64 of it
/// horizontally whose centre is within HALF_HEIGHT + STEP of it vertically.
[[nodiscard]] std::optional<std::uint32_t> place(const WalkGraph& graph, const Vec3& location);

} // namespace uta::paths
