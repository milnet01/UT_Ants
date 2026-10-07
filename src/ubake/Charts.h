// The charts a lit polygon is baked on -- docs/specs/UTA-0164-ambient-occlusion.md
// SS 4.2 and SS 4.3 steps 1 to 3. UTA-0164's occlusion and UTA-0326's shadow
// mask each bake their own atlas on these, at their own texel size.
//
// INTERNAL to uta_ubake. Moved out of Occlusion.cpp unchanged when UTA-0326
// needed the same charts, so the occlusion atlas did not move a byte.

#pragma once

#include "ubake/CollisionQuery.h"
#include "ubundle/Bundle.h"

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

/// A chart's polygon in its plane, each corner's height along its normal, and
/// twice its signed area.
struct Outline {
    std::vector<Point2> polygon;
    std::vector<double> heights;
    double area = 0;
};

[[nodiscard]] Vec3 positionOf(const ubundle::Geometry& geometry, std::uint32_t index);

/// The charts of `geometry`, in first-index order. A chart is lit when its
/// batch lacks PF_Invisible, PF_FakeBackdrop, PF_Unlit and PF_Portal and its
/// stored normal has a length.
[[nodiscard]] std::vector<Chart> chartsOf(const ubundle::Geometry& geometry);

/// Each lit chart's rectangle at `texelSize`, grown by a texel a side.
void sizeCharts(std::vector<Chart>& charts, const ubundle::Geometry& geometry, double texelSize);

[[nodiscard]] Outline outlineOf(const Chart& chart, const ubundle::Geometry& geometry);

/// UTA-0164 SS 4.3 steps 1 to 3: the ray origin for the point (gridU, gridV),
/// in texels on the world grid -- clamped onto the polygon, moved onto the
/// surface under it, then `lift` along the normal.
[[nodiscard]] Vec3 originAt(const Chart& chart, const Outline& outline, double gridU, double gridV,
                            double texelSize, double lift);

} // namespace uta::ubake::charts
