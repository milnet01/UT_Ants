// docs/specs/UTA-0439-still-movers.md SS 4.3.

#include "ubake/StillMovers.h"

#include "ubake/LightModel.h"

#include <cmath>
#include <utility>

namespace uta::ubake {

Vec3 placed(const Vec3& location, const std::array<std::int32_t, 3>& rotation, const std::array<float, 3>& postScale,
            const Vec3& q) noexcept {
    const double cp = cosineOf(rotation[0]), sp = sineOf(rotation[0]);
    const double cy = cosineOf(rotation[1]), sy = sineOf(rotation[1]);
    const double cr = cosineOf(rotation[2]), sr = sineOf(rotation[2]);
    const Vec3 r{q.x, cr * q.y + sr * q.z, -sr * q.y + cr * q.z};   // roll, about X
    const Vec3 p{cp * r.x - sp * r.z, r.y, sp * r.x + cp * r.z};    // pitch
    const Vec3 y{cy * p.x - sy * p.y, sy * p.x + cy * p.y, p.z};    // yaw, about Z
    return location + Vec3{postScale[0] * y.x, postScale[1] * y.y, postScale[2] * y.z};
}

std::optional<ubundle::Geometry> chartedGeometry(const ubundle::Geometry& level,
                                                 const std::vector<ubundle::MoverShape>& movers) {
    std::optional<ubundle::Geometry> out;
    for (const ubundle::MoverShape& mover : movers) {
        if (mover.still == 0) continue;
        if (!out) out = level;
        const auto firstVertex = static_cast<std::uint32_t>(out->vertices.size());
        const auto firstIndex = static_cast<std::uint32_t>(out->indices.size());
        const Vec3 location{mover.location[0], mover.location[1], mover.location[2]};
        const std::array<float, 3> unit{1, 1, 1};
        for (ubundle::GeometryVertex vertex : mover.geometry.vertices) {
            const Vec3 p = placed(location, mover.rotation, mover.postScale,
                                  {vertex.position[0], vertex.position[1], vertex.position[2]});
            // A normal turns with the shape and divides by its scale, as a
            // plane does under a stretch, then is made unit again.
            Vec3 n = placed({}, mover.rotation, unit, {vertex.normal[0], vertex.normal[1], vertex.normal[2]});
            n = {n.x / mover.postScale[0], n.y / mover.postScale[1], n.z / mover.postScale[2]};
            const double length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            if (length > 0) n = {n.x / length, n.y / length, n.z / length};
            vertex.position = {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)};
            vertex.normal = {static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z)};
            out->vertices.push_back(vertex);
        }
        // A mirroring postScale turns each triangle's front to its back, so
        // its winding swaps, as UTA-0119 SS 4.5 step 3 does for MainScale.
        const bool mirrored = mover.postScale[0] * mover.postScale[1] * mover.postScale[2] < 0;
        for (std::size_t i = 0; i < mover.geometry.indices.size(); ++i) {
            const std::size_t corner = i % 3;
            const std::size_t from = mirrored && corner != 0 ? i + (corner == 1 ? 1 : -1) : i;
            out->indices.push_back(firstVertex + mover.geometry.indices[from]);
        }
        for (ubundle::GeometryBatch batch : mover.geometry.batches) {
            batch.firstIndex += firstIndex;
            out->batches.push_back(std::move(batch));
        }
    }
    return out;
}

} // namespace uta::ubake
