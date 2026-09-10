// The NAVG section: unav's NavGraph --
// docs/specs/UTA-0008-bundle-container-and-origin.md SS 4.7 and SS 4.9.

#include "Sections.h"

namespace uta::ubundle::detail {
namespace {

constexpr std::uint64_t MIN_NAV_NODE = 16;
constexpr std::uint64_t MIN_NAV_EDGE = 25;

[[nodiscard]] Result<unav::NavNode> readNavNode(Cursor& cursor) {
    unav::NavNode node;
    UTA_TRY(node.exportIndex, cursor.readU32());
    UTA_TRY(node.className, readString(cursor));
    UTA_TRY(node.firstEdge, cursor.readU32());
    UTA_TRY(node.edgeCount, cursor.readU32());
    return node;
}

/// SS 4.7. `distance`, `collisionRadius`, `collisionHeight` and `reachFlags`
/// are four consecutive i32 -- the longest same-type run in this format and
/// the one where a transposition is least visible.
[[nodiscard]] Result<unav::NavEdge> readNavEdge(Cursor& cursor) {
    unav::NavEdge edge;
    UTA_TRY(edge.from, cursor.readU32());
    UTA_TRY(edge.to, cursor.readU32());
    UTA_TRY(edge.distance, cursor.readI32());
    UTA_TRY(edge.collisionRadius, cursor.readI32());
    UTA_TRY(edge.collisionHeight, cursor.readI32());
    UTA_TRY(edge.reachFlags, cursor.readI32());
    UTA_TRY(edge.pruned, cursor.readU8());
    return edge;
}

void putNavNode(Sink& sink, const unav::NavNode& node) {
    sink.putU32(node.exportIndex);
    sink.putString(node.className);
    sink.putU32(node.firstEdge);
    sink.putU32(node.edgeCount);
}

void putNavEdge(Sink& sink, const unav::NavEdge& edge) {
    sink.putU32(edge.from);
    sink.putU32(edge.to);
    sink.putI32(edge.distance);
    sink.putI32(edge.collisionRadius);
    sink.putI32(edge.collisionHeight);
    sink.putI32(edge.reachFlags);
    sink.putU8(edge.pruned);
}

} // namespace

Result<unav::NavGraph> readNavGraph(Cursor& cursor) {
    unav::NavGraph graph;
    UTA_TRY(graph.nodes, readVector<unav::NavNode>(cursor, MIN_NAV_NODE, "nav nodes", readNavNode));
    UTA_TRY(graph.edges, readVector<unav::NavEdge>(cursor, MIN_NAV_EDGE, "nav edges", readNavEdge));
    UTA_TRY(graph.discardedEndpoints, cursor.readU32());
    return graph;
}

Result<void> validateNavGraph(const unav::NavGraph& graph, ErrorCode code) {
    // nodeOf relies on the order; an unsorted table makes it return ANOTHER
    // actor's edges rather than fail.
    for (std::size_t i = 1; i < graph.nodes.size(); ++i)
        if (graph.nodes[i].exportIndex <= graph.nodes[i - 1].exportIndex)
            return fail(code, "NAVG: nodes are not in strictly ascending exportIndex order");

    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        const unav::NavNode& node = graph.nodes[i];
        if (!runWithin(node.firstEdge, node.edgeCount, graph.edges.size()))
            return fail(code, "NAVG: a node's edge run reaches past the edge table");
        for (std::uint32_t j = 0; j < node.edgeCount; ++j)
            if (graph.edges[static_cast<std::size_t>(node.firstEdge) + j].from != i)
                return fail(code, "NAVG: an edge in a node's run does not name that node");
    }

    for (const unav::NavEdge& edge : graph.edges)
        if (edge.from >= graph.nodes.size() || edge.to >= graph.nodes.size())
            return fail(code, "NAVG: an edge endpoint names no node");

    return {};
}

std::vector<std::byte> encodeNavGraph(const unav::NavGraph& graph) {
    Sink sink;
    sink.putVector(graph.nodes, putNavNode);
    sink.putVector(graph.edges, putNavEdge);
    sink.putU32(graph.discardedEndpoints);
    return std::move(sink).take();
}

} // namespace uta::ubundle::detail
