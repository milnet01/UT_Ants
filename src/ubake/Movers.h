// A level's movers and their shapes -- docs/specs/UTA-0119-mover-shapes.md
// SS 4.3 to SS 4.5.
//
// SCOPE: which actors are movers, and each one's shape in pivot space. Placing
// a shape in the world is the renderer's, to SS 4.5's formula; moving one is
// not yet queued.
//
// THE SAME BYTES ON EVERY COMPILER, as Geometry.h: the transform here is a
// subtraction, a multiplication and a division in double, and no sine or
// cosine, which is why SS 3 decision 2 leaves the rotation to the renderer.

#pragma once

#include "core/Error.h"
#include "ubake/Geometry.h"
#include "ubundle/Bundle.h"
#include "upkg/Geometry.h"
#include "upkg/Package.h"

#include <array>
#include <cstddef>
#include <vector>

namespace uta::ubake {

/// One mover: its placement in PLAC, and its Brush's Model export.
struct MoverSite {
    std::size_t placement = 0;                ///< into Placements::actors
    const upkg::ExportEntry* model = nullptr; ///< a Model export of the map
};

/// The movers among `actors`, by SS 4.3, in export order.
///
/// MalformedData, naming the actor, when a mover's own Brush names no Model
/// export of the map. An actor whose Brush is null is not a mover.
[[nodiscard]] Result<std::vector<MoverSite>> findMovers(const upkg::Package& map,
                                                        const ubundle::Placements& actors);

/// A mover's pivot space -- SS 4.4: its MainScale, resolved as doubles, and
/// its PrePivot, as floats.
struct PivotSpace {
    std::array<double, 3> mainScale{1, 1, 1};
    std::array<float, 3> prePivot{};

    /// `p` in pivot space: MainScale times (p - PrePivot), per axis in double,
    /// stored as float -- SS 4.5 step 2. UTA-0111 SS 4.4 moves a collision
    /// tree's points with this same function, so a corner a shape and a tree
    /// both hold is the same float.
    [[nodiscard]] std::array<float, 3> point(const std::array<float, 3>& p) const noexcept;
};

/// `mover`'s pivot space, by SS 4.4. MalformedData, naming the actor, for a
/// MainScale with a zero component.
[[nodiscard]] Result<PivotSpace> pivotSpaceOf(const MoverSite& mover,
                                              const ubundle::Placements& actors);

/// One mover's shape, by SS 4.4 and SS 4.5, from its Model already read.
///
/// buildGeometry's refusal comes back with its own code, naming the actor
/// (INV-9); a MainScale with a zero component is MalformedData, naming it.
[[nodiscard]] Result<ubundle::MoverShape> buildMover(const MoverSite& mover,
                                                     const upkg::Model& model,
                                                     const ubundle::Placements& actors,
                                                     const MaterialLookup& lookup);

} // namespace uta::ubake
