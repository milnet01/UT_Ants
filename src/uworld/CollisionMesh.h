// A collision tree's surfaces as triangles -- docs/specs/UTA-0457-body-collision.md
// SS 4.2. What a body collides with: BodyCollision.h builds its meshes from
// this, and UTA-0114's physics world will too.

#pragma once

#include "ubundle/Bundle.h"
#include "uworld/CollisionQuery.h"

#include <array>
#include <cstdint>
#include <vector>

namespace uta::uworld {

/// How far either side of a face SS 4.2 asks whether space is empty.
inline constexpr double FACE_PROBE = 0.5;

struct CollisionMesh {
    std::vector<Vec3> points;                            ///< the tree's points, in its own space
    std::vector<std::array<std::uint32_t, 3>> triangles; ///< into points; each faces empty space
    std::size_t sheets = 0; ///< faces dropped for empty space on both sides at every sample
    std::size_t buried = 0; ///< faces dropped with no sample between empty and solid, not sheets
};

/// SS 4.2: every face a walk from node 0 reaches, with an outline of three or
/// more points, that has empty space on exactly one side at one or more of its
/// samples -- its centroid, and the midpoints from there to each corner. Fanned
/// into triangles wound so that (b - a) x (c - a) points into the side most of
/// those samples find empty.
[[nodiscard]] CollisionMesh collisionMesh(const ubundle::CollisionTree& tree);

} // namespace uta::uworld
