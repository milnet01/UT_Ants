// docs/specs/UTA-0055-swaying-cloth-and-water.md SS 4.2 and SS 4.3.

#include "ubake/Sway.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <tuple>
#include <utility>

namespace uta::ubake {

namespace {

using ubundle::SwayKind;
using Polygon = std::vector<SwayPoint>;

/// A plane this close to a polygon's extreme cuts nothing, and a neighbour's
/// corner this close to an edge's end is that end.
constexpr double NEAR = 1.0 / 64.0;
/// A point this close to an edge's line lies on it.
constexpr double ON_LINE = 1.0 / 32.0;
/// Edges are matched with the edges sharing a cube of this side.
constexpr double CELL = 128.0;

SwayPoint minus(const SwayPoint& a, const SwayPoint& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
SwayPoint along(const SwayPoint& a, const SwayPoint& direction, double t) {
    return {a[0] + direction[0] * t, a[1] + direction[1] * t, a[2] + direction[2] * t};
}
double dot(const SwayPoint& a, const SwayPoint& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
SwayPoint cross(const SwayPoint& a, const SwayPoint& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double lengthOf(const SwayPoint& a) { return std::sqrt(dot(a, a)); }

/// Twice the polygon's area.
double doubledArea(const Polygon& polygon) {
    SwayPoint sum{};
    for (std::size_t k = 1; k + 1 < polygon.size(); ++k) {
        const SwayPoint c = cross(minus(polygon[k], polygon[0]), minus(polygon[k + 1], polygon[0]));
        sum = {sum[0] + c[0], sum[1] + c[1], sum[2] + c[2]};
    }
    return lengthOf(sum);
}

/// Where the plane `axis` = `c` crosses the edge p..q. Computed from the ends
/// in lexicographic order, so a neighbour holding the edge the other way
/// round gets the same bits.
SwayPoint crossing(SwayPoint p, SwayPoint q, std::size_t axis, double c) {
    if (q < p) std::swap(p, q);
    const double t = (c - p[axis]) / (q[axis] - p[axis]);
    SwayPoint x = along(p, minus(q, p), t);
    x[axis] = c;
    return x;
}

/// `polygon` cut by the plane `axis` = `c` into `out`: two pieces where the
/// plane passes through it, else the polygon itself.
void split(const Polygon& polygon, std::size_t axis, double c, std::vector<Polygon>& out) {
    const std::size_t n = polygon.size();
    std::vector<int> side(n);
    bool below = false;
    bool above = false;
    for (std::size_t k = 0; k < n; ++k) {
        const double s = polygon[k][axis] - c;
        side[k] = s < -NEAR ? -1 : (s > NEAR ? 1 : 0);
        below = below || side[k] < 0;
        above = above || side[k] > 0;
    }
    if (!below || !above) {
        out.push_back(polygon);
        return;
    }
    Polygon low;
    Polygon high;
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t next = (k + 1) % n;
        if (side[k] <= 0) low.push_back(polygon[k]);
        if (side[k] >= 0) high.push_back(polygon[k]);
        if (side[k] * side[next] < 0) {
            const SwayPoint x = crossing(polygon[k], polygon[next], axis, c);
            low.push_back(x);
            high.push_back(x);
        }
    }
    for (Polygon* piece : {&low, &high})
        if (piece->size() >= 3 && doubledArea(*piece) > 1e-9) out.push_back(std::move(*piece));
}

/// SS 4.2: `polygon` cut by every world plane `k * spacing` on each axis.
std::vector<Polygon> slice(const Polygon& polygon, double spacing) {
    std::vector<Polygon> pieces{polygon};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        double lowest = polygon[0][axis];
        double highest = lowest;
        for (const SwayPoint& point : polygon) {
            lowest = std::min(lowest, point[axis]);
            highest = std::max(highest, point[axis]);
        }
        const auto first = static_cast<std::int64_t>(std::ceil((lowest + NEAR) / spacing));
        const auto last = static_cast<std::int64_t>(std::floor((highest - NEAR) / spacing));
        for (std::int64_t k = first; k <= last; ++k) {
            std::vector<Polygon> next;
            next.reserve(pieces.size() + 1);
            for (const Polygon& piece : pieces) split(piece, axis, static_cast<double>(k) * spacing, next);
            pieces = std::move(next);
        }
    }
    return pieces;
}

/// An upper bound on the points slicing `polygon` makes: the cells of its two
/// widest axes, five points a cell. Kept so a vast plane is never cut only to
/// learn it is over the budget.
double estimate(const Polygon& polygon, double spacing) {
    std::array<double, 3> cells{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        double lowest = polygon[0][axis];
        double highest = lowest;
        for (const SwayPoint& point : polygon) {
            lowest = std::min(lowest, point[axis]);
            highest = std::max(highest, point[axis]);
        }
        cells[axis] = std::floor((highest - lowest) / spacing) + 1.0;
    }
    std::ranges::sort(cells);
    return 5.0 * cells[1] * cells[2] + static_cast<double>(polygon.size());
}

struct Edge {
    std::size_t polygon = 0;
    std::size_t index = 0; ///< from point `index` to the next
    SwayPoint start{};
    SwayPoint direction{}; ///< unit
    double length = 0;
};

struct Patch {
    SwayKind kind = SwayKind::None;
    bool hanging = false;
    double top = 0;
    std::vector<std::pair<SwayPoint, SwayPoint>> outline; ///< the stretches no neighbour covers
};

/// What slicing needs, worked out once: each polygon with its neighbours'
/// corners added, and the patch it belongs to.
struct Prepared {
    std::vector<Polygon> polygons;
    std::vector<std::size_t> patchOf;
    std::vector<Patch> patches;
};

std::size_t rootOf(std::vector<std::size_t>& parent, std::size_t x) {
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

Prepared prepare(std::span<const SwayPolygon> polygons) {
    Prepared out;
    out.polygons.resize(polygons.size());
    out.patchOf.assign(polygons.size(), 0);

    // Every moving polygon's edges, by the group a patch may not leave.
    std::map<std::pair<SwayKind, std::string>, std::vector<Edge>> groups;
    for (std::size_t p = 0; p < polygons.size(); ++p) {
        const SwayPolygon& polygon = polygons[p];
        if (polygon.kind == SwayKind::None) continue;
        std::vector<Edge>& edges = groups[{polygon.kind, polygon.group}];
        for (std::size_t k = 0; k < polygon.points.size(); ++k) {
            const SwayPoint& a = polygon.points[k];
            const SwayPoint& b = polygon.points[(k + 1) % polygon.points.size()];
            const SwayPoint span = minus(b, a);
            const double length = lengthOf(span);
            if (length <= 0) continue;
            edges.push_back(Edge{p, k, a, {span[0] / length, span[1] / length, span[2] / length}, length});
        }
    }

    std::vector<std::size_t> parent(polygons.size());
    std::iota(parent.begin(), parent.end(), std::size_t{0});
    // Per edge, keyed by (polygon, index): the stretches a neighbour covers,
    // and the neighbours' corners inside it, by distance from its start.
    std::map<std::pair<std::size_t, std::size_t>, std::vector<std::pair<double, double>>> covered;
    std::map<std::pair<std::size_t, std::size_t>, std::vector<std::pair<double, SwayPoint>>> inside;

    for (auto& [key, edges] : groups) {
        // Edges sharing a cube are the only candidates for overlapping.
        std::map<std::array<std::int64_t, 3>, std::vector<std::size_t>> cells;
        for (std::size_t e = 0; e < edges.size(); ++e) {
            const Edge& edge = edges[e];
            std::set<std::array<std::int64_t, 3>> seen;
            for (double t = 0;; t = std::min(t + CELL / 4.0, edge.length)) {
                const SwayPoint point = along(edge.start, edge.direction, t);
                const std::array<std::int64_t, 3> cell{static_cast<std::int64_t>(std::floor(point[0] / CELL)),
                                                       static_cast<std::int64_t>(std::floor(point[1] / CELL)),
                                                       static_cast<std::int64_t>(std::floor(point[2] / CELL))};
                if (seen.insert(cell).second) cells[cell].push_back(e);
                if (t >= edge.length) break;
            }
        }
        std::set<std::pair<std::size_t, std::size_t>> tried;
        for (const auto& [cell, members] : cells)
            for (std::size_t i = 0; i < members.size(); ++i)
                for (std::size_t j = i + 1; j < members.size(); ++j) {
                    const Edge& a = edges[members[i]];
                    const Edge& b = edges[members[j]];
                    if (a.polygon == b.polygon || !tried.insert(std::minmax(members[i], members[j])).second)
                        continue;
                    if (lengthOf(cross(a.direction, b.direction)) > 1e-4) continue;
                    const SwayPoint bEnd = along(b.start, b.direction, b.length);
                    if (lengthOf(cross(minus(b.start, a.start), a.direction)) > ON_LINE
                        || lengthOf(cross(minus(bEnd, a.start), a.direction)) > ON_LINE)
                        continue;
                    const double t0 = dot(minus(b.start, a.start), a.direction);
                    const double t1 = dot(minus(bEnd, a.start), a.direction);
                    const double from = std::max(0.0, std::min(t0, t1));
                    const double to = std::min(a.length, std::max(t0, t1));
                    if (to - from <= NEAR) continue;
                    // They overlap: one patch, each covered there, and each
                    // carries the other's corners that fall inside it.
                    parent[rootOf(parent, a.polygon)] = rootOf(parent, b.polygon);
                    for (const auto& [self, other] : {std::pair{&a, &b}, std::pair{&b, &a}}) {
                        const SwayPoint otherEnd = along(other->start, other->direction, other->length);
                        const double s0 = dot(minus(other->start, self->start), self->direction);
                        const double s1 = dot(minus(otherEnd, self->start), self->direction);
                        covered[{self->polygon, self->index}].emplace_back(
                            std::max(0.0, std::min(s0, s1)), std::min(self->length, std::max(s0, s1)));
                        // The other's corners exactly as it holds them, so both
                        // polygons carry the same bits.
                        const SwayPoint& otherFirst = polygons[other->polygon].points[other->index];
                        const SwayPoint& otherSecond =
                            polygons[other->polygon].points[(other->index + 1) % polygons[other->polygon].points.size()];
                        for (const auto& [t, corner] : {std::pair{s0, otherFirst}, std::pair{s1, otherSecond}})
                            if (t > NEAR && t < self->length - NEAR)
                                inside[{self->polygon, self->index}].emplace_back(t, corner);
                    }
                }
    }

    // Each polygon with the corners inside its edges added, in order.
    for (std::size_t p = 0; p < polygons.size(); ++p) {
        const Polygon& points = polygons[p].points;
        if (polygons[p].kind == SwayKind::None) continue;
        Polygon& with = out.polygons[p];
        for (std::size_t k = 0; k < points.size(); ++k) {
            with.push_back(points[k]);
            const auto found = inside.find({p, k});
            if (found == inside.end()) continue;
            std::vector<std::pair<double, SwayPoint>> corners = found->second;
            std::ranges::sort(corners, {}, &std::pair<double, SwayPoint>::first);
            double last = 0;
            for (const auto& [t, corner] : corners) {
                if (t - last <= NEAR) continue;
                with.push_back(corner);
                last = t;
            }
        }
    }

    // The patches: the top, whether cloth hangs, and the outline.
    std::map<std::size_t, std::size_t> patchByRoot;
    std::vector<double> weightedUp;
    std::vector<double> area;
    for (std::size_t p = 0; p < polygons.size(); ++p) {
        if (polygons[p].kind == SwayKind::None) continue;
        const std::size_t root = rootOf(parent, p);
        auto [found, fresh] = patchByRoot.try_emplace(root, out.patches.size());
        if (fresh) {
            out.patches.push_back(Patch{polygons[p].kind, false, polygons[p].points[0][2], {}});
            weightedUp.push_back(0);
            area.push_back(0);
        }
        const std::size_t patch = found->second;
        out.patchOf[p] = patch;
        for (const SwayPoint& point : polygons[p].points) out.patches[patch].top = std::max(out.patches[patch].top, point[2]);
        const double a = doubledArea(polygons[p].points);
        weightedUp[patch] += a * std::abs(polygons[p].normal[2]);
        area[patch] += a;
    }
    for (std::size_t patch = 0; patch < out.patches.size(); ++patch)
        out.patches[patch].hanging = out.patches[patch].kind == SwayKind::Cloth
                                     && weightedUp[patch] < FACING_UP * area[patch];
    for (const auto& [key, edges] : groups)
        for (const Edge& edge : edges) {
            std::vector<std::pair<double, double>> stretches;
            if (const auto found = covered.find({edge.polygon, edge.index}); found != covered.end())
                stretches = found->second;
            std::ranges::sort(stretches);
            double reached = 0;
            std::vector<std::pair<SwayPoint, SwayPoint>>& outline = out.patches[out.patchOf[edge.polygon]].outline;
            for (const auto& [from, to] : stretches) {
                if (from - reached > NEAR)
                    outline.emplace_back(along(edge.start, edge.direction, reached),
                                         along(edge.start, edge.direction, from));
                reached = std::max(reached, to);
            }
            if (edge.length - reached > NEAR)
                outline.emplace_back(along(edge.start, edge.direction, reached),
                                     along(edge.start, edge.direction, edge.length));
        }
    return out;
}

double distanceTo(const SwayPoint& point, const SwayPoint& a, const SwayPoint& b) {
    const SwayPoint ab = minus(b, a);
    const double lengthSquared = dot(ab, ab);
    const double t = lengthSquared > 0 ? std::clamp(dot(minus(point, a), ab) / lengthSquared, 0.0, 1.0) : 0.0;
    return lengthOf(minus(point, along(a, ab, t)));
}

/// SS 4.3: the point's weight in its patch.
std::uint8_t weightOf(const Patch& patch, const SwayPoint& point) {
    double free = 1.0;
    if (patch.hanging) {
        free = (patch.top - point[2]) / CLOTH_FULL_DROP;
    } else if (!patch.outline.empty()) {
        double nearest = EDGE_FADE;
        for (const auto& [a, b] : patch.outline) nearest = std::min(nearest, distanceTo(point, a, b));
        free = nearest / EDGE_FADE;
    }
    return static_cast<std::uint8_t>(std::lround(std::clamp(free, 0.0, 1.0) * 255.0));
}

/// Every moving polygon cut and weighted, water at `waterSlice`; a kind
/// switched off stays whole and still. Stops early, uncut, once the estimate
/// passes the budget many times over.
SwaySlicing sliceAll(std::span<const SwayPolygon> polygons, const Prepared& prepared, double waterSlice,
                     double clothSlice, bool water, bool cloth) {
    SwaySlicing out;
    out.waterSlice = waterSlice;
    out.clothSlice = clothSlice;
    out.waterStill = !water;
    out.clothStill = !cloth;
    out.pieces.resize(polygons.size());
    const auto spacingOf = [&](SwayKind kind) { return kind == SwayKind::Water ? waterSlice : clothSlice; };
    const auto moves = [&](SwayKind kind) {
        return (kind == SwayKind::Water && water) || (kind == SwayKind::Cloth && cloth);
    };
    double estimated = 0;
    for (std::size_t p = 0; p < polygons.size(); ++p)
        if (moves(polygons[p].kind)) estimated += estimate(prepared.polygons[p], spacingOf(polygons[p].kind));
    if (estimated > 8.0 * static_cast<double>(SWAY_VERTEX_BUDGET)) {
        out.vertices = static_cast<std::size_t>(estimated);
        for (std::size_t p = 0; p < polygons.size(); ++p)
            if (moves(polygons[p].kind) && polygons[p].kind == SwayKind::Cloth)
                out.clothVertices += static_cast<std::size_t>(estimate(prepared.polygons[p], clothSlice));
        return out;
    }
    for (std::size_t p = 0; p < polygons.size(); ++p) {
        if (!moves(polygons[p].kind)) continue;
        const Patch& patch = prepared.patches[prepared.patchOf[p]];
        const double spacing = spacingOf(polygons[p].kind);
        for (Polygon& piece : slice(prepared.polygons[p], spacing)) {
            SwayPiece made;
            made.cell = static_cast<std::uint8_t>(spacing / SWAY_CELL_UNITS);
            made.weights.reserve(piece.size());
            for (const SwayPoint& point : piece) made.weights.push_back(weightOf(patch, point));
            out.vertices += piece.size();
            if (polygons[p].kind == SwayKind::Cloth) out.clothVertices += piece.size();
            made.points = std::move(piece);
            out.pieces[p].push_back(std::move(made));
        }
    }
    return out;
}

} // namespace

SwaySlicing sliceSway(std::span<const SwayPolygon> polygons) {
    const Prepared prepared = prepare(polygons);
    double waterSlice = WATER_SLICE;
    double clothSlice = CLOTH_SLICE;
    bool water = true;
    bool cloth = true;
    for (;;) {
        SwaySlicing attempt = sliceAll(polygons, prepared, waterSlice, clothSlice, water, cloth);
        if (attempt.vertices <= SWAY_VERTEX_BUDGET) return attempt;
        // Coarsen the kind holding more of the vertices while it can be;
        // then the other; then leave water still, then cloth.
        const bool clothHeavier = attempt.clothVertices * 2 >= attempt.vertices;
        const bool waterCoarser = water && waterSlice * 2.0 <= WATER_SLICE_MAX;
        const bool clothCoarser = cloth && clothSlice * 2.0 <= CLOTH_SLICE_MAX;
        if (clothCoarser && (clothHeavier || !waterCoarser)) {
            clothSlice *= 2.0;
        } else if (waterCoarser) {
            waterSlice *= 2.0;
        } else if (water) {
            water = false;
        } else if (cloth) {
            cloth = false;
        } else {
            return attempt; // nothing moves; unreachable while the count is zero
        }
    }
}

} // namespace uta::ubake
