// Building the two graphs from what `upkg` already returns.
//
// docs/specs/UTA-0006-navigation-and-wiring-graphs.md SS 4.1.
//
// BAKE-SIDE ONLY. This library links uta_upkg, so no runtime target may link
// it -- docs/design.md rule 2, which is why the TYPES live in Graphs.h in a
// library that links uta_core alone. A single library would breach that rule
// through this item.
//
// These builders RESOLVE, JOIN and VALIDATE, and read no bytes of their own:
// `buildNavGraph` takes the `Level` UTA-0057 returns rather than re-reading it,
// because a second decoder of that layout is what INV-6 forbids.

#pragma once

#include "core/Error.h"
#include "unav/Graphs.h"
#include "upkg/Class.h"
#include "upkg/Level.h"
#include "upkg/Package.h"

namespace uta::unav {

/// Join a level's reach specs onto its navigation points.
///
/// The resolver is what lets the node filter walk a class's ancestry into
/// another package (SS 4.3); `upkg/Class.h` owns its contract. An endpoint that
/// does not resolve to a node is dropped and counted rather than refused --
/// SS 4.5, INV-2.
[[nodiscard]] Result<NavGraph> buildNavGraph(const upkg::Package& package,
                                            const upkg::Level& level,
                                            const upkg::PackageResolver& resolver);

/// Join every actor's `Event` onto every actor carrying that `Tag`.
///
/// Takes no `Level`: the wiring graph is a property of the package's exports,
/// and needs no ancestry walk (SS 4.3). An `Event` naming a tag no actor
/// carries is returned in `dangling` rather than dropped -- SS 4.5, INV-4.
[[nodiscard]] Result<WiringGraph> buildWiringGraph(const upkg::Package& package);

} // namespace uta::unav
