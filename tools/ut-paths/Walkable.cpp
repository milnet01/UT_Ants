// Where a player can stand, and where they can walk --
// docs/specs/UTA-0121-bot-path-seeds.md SS 4.5, INV-3 and INV-4; and SS 4.6's
// placing.

#include "Walkable.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>

namespace uta::paths {
namespace {

using ubundle::CollisionTree;

/// SS 4.5's three heights from a centre: one above the step, the middle, and
/// one under the head.
constexpr std::array<double, 3> HEIGHTS = {-HALF_HEIGHT + STEP + 1, 0, HALF_HEIGHT - 1};

/// SS 4.6: how far from an actor its spot may be.
constexpr double PLACE_ACROSS = 64;
constexpr double PLACE_UP = HALF_HEIGHT + STEP;

/// Two floor normals are one plane's to within float rounding.
constexpr double SAME_NORMAL = 1e-4;

/// SS 4.5's fit: the centre, the centre raised and lowered by H - 1, and
/// eight points on the radius-R circle at each of the three heights, all empty.
bool fits(const CollisionTree& tree, const Vec3& centre) {
    if (!isEmpty(tree, centre) || !isEmpty(tree, centre + Vec3{0, 0, HALF_HEIGHT - 1})
        || !isEmpty(tree, centre - Vec3{0, 0, HALF_HEIGHT - 1}))
        return false;
    for (int k = 0; k < 8; ++k) {
        const double angle = k * std::numbers::pi / 4;
        const Vec3 out{RADIUS * std::cos(angle), RADIUS * std::sin(angle), 0};
        for (const double height : HEIGHTS)
            if (!isEmpty(tree, centre + out + Vec3{0, 0, height})) return false;
    }
    return true;
}

/// SS 4.5's floors in one column, top down, each kept spot appended.
void standings(const CollisionTree& tree, double x, double y, double top, double bottom,
               std::int32_t column, std::int32_t row, std::vector<Spot>& spots) {
    for (double z = top; z > bottom;) {
        const Hit hit = trace(tree, {x, y, z}, {x, y, bottom});
        if (hit.fraction >= 1) return;
        const double floor = z + (bottom - z) * hit.fraction;
        if (hit.normal.z >= FLOOR_Z) {
            const Vec3 centre{x, y, floor + HALF_HEIGHT};
            if (fits(tree, centre)) spots.push_back(Spot{centre, hit.normal, column, row});
        }
        // One below the hit, then down to where space is empty again.
        const double below = floor - 1;
        if (below <= bottom) return;
        const Hit out = traceOut(tree, {x, y, below}, {x, y, bottom});
        if (out.fraction >= 1) return;
        z = below + (bottom - below) * out.fraction;
    }
}

/// SS 4.5's ramp: the floors' normals match, and each floor hit lies within 1
/// of the other's plane.
bool onePlane(const Spot& a, const Spot& b) {
    const Vec3 difference = a.floorNormal - b.floorNormal;
    if (std::abs(difference.x) > SAME_NORMAL || std::abs(difference.y) > SAME_NORMAL
        || std::abs(difference.z) > SAME_NORMAL)
        return false;
    const Vec3 lift{0, 0, HALF_HEIGHT};
    const Vec3 floorA = a.centre - lift;
    const Vec3 floorB = b.centre - lift;
    return std::abs(dot(b.floorNormal, floorA - floorB)) <= 1
           && std::abs(dot(a.floorNormal, floorB - floorA)) <= 1;
}

/// SS 4.5's join: within a step or along one plane, and the three segments
/// between them clear.
bool joins(const CollisionTree& tree, const Spot& a, const Spot& b) {
    if (std::abs(a.centre.z - b.centre.z) > STEP && !onePlane(a, b)) return false;
    for (const double height : HEIGHTS) {
        const Vec3 lift{0, 0, height};
        if (trace(tree, a.centre + lift, b.centre + lift).fraction < 1) return false;
    }
    return true;
}

} // namespace

WalkGraph::Range WalkGraph::cell(std::int32_t column, std::int32_t row) const noexcept {
    if (column < 0 || row < 0 || column >= columns || row >= rows) return {};
    const auto at = static_cast<std::size_t>(column) * static_cast<std::size_t>(rows)
                    + static_cast<std::size_t>(row);
    return {cellStart[at], cellStart[at + 1]};
}

WalkGraph walkGraph(const CollisionTree& tree) {
    WalkGraph graph;
    graph.cellStart = {0};
    if (tree.points.empty() || tree.nodes.empty()) return graph;

    Vec3 low{tree.points[0][0], tree.points[0][1], tree.points[0][2]};
    Vec3 high = low;
    for (const auto& point : tree.points) {
        low = {std::min<double>(low.x, point[0]), std::min<double>(low.y, point[1]),
               std::min<double>(low.z, point[2])};
        high = {std::max<double>(high.x, point[0]), std::max<double>(high.y, point[1]),
                std::max<double>(high.z, point[2])};
    }
    graph.origin = {low.x, low.y, 0};
    graph.columns = static_cast<std::int32_t>(std::floor((high.x - low.x) / COLUMN)) + 1;
    graph.rows = static_cast<std::int32_t>(std::floor((high.y - low.y) / COLUMN)) + 1;

    graph.cellStart.clear();
    for (std::int32_t column = 0; column < graph.columns; ++column)
        for (std::int32_t row = 0; row < graph.rows; ++row) {
            graph.cellStart.push_back(static_cast<std::uint32_t>(graph.spots.size()));
            standings(tree, low.x + column * COLUMN, low.y + row * COLUMN, high.z, low.z, column, row,
                      graph.spots);
        }
    graph.cellStart.push_back(static_cast<std::uint32_t>(graph.spots.size()));

    // Each pair of neighbouring cells once: four of the eight neighbours.
    constexpr std::array<std::array<std::int32_t, 2>, 4> AHEAD = {{{1, -1}, {1, 0}, {1, 1}, {0, 1}}};
    graph.joins.resize(graph.spots.size());
    for (std::uint32_t a = 0; a < graph.spots.size(); ++a) {
        const Spot& from = graph.spots[a];
        for (const auto& [dc, dr] : AHEAD) {
            const WalkGraph::Range next = graph.cell(from.column + dc, from.row + dr);
            for (std::uint32_t b = next.begin; b < next.end; ++b) {
                if (!joins(tree, from, graph.spots[b])) continue;
                graph.joins[a].push_back(b);
                graph.joins[b].push_back(a);
            }
        }
    }
    return graph;
}

std::optional<std::uint32_t> place(const WalkGraph& graph, const Vec3& location) {
    const auto column = static_cast<std::int32_t>(std::lround((location.x - graph.origin.x) / COLUMN));
    const auto row = static_cast<std::int32_t>(std::lround((location.y - graph.origin.y) / COLUMN));
    const auto reach = static_cast<std::int32_t>(PLACE_ACROSS / COLUMN);

    std::optional<std::uint32_t> best;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (std::int32_t c = column - reach; c <= column + reach; ++c)
        for (std::int32_t r = row - reach; r <= row + reach; ++r) {
            const WalkGraph::Range cell = graph.cell(c, r);
            for (std::uint32_t s = cell.begin; s < cell.end; ++s) {
                const Vec3& centre = graph.spots[s].centre;
                if (horizontal(centre, location) > PLACE_ACROSS
                    || std::abs(centre.z - location.z) > PLACE_UP)
                    continue;
                const double distance = length(centre - location);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = s;
                }
            }
        }
    return best;
}

} // namespace uta::paths
