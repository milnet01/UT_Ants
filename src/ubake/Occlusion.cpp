// Baked ambient occlusion -- docs/specs/UTA-0164-ambient-occlusion.md SS 4.2
// and SS 4.3. Occlusion.h says what is baked, and why it is the same at any
// worker count.

#include "ubake/Occlusion.h"

#include "ubake/LightProbes.h"
#include "ubake/SurfaceRays.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>

namespace uta::ubake {
namespace {

/// PolyFlags a chart must lack to be lit -- SS 4.2.
constexpr std::uint32_t PF_INVISIBLE = 0x00000001u;
constexpr std::uint32_t PF_FAKE_BACKDROP = 0x00000080u;
constexpr std::uint32_t PF_UNLIT = 0x00400000u;
constexpr std::uint32_t PF_PORTAL = 0x04000000u;
constexpr std::uint32_t UNLIT_FLAGS = PF_INVISIBLE | PF_FAKE_BACKDROP | PF_UNLIT | PF_PORTAL;

/// Charts to one job. Any size gives the same bytes; this one keeps the number
/// of jobs small.
constexpr std::size_t CHARTS_PER_JOB = 32;

Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 positionOf(const ubundle::Geometry& geometry, std::uint32_t index) {
    const auto& p = geometry.vertices[index].position;
    return {p[0], p[1], p[2]};
}

/// One polygon: the vertices first..last, all of one batch -- SS 4.2.
struct Chart {
    std::uint32_t first = 0, last = 0;
    bool lit = false;
    Vec3 n{}, u{}, v{};          ///< the plane's normal and its texel axes
    double d = 0;                ///< n . p on the plane
    std::int64_t loU = 0, loV = 0; ///< the rectangle's first texel, on the world grid
    std::uint32_t w = 0, h = 0;  ///< the rectangle, in texels
    std::uint32_t x = 0, y = 0;  ///< where it is placed in the atlas
};

/// SS 4.2: the basis from the normal alone -- the world axis least along it,
/// the lowest on a tie.
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

/// The charts of `geometry`, in first-index order.
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
        if (chart.lit) {
            chart.lit = setBasis(chart, geometry.vertices[chart.first].normal);
            if (chart.lit) chart.d = dot(chart.n, positionOf(geometry, chart.first));
        }
    return charts;
}

/// SS 4.2: each lit chart's rectangle at `texelSize`, grown by a texel a side.
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

/// SS 4.2: shelf packing at `width`, tallest first. The height used, or 0 when
/// a chart is wider than `width`.
std::uint64_t shelve(std::vector<Chart*>& order, std::uint32_t width) {
    std::uint64_t x = OCCLUSION_WHITE_BLOCK, shelfY = 0, shelfH = OCCLUSION_WHITE_BLOCK;
    for (Chart* chart : order) {
        if (chart->w > width) return 0;
        if (x + chart->w > width) {
            shelfY += shelfH;
            x = 0;
            shelfH = 0;
        }
        chart->x = static_cast<std::uint32_t>(x);
        chart->y = static_cast<std::uint32_t>(shelfY);
        x += chart->w;
        shelfH = std::max<std::uint64_t>(shelfH, chart->h);
    }
    return shelfY + shelfH;
}

/// The nearest point of a convex polygon to `q`, all in the plane's (u, v).
struct Point2 {
    double u = 0, v = 0;
};

Point2 clampToPolygon(const std::vector<Point2>& polygon, double area, Point2 q) {
    bool inside = true;
    for (std::size_t k = 0; k < polygon.size(); ++k) {
        const Point2 a = polygon[k], b = polygon[(k + 1) % polygon.size()];
        const double side = (b.u - a.u) * (q.v - a.v) - (b.v - a.v) * (q.u - a.u);
        if (side * area < 0) inside = false;
    }
    if (inside) return q;
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

/// SS 4.3 steps 4 and 5: how open the space above `origin` is, 0 to 1.
double opennessAt(const Vec3& origin, const Vec3& n, const SurfaceRays& rays) {
    // Nothing in front within reach: every ray misses, so skip them. Exact --
    // the value is what casting them would give.
    if (!rays.anyInFront(origin, n, OCCLUSION_DISTANCE)) return 1.0;
    double weights = 0, occluded = 0;
    for (const Vec3& w : directions()) {
        const double c = dot(w, n);
        if (c <= 0) continue;
        weights += c;
        const std::optional<SurfaceRays::Hit> hit = rays.first(origin, w, OCCLUSION_DISTANCE);
        if (hit && hit->t <= OCCLUSION_DISTANCE) occluded += c * (1.0 - hit->t / OCCLUSION_DISTANCE);
    }
    return weights > 0 ? std::clamp(1.0 - occluded / weights, 0.0, 1.0) : 1.0;
}

/// SS 4.3: every texel of one lit chart, into its rectangle of the atlas.
void bakeChart(const Chart& chart, const ubundle::Geometry& geometry, const SurfaceRays& rays,
               double texelSize, ubundle::Occlusion& out) {
    std::vector<Point2> polygon;
    double area = 0;
    for (std::uint32_t k = chart.first; k <= chart.last; ++k) {
        const Vec3 p = positionOf(geometry, k);
        polygon.push_back({dot(p, chart.u), dot(p, chart.v)});
    }
    for (std::size_t k = 0; k < polygon.size(); ++k) {
        const Point2 a = polygon[k], b = polygon[(k + 1) % polygon.size()];
        area += a.u * b.v - b.u * a.v;
    }
    for (std::uint32_t j = 0; j < chart.h; ++j)
        for (std::uint32_t i = 0; i < chart.w; ++i) {
            const Point2 q = clampToPolygon(
                polygon, area,
                {(static_cast<double>(chart.loU + i) + 0.5) * texelSize,
                 (static_cast<double>(chart.loV + j) + 0.5) * texelSize});
            const Vec3 origin = chart.u * q.u + chart.v * q.v + chart.n * (chart.d + OCCLUSION_LIFT);
            const double value = opennessAt(origin, chart.n, rays);
            out.texels[static_cast<std::size_t>(chart.y + j) * out.width + chart.x + i] =
                static_cast<std::uint8_t>(std::lround(255.0 * value));
        }
}

} // namespace

Result<ubundle::Occlusion> bakeOcclusion(const ubundle::Geometry& geometry, JobSystem& jobs) {
    return bakeOcclusion(geometry, jobs, OCCLUSION_TEXEL_SIZE);
}

Result<ubundle::Occlusion> bakeOcclusion(const ubundle::Geometry& geometry, JobSystem& jobs, float texelSize) {
    std::vector<Chart> charts = chartsOf(geometry);

    // SS 4.2: pack, widening to the limit and then coarsening the texel.
    std::vector<Chart*> order;
    std::uint32_t width = 0;
    std::uint64_t height = 0;
    for (;;) {
        if (texelSize > OCCLUSION_TEXEL_CEILING)
            return fail(ErrorCode::MalformedData,
                        "ambient occlusion: the level does not fit a " + std::to_string(ubundle::OCCLUSION_ATLAS_LIMIT)
                            + "-texel atlas at " + std::to_string(OCCLUSION_TEXEL_CEILING) + " units a texel");
        sizeCharts(charts, geometry, texelSize);
        order.clear();
        std::uint64_t area = std::uint64_t{OCCLUSION_WHITE_BLOCK} * OCCLUSION_WHITE_BLOCK;
        for (Chart& chart : charts)
            if (chart.lit) {
                order.push_back(&chart);
                area += std::uint64_t{chart.w} * chart.h;
            }
        std::stable_sort(order.begin(), order.end(), [](const Chart* a, const Chart* b) { return a->h > b->h; });
        width = 64;
        while (std::uint64_t{width} * width < area && width < ubundle::OCCLUSION_ATLAS_LIMIT) width *= 2;
        for (;;) {
            height = shelve(order, width);
            if (height != 0 && height <= width) break;
            if (width == ubundle::OCCLUSION_ATLAS_LIMIT) break;
            width *= 2;
        }
        if (height != 0 && height <= ubundle::OCCLUSION_ATLAS_LIMIT) break;
        texelSize *= 2;
    }

    ubundle::Occlusion out;
    out.texelSize = texelSize;
    out.width = width;
    out.height = static_cast<std::uint32_t>((height + 3) / 4 * 4);
    out.texels.assign(std::size_t{out.width} * out.height, 255);

    // SS 4.2: every vertex's uv -- its place in its chart's rectangle, or the
    // white block's centre.
    const std::array<float, 2> white{static_cast<float>(OCCLUSION_WHITE_BLOCK / 2.0 / out.width),
                                     static_cast<float>(OCCLUSION_WHITE_BLOCK / 2.0 / out.height)};
    out.uv.assign(geometry.vertices.size(), white);
    for (const Chart& chart : charts) {
        if (!chart.lit) continue;
        for (std::uint32_t k = chart.first; k <= chart.last; ++k) {
            const Vec3 p = positionOf(geometry, k);
            const double u = dot(p, chart.u) / texelSize - static_cast<double>(chart.loU) + chart.x;
            const double v = dot(p, chart.v) / texelSize - static_cast<double>(chart.loV) + chart.y;
            out.uv[k] = {static_cast<float>(u / out.width), static_cast<float>(v / out.height)};
        }
    }

    // SS 4.3: each chart's texels on their own, into their own rectangle.
    const SurfaceRays rays(geometry);
    const std::size_t batches = (order.size() + CHARTS_PER_JOB - 1) / CHARTS_PER_JOB;
    const double size = texelSize;
    const std::size_t threw = jobs.parallelFor(batches, [&](std::size_t job) {
        const std::size_t end = std::min(order.size(), (job + 1) * CHARTS_PER_JOB);
        for (std::size_t c = job * CHARTS_PER_JOB; c < end; ++c) bakeChart(*order[c], geometry, rays, size, out);
    });
    // A partly baked atlas is a wrong bundle presented as a good one.
    if (threw != 0)
        return fail(ErrorCode::Unknown,
                    "ambient occlusion: " + std::to_string(threw) + " jobs threw while baking");
    return out;
}

} // namespace uta::ubake
