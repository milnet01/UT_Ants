// UTA-0055 SS 4.2 and SS 4.3: sliceSway cuts moving polygons by world planes
// with no crack between neighbours, weights each corner from its patch, and
// keeps the map inside its budget.
//
// docs/specs/UTA-0055-swaying-cloth-and-water.md INV-2, INV-4 and INV-6.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "ubake/Sway.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

using uta::ubake::sliceSway;
using uta::ubake::SwayPiece;
using uta::ubake::SwayPoint;
using uta::ubake::SwayPolygon;
using uta::ubake::SwaySlicing;
using uta::ubundle::SwayKind;

namespace {

/// A rectangle in the plane z = `z`, from (x0, y0) to (x1, y1), wound so its
/// fan points along +z.
SwayPolygon floorOf(double x0, double y0, double x1, double y1, double z = 0,
                    SwayKind kind = SwayKind::Water) {
    return SwayPolygon{kind, "pool", {{x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}}, {0, 0, 1}};
}

/// A hanging rectangle in the plane y = 0, from x0 to x1 and z0 down to z1.
SwayPolygon hangingOf(double x0, double x1, double z0, double z1) {
    return SwayPolygon{SwayKind::Cloth, "banner", {{x0, 0, z0}, {x0, 0, z1}, {x1, 0, z1}, {x1, 0, z0}}, {0, -1, 0}};
}

double distanceTo(const SwayPoint& p, const SwayPoint& a, const SwayPoint& b) {
    SwayPoint ab{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const double lengthSquared = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
    double t = ((p[0] - a[0]) * ab[0] + (p[1] - a[1]) * ab[1] + (p[2] - a[2]) * ab[2]) / lengthSquared;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    const SwayPoint q{a[0] + ab[0] * t - p[0], a[1] + ab[1] * t - p[1], a[2] + ab[2] * t - p[2]};
    return std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
}

/// INV-2's crack: a corner of one piece lying on another piece's edge without
/// being bitwise one of its ends. Returns how many.
std::size_t tJunctions(const SwaySlicing& slicing) {
    std::vector<SwayPoint> corners;
    std::vector<std::pair<SwayPoint, SwayPoint>> edges;
    for (const auto& pieces : slicing.pieces)
        for (const SwayPiece& piece : pieces)
            for (std::size_t k = 0; k < piece.points.size(); ++k) {
                corners.push_back(piece.points[k]);
                edges.emplace_back(piece.points[k], piece.points[(k + 1) % piece.points.size()]);
            }
    std::size_t found = 0;
    for (const SwayPoint& corner : corners)
        for (const auto& [a, b] : edges)
            if (corner != a && corner != b && distanceTo(corner, a, b) < 1e-3) ++found;
    return found;
}

/// The weight sliceSway gave the corner at `at`; -1 where no corner is there.
int weightAt(const SwaySlicing& slicing, const SwayPoint& at) {
    for (const auto& pieces : slicing.pieces)
        for (const SwayPiece& piece : pieces)
            for (std::size_t k = 0; k < piece.points.size(); ++k)
                if (piece.points[k] == at) return piece.weights[k];
    return -1;
}

} // namespace

TEST_CASE("UTA-0055 INV-2: neighbours sliced in a tilted plane share every split point", "[ubake][sway]") {
    // Two cloth quads sharing the edge from O + U to O + U + V, which each
    // walks the other way. The edge runs across all three axes at uneven
    // coordinates, so a crossing computed from its ends in the polygon's own
    // order lands on different bits.
    const SwayPoint o{0.37, 7.13, 3.71};
    const SwayPoint u{40.29, 0.31, -0.17};
    const SwayPoint v{1.63, 100.57, 199.61};
    const auto at = [&](double s, double t) {
        return SwayPoint{o[0] + s * u[0] + t * v[0], o[1] + s * u[1] + t * v[1], o[2] + s * u[2] + t * v[2]};
    };
    SwayPoint normal{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
    const double length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    for (double& part : normal) part /= length;
    const std::vector<SwayPolygon> polygons{
        SwayPolygon{SwayKind::Cloth, "sail", {at(0, 0), at(1, 0), at(1, 1), at(0, 1)}, normal},
        SwayPolygon{SwayKind::Cloth, "sail", {at(1, 0), at(2, 0), at(2, 1), at(1, 1)}, normal},
    };
    const SwaySlicing slicing = sliceSway(polygons);
    REQUIRE(slicing.pieces[0].size() > 4);
    REQUIRE(slicing.pieces[1].size() > 4);
    CHECK(tJunctions(slicing) == 0);
}

TEST_CASE("UTA-0055 INV-2: a neighbour's corner inside an edge is added to it", "[ubake][sway]") {
    // A wide floor below two narrow ones that meet at x = 50, off every
    // slicing plane. Unrepaired, the wide floor's top edge passes through
    // (50, 64) without a corner there.
    const std::vector<SwayPolygon> polygons{floorOf(0, 0, 128, 64), floorOf(0, 64, 50, 128),
                                            floorOf(50, 64, 128, 128)};
    const SwaySlicing slicing = sliceSway(polygons);
    CHECK(tJunctions(slicing) == 0);
    // And the seam is inside the pool, not its shore: 50 from the nearest
    // outline edge, x = 0.
    CHECK(weightAt(slicing, {50, 64, 0}) == static_cast<int>(std::lround(50.0 / 64.0 * 255.0)));
}

TEST_CASE("UTA-0055 INV-4: water is pinned at its outline and free past the fade", "[ubake][sway]") {
    const std::vector<SwayPolygon> polygons{floorOf(0, 0, 256, 256)};
    const SwaySlicing slicing = sliceSway(polygons);
    for (const SwayPiece& piece : slicing.pieces[0])
        for (std::size_t k = 0; k < piece.points.size(); ++k) {
            const SwayPoint& p = piece.points[k];
            const bool onOutline = p[0] == 0 || p[0] == 256 || p[1] == 0 || p[1] == 256;
            if (onOutline) CHECK(piece.weights[k] == 0);
        }
    CHECK(weightAt(slicing, {128, 128, 0}) == 255);
    CHECK(weightAt(slicing, {32, 128, 0}) == 128); // half the fade
}

TEST_CASE("UTA-0055 INV-4: hanging cloth is pinned at its patch's top", "[ubake][sway]") {
    // Two stacked quads: the lower one's top is 128 below the patch's.
    const std::vector<SwayPolygon> polygons{hangingOf(0, 64, 0, -128), hangingOf(0, 64, -128, -512)};
    const SwaySlicing slicing = sliceSway(polygons);
    CHECK(weightAt(slicing, {0, 0, 0}) == 0);
    CHECK(weightAt(slicing, {64, 0, 0}) == 0);
    CHECK(weightAt(slicing, {0, 0, -128}) == 128); // per patch: per polygon it would be 0
    CHECK(weightAt(slicing, {0, 0, -256}) == 255);
    CHECK(weightAt(slicing, {64, 0, -512}) == 255);
}

TEST_CASE("UTA-0055 SS 4.2: a polygon that sways none gets no pieces", "[ubake][sway]") {
    std::vector<SwayPolygon> polygons{floorOf(0, 0, 64, 64)};
    polygons[0].kind = SwayKind::None;
    const SwaySlicing slicing = sliceSway(polygons);
    CHECK(slicing.pieces[0].empty());
    CHECK(slicing.vertices == 0);
}

TEST_CASE("UTA-0055 INV-6: water too big for the budget is cut coarser", "[ubake][sway]") {
    // At 32 units, 375 cells a side and four corners a cell pass the budget.
    const std::vector<SwayPolygon> polygons{floorOf(0, 0, 12000, 12000)};
    const SwaySlicing slicing = sliceSway(polygons);
    CHECK(slicing.waterSlice == 64.0);
    CHECK_FALSE(slicing.waterStill);
    CHECK(slicing.vertices <= uta::ubake::SWAY_VERTEX_BUDGET);
    CHECK_FALSE(slicing.pieces[0].empty());
}

TEST_CASE("UTA-0055 INV-6: water too big at the coarsest spacing stays still", "[ubake][sway]") {
    std::vector<SwayPolygon> polygons{floorOf(0, 0, 1000000, 1000000), hangingOf(0, 64, 0, -64)};
    const SwaySlicing slicing = sliceSway(polygons);
    CHECK(slicing.waterStill);
    CHECK_FALSE(slicing.clothStill);
    CHECK(slicing.pieces[0].empty());
    CHECK_FALSE(slicing.pieces[1].empty());
    CHECK(slicing.vertices <= uta::ubake::SWAY_VERTEX_BUDGET);
}
