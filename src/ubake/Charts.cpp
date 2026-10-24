// The charts a lit polygon is baked on -- Charts.h.

#include "ubake/Charts.h"

#include "ubake/Geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace uta::ubake::charts {
namespace {

/// PolyFlags a chart must lack to be lit -- UTA-0164 SS 4.2. Not PF_Portal: the
/// renderer draws and lights a portal that is not invisible (UTA-0188), such
/// as a water sheet, and with no chart it lit through the cluster path's
/// shadow-map reads: 7.4 ms of DM-Fool'sBravado's frame at 4K (UTA-0352).
constexpr std::uint32_t UNLIT_FLAGS = PF_INVISIBLE | PF_FAKE_BACKDROP | PF_UNLIT;

Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

/// UTA-0164 SS 4.2: the basis from the normal alone -- the world axis least
/// along it, the lowest on a tie.
bool setBasis(Chart& chart, const std::array<float, 3>& stored) {
    const Vec3 raw{stored[0], stored[1], stored[2]};
    const double size = length(raw);
    if (!(size > 1e-9)) return false;
    chart.n = raw * (1.0 / size);
    const double a[3] = {std::abs(chart.n.x), std::abs(chart.n.y), std::abs(chart.n.z)};
    int axis = 0;
    if (a[1] < a[axis]) axis = 1;
    if (a[2] < a[axis]) axis = 2;
    const Vec3 e{axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0};
    const Vec3 u = cross(chart.n, e);
    chart.u = u * (1.0 / length(u));
    chart.v = cross(chart.n, chart.u);
    return true;
}

/// UTA-0343: whether `q` is on a triangle of the polygon's fan (first, k,
/// k + 1), the surface GEOM draws. An outline is not always convex:
/// MH-()mG-DeathcourseV2's wall runs one corner 0.25 units past the next and
/// back, and an every-edge sign test then put the whole wall outside it. A
/// zero-area triangle holds no point.
bool onFan(const std::vector<Point2>& polygon, Point2 q) {
    for (std::size_t k = 1; k + 1 < polygon.size(); ++k) {
        const Point2 a = polygon[0], b = polygon[k], c = polygon[k + 1];
        const double det = (b.u - a.u) * (c.v - a.v) - (c.u - a.u) * (b.v - a.v);
        if (det == 0) continue;
        const double s = ((q.u - a.u) * (c.v - a.v) - (c.u - a.u) * (q.v - a.v)) / det;
        const double t = ((b.u - a.u) * (q.v - a.v) - (q.u - a.u) * (b.v - a.v)) / det;
        if (std::min({1 - s - t, s, t}) >= 0) return true;
    }
    return false;
}

Point2 clampToPolygon(const std::vector<Point2>& polygon, Point2 q) {
    if (onFan(polygon, q)) return q;
    Point2 best = polygon.front();
    double bestDistance = std::numeric_limits<double>::infinity();
    for (std::size_t k = 0; k < polygon.size(); ++k) {
        const Point2 a = polygon[k], b = polygon[(k + 1) % polygon.size()];
        const double eu = b.u - a.u, ev = b.v - a.v;
        const double span = eu * eu + ev * ev;
        const double t = span > 0 ? std::clamp(((q.u - a.u) * eu + (q.v - a.v) * ev) / span, 0.0, 1.0) : 0.0;
        const Point2 at{a.u + t * eu, a.v + t * ev};
        const double distance = (q.u - at.u) * (q.u - at.u) + (q.v - at.v) * (q.v - at.v);
        if (distance < bestDistance) bestDistance = distance, best = at;
    }
    return best;
}

/// UTA-0284: how far along the chart's normal the surface itself lies at `q`:
/// on the fan triangle (first, k, k + 1) under it, or the nearest one when q is
/// on an edge. A map's polygon is not always flat, nor its stored normal true:
/// MH-()mG-TheBoat-V2mini's hull holds a seven-cornered face 3 units either side
/// of any one plane, and a quad whose stored normal is 0.23 degrees off it, both
/// well past OCCLUSION_LIFT. A sample on the chart's plane fell behind such a
/// face and saw its back; one on the triangle under it cannot.
double surfaceAt(const std::vector<Point2>& polygon, const std::vector<double>& heights, Point2 q) {
    double best = -std::numeric_limits<double>::infinity(), height = heights.front();
    for (std::size_t k = 1; k + 1 < polygon.size(); ++k) {
        const Point2 a = polygon[0], b = polygon[k], c = polygon[k + 1];
        const double det = (b.u - a.u) * (c.v - a.v) - (c.u - a.u) * (b.v - a.v);
        if (det == 0) continue;
        const double s = ((q.u - a.u) * (c.v - a.v) - (c.u - a.u) * (q.v - a.v)) / det;
        const double t = ((b.u - a.u) * (q.v - a.v) - (q.u - a.u) * (b.v - a.v)) / det;
        const double inside = std::min({1 - s - t, s, t}); // 0 or more on the triangle
        if (inside > best) best = inside, height = (1 - s - t) * heights[0] + s * heights[k] + t * heights[k + 1];
    }
    return height;
}

} // namespace

