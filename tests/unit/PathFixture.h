// Collision trees built in memory, for ut-paths' cases --
// docs/specs/UTA-0121-bot-path-seeds.md SS 7. Shared by PathWalkableTest and
// PathSeedsTest.
//
// A WORLD IS SOLID EXCEPT WHERE A REGION SAYS. Each region is a convex room:
// the planes it lies in front of. The tree is a chain of them, every node a
// CSG node (UTA-0111 SS 4.5's IsCsg), so going front means "inside this plane"
// and going back means "try the next region". Each back link leads to a fresh
// copy of the regions after it, so the tree is a tree -- a walk from node 0
// reaches no node twice -- at a size that grows as the product of the regions'
// plane counts, which is why a case keeps to a few boxes.

#pragma once

#include "ut-paths/Trace.h"
#include "ubundle/Bundle.h"

#include <vector>

namespace uta::test::paths {

using uta::paths::Vec3;

/// A plane a region lies in front of: normal . p > distance inside it. The
/// normal is a unit vector.
struct Plane {
    Vec3 normal;
    double distance = 0;
};

using Region = std::vector<Plane>;

/// The box from `min` to `max`.
[[nodiscard]] Region box(const Vec3& min, const Vec3& max);

/// A tree whose empty space is the union of `regions`, none empty, and whose
/// points are the corners of the box from `low` to `high` -- which is what the
/// walk graph's columns cover.
[[nodiscard]] ubundle::CollisionTree worldOf(const std::vector<Region>& regions, const Vec3& low,
                                             const Vec3& high);

} // namespace uta::test::paths
