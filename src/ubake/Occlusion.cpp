// Baked ambient occlusion -- docs/specs/UTA-0164-ambient-occlusion.md SS 4.2
// and SS 4.3. Occlusion.h says what is baked, and why it is the same at any
// worker count.

#include "ubake/Occlusion.h"

#include "ubake/Charts.h"
#include "ubake/LightProbes.h"
#include "ubake/SurfaceRays.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace uta::ubake {
namespace {

using charts::Chart;

/// Charts to one job. Any size gives the same bytes; this one keeps the number
/// of jobs small.
constexpr std::size_t CHARTS_PER_JOB = 32;

/// UTA-0259: past this many gathered occluders a ray searches the tree, as
/// before. Any value gives the same bytes; this one is where the two cost
/// about the same.
constexpr std::size_t GATHERED_LIMIT = 64;

/// SS 4.2: shelf packing at `width`, tallest first. The height used, or 0 when
/// a chart is wider than `width`.
std::uint64_t shelve(const std::vector<Chart*>& order, std::uint32_t width) {
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

/// SS 4.3 steps 4 and 5: how open the space above `origin` is, 0 to 1.
double opennessAt(const Vec3& origin, const Vec3& n, const SurfaceRays& rays) {
    // Nothing in front within reach: every ray misses, so skip them. Exact --
    // the value is what casting them would give.
    if (!rays.anyInFront(origin, n, OCCLUSION_DISTANCE)) return 1.0;
    // UTA-0259: every ray below leaves this one point and stops at the same
    // reach, so the occluders any of them can meet are gathered once. Where
    // they are few, each ray is met against them alone; the nearest t is the
    // one the whole tree gives, so no texel changes.
    thread_local std::vector<std::uint32_t> near;
    near.clear();
    rays.gatherInFront(origin, n, OCCLUSION_DISTANCE, near);
    const bool few = near.size() <= GATHERED_LIMIT;
    double weights = 0, occluded = 0;
    for (const Vec3& w : directions()) {
        const double c = dot(w, n);
        if (c <= 0) continue;
        weights += c;
        std::optional<double> t;
        if (few) {
            t = rays.nearestAmong(near, origin, w, OCCLUSION_DISTANCE);
        } else if (const std::optional<SurfaceRays::Hit> hit = rays.first(origin, w, OCCLUSION_DISTANCE)) {
            t = hit->t;
        }
        if (t && *t <= OCCLUSION_DISTANCE) occluded += c * (1.0 - *t / OCCLUSION_DISTANCE);
    }
    return weights > 0 ? std::clamp(1.0 - occluded / weights, 0.0, 1.0) : 1.0;
}

/// SS 4.3: every texel of one lit chart, into its rectangle of the atlas.
void bakeChart(const Chart& chart, const ubundle::Geometry& geometry, const SurfaceRays& rays,
               double texelSize, ubundle::Occlusion& out) {
    const charts::Outline outline = charts::outlineOf(chart, geometry);
    for (std::uint32_t j = 0; j < chart.h; ++j)
        for (std::uint32_t i = 0; i < chart.w; ++i) {
            const Vec3 origin = charts::originAt(chart, outline, static_cast<double>(chart.loU + i) + 0.5,
                                                 static_cast<double>(chart.loV + j) + 0.5, texelSize, OCCLUSION_LIFT);
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
    // Coarsening doubles the texel until the level fits, which never ends on
    // a size that doubling cannot grow (UTA-0218).
    if (!(texelSize > 0.0f) || !std::isfinite(texelSize))
        return fail(ErrorCode::InvalidArgument,
                    "ambient occlusion: a texel size must be positive and finite, not " + std::to_string(texelSize));
    std::vector<Chart> all = charts::chartsOf(geometry);

    // SS 4.2: pack, widening to the limit and then coarsening the texel.
    std::vector<Chart*> order;
    std::uint32_t width = 0;
    std::uint64_t height = 0;
    for (;;) {
        if (texelSize > OCCLUSION_TEXEL_CEILING)
            return fail(ErrorCode::MalformedData,
                        "ambient occlusion: the level does not fit a " + std::to_string(ubundle::OCCLUSION_ATLAS_LIMIT)
                            + "-texel atlas at " + std::to_string(OCCLUSION_TEXEL_CEILING) + " units a texel");
        charts::sizeCharts(all, geometry, texelSize);
        order.clear();
        std::uint64_t area = std::uint64_t{OCCLUSION_WHITE_BLOCK} * OCCLUSION_WHITE_BLOCK;
        for (Chart& chart : all)
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
    for (const Chart& chart : all) {
        if (!chart.lit) continue;
        for (std::uint32_t k = chart.first; k <= chart.last; ++k) {
            const Vec3 p = charts::positionOf(geometry, k);
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
