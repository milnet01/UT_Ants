// The level's shape, as the file stores it.
//
// docs/specs/UTA-0004-typed-level-content.md SS 4.4 and SS 4.5.
//
// SCOPE: this reader transcribes and does nothing else -- no BSP walk, no
// triangulation, no winding repair. SS 3.1 records that as the user's choice
// and why: turning tables into triangles is a design decision, it belongs
// where the bundle's shape is decided (`ubake`), and a reader that only
// transcribes can be checked against the file byte for byte, which SS 4.3
// turns into this item's whole acceptance test.
//
// LIFETIME: as Package.h -- these types hold no view into the package, but
// they are read FROM one, so the caller's bytes must outlive the read call.

#ifndef UTA_UPKG_GEOMETRY_H
#define UTA_UPKG_GEOMETRY_H

#include "core/Error.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"

#include <cstdint>
#include <vector>

namespace uta::upkg {

/// One brush polygon: its vertices, the texture it wears, and its flags.
///
/// `polyFlags` is what ROADMAP UTA-0004 calls "free information about glass,
/// water, sky and lava" -- it arrives here already paired with the texture it
/// applies to, which is why this is the surface most callers want.
struct Polygon {
    std::vector<Vector3> vertices;
    Vector3 base;
    Vector3 normal;
    Vector3 textureU;
    Vector3 textureV;
    std::uint32_t polyFlags = 0;
    /// Unresolved on purpose (SS 4.1): Package::objectName returns a view
    /// whose lifetime is the Package's, so resolving here would hand every
    /// caller a view it did not ask for and cannot outlive.
    ObjectReference actor;
    ObjectReference texture;
    std::uint32_t itemName = 0; // name index
    std::int32_t link = 0;
    std::int32_t brushPoly = 0;
    std::int16_t panU = 0;
    std::int16_t panV = 0;
};

struct Polys {
    std::vector<Polygon> polygons;
};

/// A `Polys` export: the brush polygons, with their textures and flags.
[[nodiscard]] Result<Polys> readPolys(const Package& package, const ExportEntry& entry);

} // namespace uta::upkg

#endif // UTA_UPKG_GEOMETRY_H
