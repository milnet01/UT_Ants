// The two graphs over a level's actors: where a player can walk, and which
// actor fires which.
//
// docs/specs/UTA-0006-navigation-and-wiring-graphs.md.
//
// SCOPE: the types and the queries. Nothing here reads a package byte, and no
// member of any type below is an `upkg` type -- INV-5 and INV-6. That is what
// lets this library link uta_core alone, which is what keeps the package
// reader out of every runtime target (docs/design.md rule 2). Building the
// graphs is Build.h's job, in the other library.
//
// LIFETIME: unlike upkg's types, these OWN their strings (SS 4.6). A graph
// outlives the `Package` it was built from, because ubundle serialises it
// after that package may be closed.
//
// TWO INDEX SPACES, and confusing them is silent (SS 4.7). An actor is named
// by its EXPORT INDEX -- its slot in the package's export table. The vectors
// below are addressed by NODE POSITION -- an index into `nodes`. `nodeOf` is
// the bridge, and every consumer starts there.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace uta::unav {

/// One navigation point: an actor whose class descends from `NavigationPoint`.
struct NavNode {
    /// The actor's slot in the package's export table -- SS 4.2. Deliberately
    /// not a position in `Level::actors`, which drops null slots.
    std::uint32_t exportIndex = 0;
    /// Owned, not a view into the package -- SS 4.6.
    std::string className;
    /// This node's own run in `NavGraph::edges`, which is what makes
    /// `edgesFrom` a span rather than a search.
    std::uint32_t firstEdge = 0;
    std::uint32_t edgeCount = 0;
};

/// One reach spec whose endpoints both resolved to nodes, carried through with
/// the collision size the file built it for.
struct NavEdge {
    std::uint32_t from = 0; ///< NODE POSITION, not an export index
    std::uint32_t to = 0;   ///< NODE POSITION, not an export index
    std::int32_t distance = 0;
    /// Passed through ungraded. Nothing here or in UTA-0057 checks these
    /// against an independent source; UTA-0006 SS 14 keeps that open.
    std::int32_t collisionRadius = 0;
    std::int32_t collisionHeight = 0;
    std::int32_t reachFlags = 0;
    /// The file's own byte rather than a bool, for `upkg/Level.h`'s reason:
    /// nothing has measured that it is only ever 0 or 1.
    std::uint8_t pruned = 0;
};

/// A level's navigation graph.
///
/// `nodes` is in ASCENDING `exportIndex` order, which `nodeOf` relies on.
struct NavGraph {
    std::vector<NavNode> nodes;
    /// Grouped by `from`: one contiguous run per node, in the file's own spec
    /// order within a run.
    std::vector<NavEdge> edges;
    /// Counts unresolvable ENDPOINTS, not specs -- a spec with two bad
    /// endpoints adds 2. SS 4.5, INV-2. A count of zero is not the same as an
    /// absence, which is why this is counted rather than silently skipped.
    std::uint32_t discardedEndpoints = 0;
};

/// One actor carrying an explicit `Tag`, an explicit `Event`, or both.
struct WiringNode {
    std::uint32_t exportIndex = 0;
    /// Empty when the actor only fires.
    std::string tag;
    /// This node's run in `WiringGraph::edges`.
    std::uint32_t firstOutgoing = 0;
    std::uint32_t outgoingCount = 0;
    /// This node's run in `WiringGraph::incoming`.
    std::uint32_t firstIncoming = 0;
    std::uint32_t incomingCount = 0;
};

/// One `Event` resolved onto one actor carrying that `Tag`.
///
/// A tag is shared by several actors far more often than not, so one `Event`
/// yields one edge per actor carrying it -- INV-3.
struct WiringEdge {
    std::uint32_t from = 0; ///< NODE POSITION, not an export index
    std::uint32_t to = 0;   ///< NODE POSITION, not an export index
    std::string event;      ///< the `Tag` this edge was matched on
};

/// An `Event` naming a tag no actor in this level carries.
///
/// Returned rather than discarded because it is the only record that an author
/// wired something and the target went away -- SS 4.5, INV-4.
struct DanglingEvent {
    std::uint32_t from = 0; ///< NODE POSITION
    std::string event;
};

/// A level's event-wiring graph.
///
/// `nodes` is in ASCENDING `exportIndex` order, which `nodeOf` relies on.
struct WiringGraph {
    std::vector<WiringNode> nodes;
    /// Grouped by `from`.
    std::vector<WiringEdge> edges;
    /// The same edges, grouped by `to`, so `firing` is a span too. Whether
    /// this second order earns its storage is SS 14's open question.
    std::vector<WiringEdge> incoming;
    std::vector<DanglingEvent> dangling;
};

/// The node position of an actor, or nothing when it is not a node.
///
/// This is the bridge between the two index spaces. A consumer holds an export
/// index; every query below takes a node position. Passing an export index
/// where a position is wanted returns ANOTHER ACTOR'S edges with no error, so
/// this returning `std::nullopt` is the only thing standing between a consumer
/// and a wrong answer.
[[nodiscard]] std::optional<std::uint32_t> nodeOf(const NavGraph& graph,
                                                  std::uint32_t exportIndex);
[[nodiscard]] std::optional<std::uint32_t> nodeOf(const WiringGraph& graph,
                                                  std::uint32_t exportIndex);

/// The reach specs leading out of a node. Empty for a position out of range.
[[nodiscard]] std::span<const NavEdge> edgesFrom(const NavGraph& graph, std::uint32_t node);

/// What this actor fires. Empty for a position out of range.
[[nodiscard]] std::span<const WiringEdge> firedBy(const WiringGraph& graph, std::uint32_t node);

/// What fires at this actor. Empty for a position out of range.
[[nodiscard]] std::span<const WiringEdge> firing(const WiringGraph& graph, std::uint32_t node);

} // namespace uta::unav
