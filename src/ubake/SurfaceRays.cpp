// Rays against the surfaces the renderer draws --
// docs/specs/UTA-0112-baked-light-probes.md SS 4.7. SurfaceRays.h says why the
// answer does not depend on the tree.

#include "ubake/SurfaceRays.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace uta::ubake {
namespace {

/// PolyFlags that let light through -- the 432 headers' Engine/Inc/UnObj.h.
constexpr std::uint32_t PF_TRANSLUCENT = 0x04;
constexpr std::uint32_t PF_MODULATED = 0x40;

constexpr std::uint32_t LEAF_SIZE = 4;

/// Each node's box is widened by this much, in UT units, so a hit that the box
/// test's rounding would place a hair outside its box is still tested. It can
/// only add triangles to a test, never remove one.
constexpr double PAD = 0.01;

constexpr double INFINITE = std::numeric_limits<double>::infinity();

Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 positionOf(const ubundle::Geometry& geometry, std::uint32_t index) {
    const auto& p = geometry.vertices[index].position;
    return {p[0], p[1], p[2]};
}

/// Moller and Trumbore, from either side: the t at which o + t d meets the
/// triangle with corner `a` and edges `ab`, `ac`, or none. Edges are inclusive,
/// so a ray through a shared edge meets both triangles, and the caller's tie
/// rule decides between them.
std::optional<double> meet(const Vec3& o, const Vec3& d, const Vec3& a, const Vec3& ab,
                           const Vec3& ac) noexcept {
    const Vec3 p = cross(d, ac);
    const double det = dot(ab, p);
    if (det == 0) return std::nullopt;
    const double inverse = 1.0 / det;
    const Vec3 s = o - a;
    const double u = dot(s, p) * inverse;
    if (u < 0 || u > 1) return std::nullopt;
    const Vec3 q = cross(s, ab);
    const double v = dot(d, q) * inverse;
    if (v < 0 || u + v > 1) return std::nullopt;
    return dot(ac, q) * inverse;
}

/// Whether o + t d, for some t in [lo, hi], lies in the box.
bool reaches(const Vec3& o, const Vec3& d, const Vec3& min, const Vec3& max, double lo,
             double hi) noexcept {
    const std::array<double, 3> origin{o.x, o.y, o.z}, direction{d.x, d.y, d.z};
    const std::array<double, 3> low{min.x, min.y, min.z}, high{max.x, max.y, max.z};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (direction[axis] == 0) {
            if (origin[axis] < low[axis] || origin[axis] > high[axis]) return false;
            continue;
        }
        double t0 = (low[axis] - origin[axis]) / direction[axis];
        double t1 = (high[axis] - origin[axis]) / direction[axis];
        if (t0 > t1) std::swap(t0, t1);
        lo = std::max(lo, t0);
        hi = std::min(hi, t1);
        if (lo > hi) return false;
    }
    return true;
}

double component(const Vec3& v, int axis) noexcept {
    return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}

} // namespace

SurfaceRays::SurfaceRays(const ubundle::Geometry& geometry) {
    for (const ubundle::GeometryBatch& batch : geometry.batches) {
        if ((batch.polyFlags & (PF_TRANSLUCENT | PF_MODULATED)) != 0) continue;
        for (std::uint32_t i = batch.firstIndex; i + 3 <= batch.firstIndex + batch.indexCount;
             i += 3) {
            const Vec3 a = positionOf(geometry, geometry.indices[i]);
            const Vec3 b = positionOf(geometry, geometry.indices[i + 1]);
            const Vec3 c = positionOf(geometry, geometry.indices[i + 2]);
            triangles_.push_back(Triangle{a, b - a, c - a, i / 3});
        }
    }
    if (!triangles_.empty()) build(0, static_cast<std::uint32_t>(triangles_.size()));
}

