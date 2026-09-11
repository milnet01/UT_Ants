// GEOM built in memory for UTA-0112's probe cases --
// docs/specs/UTA-0112-baked-light-probes.md SS 7. The matching COLL tree comes
// from PathFixture.h's worldOf, one region per box.

#pragma once

#include "ubake/CollisionQuery.h"
#include "ubundle/Bundle.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace uta::test::light {

using uta::ubake::Vec3;

/// One triangle and what it wears. `normal` is stored on all three corners,
/// as GEOM stores a surface's normal.
struct Triangle {
    std::array<Vec3, 3> corners;
    Vec3 normal;
    std::string material;
    std::uint32_t polyFlags = 0;
};

/// The flat rectangle with corners a, b, c, d in order around it, as two
/// triangles.
[[nodiscard]] std::vector<Triangle> quad(const Vec3& a, const Vec3& b, const Vec3& c,
                                         const Vec3& d, const Vec3& normal,
                                         const std::string& material,
                                         std::uint32_t polyFlags = 0);

struct Face {
    std::string material;
    std::uint32_t polyFlags = 0;
};

/// The six faces of the box from `min` to `max`, each facing into it, in the
/// order -X wall, +X wall, -Y wall, +Y wall, floor, ceiling.
[[nodiscard]] std::vector<Triangle> room(const Vec3& min, const Vec3& max,
                                         const std::array<Face, 6>& faces);

/// GEOM holding `triangles`, in batches ascending by material then flags, each
/// batch a run of `indices` -- UTA-0109 SS 4.2's order.
[[nodiscard]] ubundle::Geometry geometryOf(std::vector<Triangle> triangles);

} // namespace uta::test::light
