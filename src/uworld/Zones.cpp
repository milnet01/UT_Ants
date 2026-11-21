#include "uworld/Zones.h"

#include <cstddef>

namespace uta::uworld {

umap::ZoneHit ZoneWorld::at(const Vec3& point) const {
    return umap::zoneNumberAt(*rooms_, umap::Point3{static_cast<float>(point.x), static_cast<float>(point.y),
                                                    static_cast<float>(point.z)});
}

const ubundle::Zone* ZoneWorld::zoneAt(const Vec3& point) const {
    const umap::ZoneHit hit = at(point);
    if (hit.kind != umap::ZoneHit::Kind::Zone || static_cast<std::size_t>(hit.zone) >= zones_.size()) return nullptr;
    return &zones_[hit.zone];
}

} // namespace uta::uworld
