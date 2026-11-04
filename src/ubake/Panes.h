// Whether a material is a glass pane -- UTA-0272.
//
// A FILE OF ITS OWN because it changes for its own reason: the name list is
// tuned on the census below, and neither the material making nor the geometry
// in Bake.cpp moves when it is.
//
// BY NAME, because nothing else tells glass apart. PolyFlags cannot: of the
// visible Translucent surfaces in the reference install, three in four are
// flames, liquids, beams and the like. The name alone cannot either: two in
// five glass-named surfaces are drawn opaque or invisible. The renderer
// therefore reflects a pane only where it is drawn translucent. Census of
// 2026-11-04, every map in the install, recorded on UTA-0272.

#pragma once

#include <string_view>

namespace uta::ubake {

/// True when `materialId`'s texture name -- the part after its last '.', any
/// "#masked" dropped -- holds "glas" or "window". umat::materialId folds ids
/// to lower case, so the match is on lower case. It covers every glass name
/// the census ranked (Glass, SpecGlas, Sglass, Tglass_R, sGlasW6, DemGlass1,
/// Window8, Nwindow4, ...) and leaves out Crystal, which is a glowing crystal.
[[nodiscard]] inline bool isGlassPane(std::string_view materialId) {
    if (const auto hash = materialId.rfind('#'); hash != std::string_view::npos)
        materialId = materialId.substr(0, hash);
    const auto dot = materialId.rfind('.');
    const std::string_view name = dot == std::string_view::npos ? materialId : materialId.substr(dot + 1);
    return name.find("glas") != std::string_view::npos || name.find("window") != std::string_view::npos;
}

} // namespace uta::ubake
