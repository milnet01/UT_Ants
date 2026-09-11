// Rays against the surfaces the renderer draws --
// docs/specs/UTA-0112-baked-light-probes.md SS 4.7.
//
// GEOM, not the collision tree: GEOM names each surface's material, which is
// what gives bounced light its colour, and it is what UTA-0014's shadow maps
// are drawn from (SS 3 decision 6).
//
// THE ANSWER DOES NOT DEPEND ON THE INDEX. A ray meets a triangle as Moller and
// Trumbore compute it, in double, from either side, and two hits at the same t
// go to the lower triangle number. The bounding-volume tree below only decides
// which triangles are tested, never which hit wins.

#pragma once

#include "ubake/CollisionQuery.h"
#include "ubundle/Bundle.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace uta::ubake {

class SurfaceRays {
public:
    /// An occluder is a triangle whose batch's polyFlags lack PF_Translucent
    /// (0x04) and PF_Modulated (0x40); nothing else is ever hit.
    explicit SurfaceRays(const ubundle::Geometry& geometry);

    struct Hit {
        std::size_t triangle = 0; ///< its first index's position in `indices`, over 3
        double t = 0;             ///< along the direction
    };

    /// The nearest occluder a ray from `origin` along `direction` meets at t > 0.
    [[nodiscard]] std::optional<Hit> first(const Vec3& origin, const Vec3& direction) const;

    /// Whether an occluder crosses the segment from `a` to `b` strictly between
    /// them.
    [[nodiscard]] bool blocked(const Vec3& a, const Vec3& b) const;

private:
    struct Triangle {
        Vec3 a, ab, ac;          ///< a corner, and the two edges from it
        std::size_t index = 0;   ///< its triangle number in GEOM
    };
    struct Node {
        Vec3 min, max;
        std::uint32_t first = 0; ///< into triangles_, for a leaf
        std::uint32_t count = 0; ///< 0 for an inner node
        std::uint32_t left = 0, right = 0;
    };

    std::uint32_t build(std::uint32_t first, std::uint32_t count);

    std::vector<Triangle> triangles_;
    std::vector<Node> nodes_;
};

} // namespace uta::ubake