std::uint32_t SurfaceRays::build(std::uint32_t first, std::uint32_t count) {
    const auto self = static_cast<std::uint32_t>(nodes_.size());
    nodes_.emplace_back();

    Vec3 min{INFINITE, INFINITE, INFINITE};
    Vec3 max{-INFINITE, -INFINITE, -INFINITE};
    Vec3 centreMin = min;
    Vec3 centreMax = max;
    const auto centreOf = [](const Triangle& t) { return t.a + (t.ab + t.ac) * (1.0 / 3.0); };
    for (std::uint32_t i = first; i < first + count; ++i) {
        const Triangle& t = triangles_[i];
        for (const Vec3& corner : {t.a, t.a + t.ab, t.a + t.ac}) {
            min = {std::min(min.x, corner.x), std::min(min.y, corner.y), std::min(min.z, corner.z)};
            max = {std::max(max.x, corner.x), std::max(max.y, corner.y), std::max(max.z, corner.z)};
        }
        const Vec3 centre = centreOf(t);
        centreMin = {std::min(centreMin.x, centre.x), std::min(centreMin.y, centre.y),
                     std::min(centreMin.z, centre.z)};
        centreMax = {std::max(centreMax.x, centre.x), std::max(centreMax.y, centre.y),
                     std::max(centreMax.z, centre.z)};
    }
    nodes_[self].min = min - Vec3{PAD, PAD, PAD};
    nodes_[self].max = max + Vec3{PAD, PAD, PAD};

    if (count <= LEAF_SIZE) {
        nodes_[self].first = first;
        nodes_[self].count = count;
        return self;
    }

    // Split at the median along the centres' longest extent. The order is
    // total -- the triangle number breaks a tie -- so the tree is the same on
    // every run and every compiler.
    const Vec3 extent = centreMax - centreMin;
    const int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : extent.y >= extent.z ? 1 : 2;
    std::sort(triangles_.begin() + first, triangles_.begin() + first + count,
              [&](const Triangle& a, const Triangle& b) {
                  const double ka = component(centreOf(a), axis);
                  const double kb = component(centreOf(b), axis);
                  return ka < kb || (ka == kb && a.index < b.index);
              });
    const std::uint32_t half = count / 2;
    const std::uint32_t left = build(first, half);
    const std::uint32_t right = build(first + half, count - half);
    nodes_[self].left = left;
    nodes_[self].right = right;
    return self;
}

std::optional<SurfaceRays::Hit> SurfaceRays::first(const Vec3& origin, const Vec3& direction) const {
    std::optional<Hit> best;
    if (nodes_.empty()) return best;
    std::vector<std::uint32_t> stack{0};
    while (!stack.empty()) {
        const Node& node = nodes_[stack.back()];
        stack.pop_back();
        // Inclusive at the best t, so a tie at it is still found.
        if (!reaches(origin, direction, node.min, node.max, 0, best ? best->t : INFINITE)) continue;
        if (node.count == 0) {
            stack.push_back(node.right);
            stack.push_back(node.left);
            continue;
        }
        for (std::uint32_t i = node.first; i < node.first + node.count; ++i) {
            const Triangle& t = triangles_[i];
            const std::optional<double> at = meet(origin, direction, t.a, t.ab, t.ac);
            if (!at || !(*at > 0)) continue;
            if (!best || *at < best->t || (*at == best->t && t.index < best->triangle))
                best = Hit{t.index, *at};
        }
    }
    return best;
}

bool SurfaceRays::blocked(const Vec3& a, const Vec3& b) const {
    if (nodes_.empty()) return false;
    const Vec3 direction = b - a;
    std::vector<std::uint32_t> stack{0};
    while (!stack.empty()) {
        const Node& node = nodes_[stack.back()];
        stack.pop_back();
        if (!reaches(a, direction, node.min, node.max, 0, 1)) continue;
        if (node.count == 0) {
            stack.push_back(node.right);
            stack.push_back(node.left);
            continue;
        }
        for (std::uint32_t i = node.first; i < node.first + node.count; ++i) {
            const Triangle& t = triangles_[i];
            const std::optional<double> at = meet(a, direction, t.a, t.ab, t.ac);
            if (at && *at > 0 && *at < 1) return true;
        }
    }
    return false;
}

} // namespace uta::ubake
