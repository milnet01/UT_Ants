// Which zone a point of the level is in, and what that zone does to a body
// standing there -- docs/specs/UTA-0493-leaving-the-world.md SS 4.3.
//
// SCOPE: the lookup only. The rules that act on it, a death outside the level
// and a pain zone's clock, are ugame's tick (SS 4.4, SS 4.5).

#ifndef UTA_UWORLD_ZONES_H
#define UTA_UWORLD_ZONES_H

#include "ubundle/Bundle.h"
#include "umap/Rooms.h"
#include "uworld/CollisionQuery.h"

#include <span>

namespace uta::uworld {

/// A bundle's ROOM map and ZONE entries, as the tick reads them. Holds
/// references: the bundle outlives it.
class ZoneWorld {
public:
    ZoneWorld(const umap::RoomMap& rooms, std::span<const ubundle::Zone> zones) : rooms_(&rooms), zones_(zones) {}

    /// What `point` is in: umap::zoneNumberAt.
    [[nodiscard]] umap::ZoneHit at(const Vec3& point) const;

    /// The ZONE entry of the zone `point` is in, or nullptr where it is in no
    /// zone or its zone is at or past the bundle's zone count, which neither
    /// hurts nor heals.
    [[nodiscard]] const ubundle::Zone* zoneAt(const Vec3& point) const;

private:
    const umap::RoomMap* rooms_;
    std::span<const ubundle::Zone> zones_;
};

} // namespace uta::uworld

#endif // UTA_UWORLD_ZONES_H
