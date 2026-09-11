// The level's placed actors, their classes and their lights --
// docs/specs/UTA-0110-lights-and-placements.md SS 4.5 and SS 4.6.
//
// SCOPE: this transcribes. Which classes matter, and what each actor becomes,
// is resolution, which is UTA-0023's and runs when an actor spawns (ADR-0004).

#pragma once

#include "core/Error.h"
#include "ubundle/Bundle.h"
#include "upkg/Class.h"
#include "upkg/Level.h"
#include "upkg/Package.h"

#include <functional>
#include <string_view>
#include <vector>

namespace uta::ubake {

struct Actors {
    ubundle::Placements placements;
    std::vector<ubundle::Light> lights; ///< strictly ascending by exportIndex
};

/// Every actor `level` places, the classes they belong to with their ancestry
/// and merged defaults, and the lights among them. `mapName` is the map's
/// folded stem.
///
/// An actor slot that is not an export of the map, or that names an export a
/// slot before it already named, is MalformedData naming the slot. A class that
/// does not resolve is recorded as such, and is not a refusal (SS 4.5).
[[nodiscard]] Result<Actors> buildActors(const upkg::Package& map, std::string_view mapName,
                                         const upkg::Level& level,
                                         const upkg::PackageResolver& resolver);

namespace detail {

/// UTA-0110 SS 4.6's order, which UTA-0119 SS 4.4 reuses: the actor's own
/// record named `name` (folded) at array index 0 that `fits`, else its class's
/// default that does, else none. A record of the right name that does not fit
/// is passed over, and the next source is tried.
[[nodiscard]] const ubundle::PropertyRecord* resolvedRecord(
    std::string_view name, const std::vector<ubundle::PropertyRecord>& own,
    const std::vector<ubundle::PropertyRecord>& defaults,
    const std::function<bool(const ubundle::PropertyRecord&)>& fits);

} // namespace detail

} // namespace uta::ubake
