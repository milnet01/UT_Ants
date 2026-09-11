// A level's collision tree and each mover's --
// docs/specs/UTA-0111-level-collision.md SS 4.3 and SS 4.4.
//
// SCOPE: transcribe, do not interpret (SS 3 decision 3). Every node field is
// the Model's own value. Which nodes are solid, how an extent is tested and
// which extra flags a caller passes are the readers' -- UTA-0017 and UTA-0114,
// to SS 4.5.
//
// THE SAME BYTES ON EVERY COMPILER, as Movers.h: a mover's tree is moved by a
// subtraction, a multiplication, a division and a square root in double, and
// nothing else.

#pragma once

#include "core/Error.h"
#include "ubake/Movers.h"
#include "ubundle/Bundle.h"
#include "upkg/Geometry.h"

namespace uta::ubake {

/// A Model's collision tree, by SS 4.3.
///
/// MalformedData, naming the node, for each of SS 4.3's refusals, and naming
/// the Model for a rootOutside other than 0 or 1. Every refusal reaches the
/// nodes buildGeometry skips too: an invisible node can still be solid.
[[nodiscard]] Result<ubundle::CollisionTree> buildCollision(const upkg::Model& model);

/// One mover's tree, by SS 4.4, from its Model already read, in the pivot
/// space of its MOVR shape.
///
/// buildCollision's refusal comes back naming the actor; a MainScale with a
/// zero component is refused as buildMover refuses it.
[[nodiscard]] Result<ubundle::MoverCollision> buildMoverCollision(const MoverSite& mover,
                                                                  const upkg::Model& model,
                                                                  const ubundle::Placements& actors);

} // namespace uta::ubake
