// A level export: its actors, and the bot path graph joining its waypoints.
//
// docs/specs/UTA-0057-level-tail-and-reachspecs.md, and
// docs/specs/UTA-0004-typed-level-content.md SS 4.9 for the actor array.
//
// SCOPE, as Geometry.h: this reader transcribes. It resolves no reference,
// builds no adjacency list and interprets no actor class. UTA-0057 INV-5 is
// what says so, and the reason is UTA-0006's -- a convenience graph here is a
// second vocabulary for the same thing, and the one downstream consumer binds
// to it before anyone decides it was right.
//
// LIFETIME: as Package.h -- these types hold no view into the package, but
// they are read FROM one, so the caller's bytes must outlive the read call.

#pragma once

#include "core/Error.h"
#include "upkg/Package.h"

#include <cstdint>
#include <vector>

namespace uta::upkg {

/// One directed connection between two navigation points, as the file stores
/// it: the two actors it joins, and the collision size it was built for.
///
/// `start` and `end` are object references to ACTORS, not indices into any
/// array -- derived over the whole reference install rather than assumed.
/// They are returned unresolved for Geometry.h's reason.
///
/// `pruned` is the file's own byte rather than a bool: nothing here has
/// measured that it is only ever 0 or 1, and narrowing it would be a claim
/// this reader has not earned.
struct ReachSpec {
    std::int32_t distance = 0;
    ObjectReference start;
    ObjectReference end;
    std::int32_t collisionRadius = 0;
    std::int32_t collisionHeight = 0;
    std::int32_t reachFlags = 0;
    std::uint8_t pruned = 0;
};

/// A `Level` export's actor array and its reach-spec array.
///
/// The level's `FURL` and the fields after the reach specs are CONSUMED and
/// not returned -- nothing in the roadmap wants a level's URL or its editor
/// text blocks, and UTA-0004 SS 4.3 requires only that they be consumed.
struct Level {
    /// Non-null slots only. UTA-0004 INV-9.
    std::vector<ObjectReference> actors;
    /// Slots the file declared, including the null ones. UTA-0004 INV-9.
    std::uint32_t rawSlotCount = 0;
    /// File order, file indexing: position `i` is the file's index `i`, and
    /// nothing is dropped, reordered or renumbered. UTA-0057 INV-1, and it is
    /// what makes an actor's `Paths` value resolvable against this array.
    std::vector<ReachSpec> reachSpecs;
};

/// A `Level` export: the actors, and the reach specs joining its waypoints.
[[nodiscard]] Result<Level> readLevel(const Package& package, const ExportEntry& entry);

} // namespace uta::upkg