Vec3 positionOf(const ubundle::Geometry& geometry, std::uint32_t index) {
    const auto& p = geometry.vertices[index].position;
    return {p[0], p[1], p[2]};
}

std::vector<Chart> chartsOf(const ubundle::Geometry& geometry) {
    std::vector<Chart> charts;
    std::unordered_map<std::uint32_t, std::size_t> byFirst;
    for (const ubundle::GeometryBatch& batch : geometry.batches) {
        const bool lit = (batch.polyFlags & UNLIT_FLAGS) == 0;
        for (std::uint32_t i = batch.firstIndex; i + 2 < batch.firstIndex + batch.indexCount; i += 3) {
            const std::uint32_t first = geometry.indices[i];
            const std::uint32_t last = std::max(geometry.indices[i + 1], geometry.indices[i + 2]);
            const auto [at, added] = byFirst.try_emplace(first, charts.size());
            if (added) {
                Chart chart;
                chart.first = first;
                chart.last = std::max(first, last);
                chart.lit = lit;
                charts.push_back(chart);
            } else {
                charts[at->second].last = std::max(charts[at->second].last, last);
            }
        }
    }
    std::sort(charts.begin(), charts.end(), [](const Chart& a, const Chart& b) { return a.first < b.first; });
    for (Chart& chart : charts)
        if (chart.lit) chart.lit = setBasis(chart, geometry.vertices[chart.first].normal);
    return charts;
}

void sizeCharts(std::vector<Chart>& charts, const ubundle::Geometry& geometry, double texelSize) {
    for (Chart& chart : charts) {
        if (!chart.lit) continue;
        double minU = std::numeric_limits<double>::infinity(), maxU = -minU, minV = minU, maxV = -minU;
        for (std::uint32_t k = chart.first; k <= chart.last; ++k) {
            const Vec3 p = positionOf(geometry, k);
            const double u = dot(p, chart.u) / texelSize, v = dot(p, chart.v) / texelSize;
            minU = std::min(minU, u), maxU = std::max(maxU, u);
            minV = std::min(minV, v), maxV = std::max(maxV, v);
        }
        chart.loU = static_cast<std::int64_t>(std::floor(minU)) - 1;
        chart.loV = static_cast<std::int64_t>(std::floor(minV)) - 1;
        chart.w = static_cast<std::uint32_t>(static_cast<std::int64_t>(std::floor(maxU)) + 1 - chart.loU + 1);
        chart.h = static_cast<std::uint32_t>(static_cast<std::int64_t>(std::floor(maxV)) + 1 - chart.loV + 1);
    }
}

Outline outlineOf(const Chart& chart, const ubundle::Geometry& geometry) {
    Outline out;
    for (std::uint32_t k = chart.first; k <= chart.last; ++k) {
        const Vec3 p = positionOf(geometry, k);
        out.polygon.push_back({dot(p, chart.u), dot(p, chart.v)});
        out.heights.push_back(dot(p, chart.n));
    }
    return out;
}

Vec3 originAt(const Chart& chart, const Outline& outline, double gridU, double gridV, double texelSize,
              double lift) {
    const Point2 q = clampToPolygon(outline.polygon, {gridU * texelSize, gridV * texelSize});
    return chart.u * q.u + chart.v * q.v + chart.n * (surfaceAt(outline.polygon, outline.heights, q) + lift);
}

} // namespace uta::ubake::charts
