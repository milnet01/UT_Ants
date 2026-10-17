// Which drawn polygons sway, cut into a grid and weighted -- UTA-0055 SS 4.2
// and SS 4.3.
//
// docs/specs/UTA-0055-swaying-cloth-and-water.md.
//
// BAKE-SIDE ONLY, as the rest of uta_ubake. buildGeometry decides each
// polygon's kind (SS 4.1) and hands the moving ones here; what comes back is
// each polygon's convex pieces, every corner carrying its weight.
//
// NO CRACKS BY CONSTRUCTION. Polygons are cut by world planes, so two
// polygons sharing an edge are cut where the same planes cross it, and each
// crossing is computed from the edge's ends in one fixed order. Where a
// neighbour's corner lies inside a polygon's edge (a T-junction, common in
// BSP), that corner is first added to the polygon, so both carry it.

#pragma once

#include "ubundle/Bundle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace uta::ubake {

/// SS 4.2: the planes' spacing, in units, before the budget coarsens it.
inline constexpr double CLOTH_SLICE = 32.0;
inline constexpr double WATER_SLICE = 32.0;
/// SS 4.2: the coarsest spacing each kind is cut at; past it, it stays whole
/// and still.
inline constexpr double CLOTH_SLICE_MAX = 64.0;
inline constexpr double WATER_SLICE_MAX = 256.0;
/// SS 4.2: the most sliced vertices a map writes.
inline constexpr std::size_t SWAY_VERTEX_BUDGET = 524288;
/// SS 4.4: a spacing's cell byte is the spacing over this.
inline constexpr double SWAY_CELL_UNITS = 16.0;
/// SS 4.3: a hanging vertex this far below its patch's top is fully free.
inline constexpr double CLOTH_FULL_DROP = 256.0;
/// SS 4.3: a water or flat-cloth vertex this far from its patch's outline is fully free.
inline constexpr double EDGE_FADE = 64.0;
/// SS 4.1 and SS 4.3: a normal whose |z| reaches this faces up or down.
inline constexpr double FACING_UP = 0.7;

using SwayPoint = std::array<double, 3>;

/// One moving polygon, as buildGeometry draws it.
struct SwayPolygon {
    ubundle::SwayKind kind = ubundle::SwayKind::None; ///< Cloth or Water
    /// Polygons join a patch only within one group: buildGeometry passes the
    /// material, so two pools of one water picture that touch are one patch.
    std::string group;
    std::vector<SwayPoint> points; ///< convex, three or more, wound as drawn
    SwayPoint normal{};            ///< unit
};

/// One convex piece of a sliced polygon, wound as the polygon is.
struct SwayPiece {
    std::vector<SwayPoint> points;
    std::vector<std::uint8_t> weights; ///< one a point; 0 pinned .. 255 free
    /// One a point, unit: the way it moves, the mean of the normals of every
    /// moving polygon holding it, so a fold's two sides move together. The
    /// polygon's own normal where those cancel.
    std::vector<SwayPoint> directions;
    std::uint8_t cell = 0;             ///< SS 4.4: the spacing it was cut at, over SWAY_CELL_UNITS
};

struct SwaySlicing {
    /// Per input polygon, in order, its pieces. Empty where the budget left
    /// the polygon whole and still (SS 4.2).
    std::vector<std::vector<SwayPiece>> pieces;
    double waterSlice = WATER_SLICE; ///< the spacing water was cut at
    double clothSlice = CLOTH_SLICE; ///< and cloth
    bool waterStill = false;         ///< water was left still to keep the budget
    bool clothStill = false;         ///< and cloth too
    std::size_t vertices = 0;        ///< the points over every piece
    std::size_t clothVertices = 0;   ///< of them, cloth's
};

/// SS 4.2 and SS 4.3. A polygon whose kind is None is passed over and gets
/// no pieces.
[[nodiscard]] SwaySlicing sliceSway(std::span<const SwayPolygon> polygons);

} // namespace uta::ubake
