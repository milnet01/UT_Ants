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

#include <cstdint>
#include <functional>
#include <string>

namespace uta::ubake {

/// The EPolyFlags bits this library reads. Source:
/// https://github.com/stephank/surreal/blob/master/Engine/Inc/UnObj.h, UT
/// 4.32's public headers.
inline constexpr std::uint32_t PF_INVISIBLE = 0x00000001u; ///< never drawn
inline constexpr std::uint32_t PF_MASKED = 0x00000002u;    ///< index-0 texels see-through

/// What a surface wears: a made material's id, and the texels one repeat of
/// its texture spans on each axis.
struct SurfaceMaterial {
    std::string id;
    double uSize = 0;
    double vSize = 0;
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
[[nodiscard]] Result<ubundle::Geometry> buildGeometry(const upkg::Model& model,
                                                      const MaterialLookup& materials);

} // namespace uta::ubake
