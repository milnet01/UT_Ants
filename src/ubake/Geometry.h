// The level's drawable surfaces as triangles --
// docs/specs/UTA-0109-map-geometry.md SS 4.3.
//
// BAKE-SIDE ONLY, as the rest of uta_ubake: it reads upkg's Model.
//
// THE SAME BYTES ON EVERY COMPILER. One thread, nodes in index order, batches
// in key order, and the arithmetic in double with contraction off
// (CMakeLists.txt, UTA-0049), so one Model gives one GEOM.
//
// SCOPE: which surfaces are drawn, and where their corners and texture
// coordinates land. What a flag means when drawn -- a portal, a sky surface, a
// two-sided one -- is the renderer's (UTA-0014); the flags go through
// untouched (SS 3 decision 4).

#pragma once

#include "core/Error.h"
#include "ubundle/Bundle.h"
#include "upkg/Geometry.h"
#include "upkg/Package.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace uta::ubake {

/// What sliceSway did to a map's moving surfaces -- UTA-0055 SS 4.2.
struct SwayReport {
    double waterSlice = 0;
    bool waterStill = false;
    bool clothStill = false;
    std::size_t vertices = 0;
    std::size_t clothVertices = 0;
};

/// The EPolyFlags bits this library reads. Source:
/// https://github.com/stephank/surreal/blob/master/Engine/Inc/UnObj.h, UT
/// 4.32's public headers.
inline constexpr std::uint32_t PF_INVISIBLE = 0x00000001u; ///< never drawn
inline constexpr std::uint32_t PF_MASKED = 0x00000002u;    ///< index-0 texels see-through
inline constexpr std::uint32_t PF_TRANSLUCENT = 0x00000004u; ///< blended; a flame sheet's -- UTA-0263
inline constexpr std::uint32_t PF_NOT_SOLID = 0x00000008u;   ///< nothing collides; a flame sheet's
inline constexpr std::uint32_t PF_TWO_SIDED = 0x00000100u;   ///< drawn from both sides -- UTA-0055
inline constexpr std::uint32_t PF_AUTO_U_PAN = 0x00000200u;  ///< pans along u -- UTA-0269
inline constexpr std::uint32_t PF_AUTO_V_PAN = 0x00000400u;  ///< pans along v -- UTA-0269

/// UTA-0276: how far a flagged surface pans a game second at a zone speed of
/// 1, in texels. Measured in the original 469 client on DM-Conveyor's belt by
/// the UT_MonsterHunt session, 2026-10-02: 34.4, 35.3 and 35.6 over three
/// runs. It replaced UTA-0269's 64, read from SurrealEngine's source.
inline constexpr double PAN_TEXELS_PER_SECOND = 35.0;

/// The furthest a drawn point may lie from the origin on any axis
/// (UTA-0109 SS 4.3 step 4): 2^24, past which an f32 no longer holds whole
/// units, so no real surface lies there. Unchecked, a corrupt file's point
/// reaches the probe and occlusion bakes' integer casts out of range.
/// tests/real/RealGeometryTest.cpp holds every library map inside it; the
/// furthest real point is MH-SeriousSam-Dunes-T1's, near -1.65 million.
inline constexpr double MAX_COORDINATE = 16777216.0;

/// UTA-0390: how far in front of an unzoned node buildGeometry looks for its
/// zone. The polygon on TheBoat's bow bends 18 units off its plane.
inline constexpr double ZONE_SEARCH_REACH = 64.0;

/// What a surface wears: a made material's id, and the texels one repeat of
/// its texture spans on each axis.
struct SurfaceMaterial {
    std::string id;
    double uSize = 0;
    double vSize = 0;
    bool cloth = false; ///< UTA-0055 SS 4.6: its picture is on umat's cloth list
    bool water = false; ///< UTA-0055 SS 4.1: it carries a Wet or Wave liquid look
};

/// The material a surface naming `texture` wears, masked or not, or nullptr
/// when none was made.
using MaterialLookup =
    std::function<const SurfaceMaterial*(upkg::ObjectReference texture, bool masked)>;

/// The level's drawable surfaces as triangles -- SS 4.3.
///
/// MalformedData, naming the node, when a drawn node's index leaves its table,
/// and when the vertices or indices emitted would reach 2^32. A node of fewer
/// than three vertices is checked for nothing; an invisible one for `iSurf`
/// only.
///
/// Every vertex of node n names zone `n.iZone[1]`, the zone its surface faces,
/// or 0 where that is not below `zoneCount` -- UTA-0156 SS 4.3.
///
/// UTA-0390: where that is 0 and `rooms` is given, the zone `ubundle::zoneAt`
/// finds in front of the polygon's centroid instead, stepping out along the
/// surface's normal 1, 2, 4 and on to ZONE_SEARCH_REACH units; still 0 where
/// no step finds one. UT99 still draws such a node; TheBoat's bow has one.
///
/// UTA-0310 SS 4.3 step 3a: a node is not drawn when invisible zone portals
/// in its coplanar chain (joined through `iPlane`) cover it, it lies on their
/// plane, and both its zones are open. Measured in the original client.
///
/// A surface in `omitted`, ascending, is not drawn: it became a FLAM record
/// (UTA-0263 SS 4.3). Its nodes are still checked for `iSurf`.
///
/// UTA-0269: a batch flagged PF_AutoUPan or PF_AutoVPan pans
/// PAN_TEXELS_PER_SECOND over the texture's size, in repeats a second at a
/// zone speed of 1. UTA-0276: the speed is the VIEWER's zone's, so the
/// renderer applies it each frame and the rate here carries none. Batches
/// split by rate.
///
/// UTA-0055 SS 4.1 to SS 4.3: a polygon wearing cloth on a two-sided or
/// not-solid surface, or water facing up or down, is cut by sliceSway and
/// drawn as its pieces, each corner carrying its sway kind and weight. What
/// the slicing did goes to `sway` where one is given.
[[nodiscard]] Result<ubundle::Geometry> buildGeometry(const upkg::Model& model,
                                                      const MaterialLookup& materials,
                                                      std::size_t zoneCount,
                                                      std::span<const std::uint32_t> omitted = {},
                                                      SwayReport* sway = nullptr,
                                                      const umap::RoomMap* rooms = nullptr);

} // namespace uta::ubake
