// The level's flame sheets, as FLAM records --
// docs/specs/UTA-0263-shader-flames.md SS 4.3 and SS 4.5.
//
// BAKE-SIDE ONLY, as the rest of uta_ubake: it reads upkg's Model.
//
// A SHEET LEAVES GEOM. A surface this finds becomes a record and is not drawn
// as a surface too; buildGeometry is handed the surfaces to leave out. A
// surface whose indices leave their tables is never a sheet, so it stays in
// GEOM, where buildGeometry refuses it.
//
// THE SAME BYTES ON EVERY COMPILER: surfaces in index order, arithmetic in
// double with contraction off (CMakeLists.txt, UTA-0049).

#pragma once

#include "ubundle/Bundle.h"
#include "upkg/Geometry.h"
#include "upkg/Package.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace uta::ubake {

/// How far above a sheet's lowest point another point may lie and still be on
/// its lowest edge, in world units. A sheet is hand-placed on UT99's grid, so
/// a level edge's corners share a height to far better than this.
inline constexpr double LOWEST_EDGE_TOLERANCE = 1.0;

/// The MATS record a surface naming `texture` wears, masked or not, when that
/// record has a flame look; empty otherwise.
using FlameMaterialLookup =
    std::function<std::optional<std::uint32_t>(upkg::ObjectReference texture, bool masked)>;

/// A flame surface carrying a sheet's flags that makes no record -- SS 6.
struct SkippedFlame {
    std::uint32_t surface = 0; ///< its index in the level's surface list
    std::string reason;
};

struct FlameSheets {
    /// One per flame, crossed sheets merged, ascending by seed. `light` is -1
    /// until assignFlameLights runs.
    std::vector<ubundle::Flame> flames;
    /// Per flame, every sheet surface merged into it, ascending -- what
    /// UTA-0256 SS 4.3 copies a fitting's flames by.
    std::vector<std::vector<std::uint32_t>> sources;
    /// The surfaces that became records, ascending: GEOM leaves them out.
    std::vector<std::uint32_t> surfaces;
    std::vector<SkippedFlame> skipped;
};

/// SS 4.3: every surface wearing a flame, with PF_NOT_SOLID and one of
/// PF_TRANSLUCENT or PF_MASKED, is a sheet. Its drawn nodes' corners give its
/// base, width and height; two sheets of one material whose bases lie closer
/// than half the smaller width merge, pairwise until none do.
[[nodiscard]] FlameSheets findFlameSheets(const upkg::Model& model, const FlameMaterialLookup& materials);

/// SS 4.5: each flame names the nearest light within 1.5 x its height of its
/// base, ties to the lower index, or -1. A light a strip absorbed is never
/// named -- it is drawn by its row's leader, so it has nothing of its own to
/// flicker. `lights` is LITE as the bake writes it.
void assignFlameLights(std::vector<ubundle::Flame>& flames, const std::vector<ubundle::Light>& lights);

} // namespace uta::ubake
