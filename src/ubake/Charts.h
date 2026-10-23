// The charts a lit polygon is baked on -- docs/specs/UTA-0164-ambient-occlusion.md
// SS 4.2 and SS 4.3 steps 1 to 3. UTA-0164's occlusion and UTA-0326's shadow
// mask each bake their own atlas on these, at their own texel size.
//
// INTERNAL to uta_ubake. Moved out of Occlusion.cpp unchanged when UTA-0326
// needed the same charts, so the occlusion atlas did not move a byte.

#pragma once

#include "ubake/CollisionQuery.h"
#include "ubundle/Bundle.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace uta::ubake::charts {

/// One polygon: the vertices first..last, all of one batch -- UTA-0164 SS 4.2.
struct Chart {
    std::uint32_t first = 0, last = 0;
    bool lit = false;
    Vec3 n{}, u{}, v{};            ///< the plane's normal and its texel axes
    std::int64_t loU = 0, loV = 0; ///< the rectangle's first texel, on the world grid
    std::uint32_t w = 0, h = 0;    ///< the rectangle, in texels
    std::uint32_t x = 0, y = 0;    ///< where it is placed in the atlas
};

/// A point in a chart's plane, in its (u, v).
struct Point2 {
    double u = 0, v = 0;
};

/// A chart's polygon in its plane, and each corner's height along its normal.
struct Outline {
    std::vector<Point2> polygon;
    std::vector<double> heights;
};

[[nodiscard]] Vec3 positionOf(const ubundle::Geometry& geometry, std::uint32_t index);

/// The charts of `geometry`, in first-index order. A chart is lit when its
/// batch lacks PF_Invisible, PF_FakeBackdrop, PF_Unlit and PF_Portal and its
/// stored normal has a length.
[[nodiscard]] std::vector<Chart> chartsOf(const ubundle::Geometry& geometry);

/// Each lit chart's rectangle at `texelSize`, grown by a texel a side.
void sizeCharts(std::vector<Chart>& charts, const ubundle::Geometry& geometry, double texelSize);

[[nodiscard]] Outline outlineOf(const Chart& chart, const ubundle::Geometry& geometry);

/// The width an atlas starts at before packing grows it, in texels.
inline constexpr std::uint32_t FIRST_ATLAS_WIDTH = 64;

/// UTA-0164 SS 4.2 and UTA-0326 SS 4.3: shelf packing at `width` of `count`
/// rectangles, already in the order they go in. The k-th is `sizeOf(k)`, a
/// Chart, and is placed by `place(k, x, y)`. The first shelf starts `start`
/// texels across and is at least `start` tall, which leaves the occlusion
/// atlas's white block free. The height used, or 0 when a rectangle is wider
/// than `width`.
template <typename SizeOf, typename Place>
std::uint64_t shelve(std::size_t count, std::uint32_t width, std::uint32_t start, SizeOf&& sizeOf, Place&& place) {
    std::uint64_t x = start, shelfY = 0, shelfH = start;
    for (std::size_t k = 0; k < count; ++k) {
        const Chart& chart = sizeOf(k);
        if (chart.w > width) return 0;
        if (x + chart.w > width) {
            shelfY += shelfH;
            x = 0;
            shelfH = 0;
        }
        place(k, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(shelfY));
        x += chart.w;
        shelfH = std::max<std::uint64_t>(shelfH, chart.h);
    }
    return shelfY + shelfH;
}

/// An atlas's width and the height its packing used.
struct AtlasSize {
    std::uint32_t width = 0;
    std::uint64_t height = 0; ///< 0 where some rectangle is wider than `width`
};

/// The atlas width: from FIRST_ATLAS_WIDTH, doubled until its square holds
/// `area`, then until `pack(width)` -- a shelve -- fits a square, stopping at
/// `limit`. With nothing to pack (`empty`) it stays where the area put it.
template <typename Pack>
AtlasSize widthFor(std::uint64_t area, std::uint32_t limit, bool empty, Pack&& pack) {
    AtlasSize atlas{FIRST_ATLAS_WIDTH, 0};
    while (std::uint64_t{atlas.width} * atlas.width < area && atlas.width < limit) atlas.width *= 2;
    for (;;) {
        atlas.height = pack(atlas.width);
        if (atlas.height != 0 && atlas.height <= atlas.width) break;
        if (empty || atlas.width == limit) break;
        atlas.width *= 2;
    }
    return atlas;
}

/// UTA-0164 SS 4.3 steps 1 to 3: the ray origin for the point (gridU, gridV),
/// in texels on the world grid -- clamped onto the polygon, moved onto the
/// surface under it, then `lift` along the normal.
[[nodiscard]] Vec3 originAt(const Chart& chart, const Outline& outline, double gridU, double gridV,
                            double texelSize, double lift);

} // namespace uta::ubake::charts
