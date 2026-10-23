// ubake: the brushes a map's recipe hides, and the openings they leave
// covered -- docs/specs/UTA-0109-map-geometry.md SS 4.6 (UTA-0396).
//
// A hidden brush's surfaces are not drawn. Where it cut through another
// surface, the plane of that surface keeps a hole the shape of the brush's
// cross-section; a cover fills it, drawn as that surface. Only drawing
// changes: collision, zones and the bakes' rays read the Model as it is.

#pragma once

#include "core/Error.h"
#include "ubake/Geometry.h"
#include "upkg/Geometry.h"
#include "upkg/Package.h"
#include "urecipe/Recipe.h"

#include <cstdint>
#include <vector>

namespace uta::ubake {

struct Hiding {
    std::vector<std::uint32_t> surfaces; ///< not drawn; ascending, each once
    std::vector<Cover> covers;           ///< one per hidden brush, in recipe order
};

/// SS 4.6, every brush of `hidden` in order. Refuses MalformedData naming the
/// section when its brush or cover brush is not in the map, the brush has no
/// surface, the cover names no drawn surface of its brush or one the hiding
/// brush owns, or the brush's drawn surfaces do not cross the cover's plane.
[[nodiscard]] Result<Hiding> hideBrushes(const upkg::Package& map, const upkg::Model& model,
                                         const std::vector<urecipe::HiddenBrush>& hidden);

} // namespace uta::ubake
