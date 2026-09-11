// Point and segment checks against a collision tree --
// docs/specs/UTA-0121-bot-path-seeds.md SS 4.4.
//
// A reader of UTA-0111's tree, to that item's SS 4.5: a walk starts at node 0
// with the tree's `outside`, descends by FBspNode::ChildOutside with no extra
// flags, and a point with normal . p > distance takes `front`. Body-against-
// level collision for the game is UTA-0017's; this tests points and segments.

#pragma once

#include "ubundle/Bundle.h"

#include <cmath>

namespace uta::paths {

/// A point or a direction, in UT99's units and on its axes.
struct Vec3 {
    double x = 0, y = 0, z = 0;

    friend Vec3 operator+(const Vec3& a, const Vec3& b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    friend Vec3 operator-(const Vec3& a, const Vec3& b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    friend Vec3 operator*(const Vec3& a, double s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
    friend bool operator==(const Vec3&, const Vec3&) = default;
};

[[nodiscard]] inline double dot(const Vec3& a, const Vec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] inline double length(const Vec3& a) noexcept {
    return std::sqrt(dot(a, a));
}

/// The distance between two points on X and Y alone.
[[nodiscard]] inline double horizontal(const Vec3& a, const Vec3& b) noexcept {
    return std::hypot(a.x - b.x, a.y - b.y);
}

/// UTA-0111 SS 4.5: whether `p` is in empty space.
[[nodiscard]] bool isEmpty(const ubundle::CollisionTree& tree, const Vec3& p);

struct Hit {
    double fraction = 1; ///< along a to b; 1 when nothing is hit
    Vec3 normal{};       ///< the crossed plane's normal, facing a
};

/// The first point from a to b where empty space turns solid. A segment
/// starting in solid returns fraction 0, and no normal.
[[nodiscard]] Hit trace(const ubundle::CollisionTree& tree, const Vec3& a, const Vec3& b);

/// The first point from a to b where solid turns empty: SS 4.5's step down
/// from a floor to where space is empty again. A segment starting in empty
/// space returns fraction 0, and no normal.
[[nodiscard]] Hit traceOut(const ubundle::CollisionTree& tree, const Vec3& a, const Vec3& b);

} // namespace uta::paths
