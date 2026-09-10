// The WIRG section: unav's WiringGraph --
// docs/specs/UTA-0008-bundle-container-and-origin.md SS 4.8 and SS 4.9.

#include "Sections.h"

namespace uta::ubundle::detail {
namespace {

constexpr std::uint64_t MIN_WIRING_NODE = 24;
constexpr std::uint64_t MIN_WIRING_EDGE = 12;
constexpr std::uint64_t MIN_DANGLING = 8;

[[nodiscard]] Result<unav::WiringNode> readWiringNode(Cursor& cursor) {
    unav::WiringNode node;
    UTA_TRY(node.exportIndex, cursor.readU32());
    UTA_TRY(node.tag, readString(cursor));
    UTA_TRY(node.firstOutgoing, cursor.readU32());
    UTA_TRY(node.outgoingCount, cursor.readU32());
    UTA_TRY(node.firstIncoming, cursor.readU32());
    UTA_TRY(node.incomingCount, cursor.readU32());
    return node;
}

[[nodiscard]] Result<unav::WiringEdge> readWiringEdge(Cursor& cursor) {
    unav::WiringEdge edge;
    UTA_TRY(edge.from, cursor.readU32());
    UTA_TRY(edge.to, cursor.readU32());
    UTA_TRY(edge.event, readString(cursor));
    return edge;
}

[[nodiscard]] Result<unav::DanglingEvent> readDangling(Cursor& cursor) {
    unav::DanglingEvent dangling;
    UTA_TRY(dangling.from, cursor.readU32());
    UTA_TRY(dangling.event, readString(cursor));
    return dangling;
}

void putWiringNode(Sink& sink, const unav::WiringNode& node) {
    sink.putU32(node.exportIndex);
    sink.putString(node.tag);
    sink.putU32(node.firstOutgoing);
    sink.putU32(node.outgoingCount);
    sink.putU32(node.firstIncoming);
    sink.putU32(node.incomingCount);
}

void putWiringEdge(Sink& sink, const unav::WiringEdge& edge) {
    sink.putU32(edge.from);
    sink.putU32(edge.to);
    sink.putString(edge.event);
}

void putDangling(Sink& sink, const unav::DanglingEvent& dangling) {
    sink.putU32(dangling.from);
    sink.putString(dangling.event);
}

} // namespace

Result<unav::WiringGraph> readWiringGraph(Cursor& cursor) {
    unav::WiringGraph graph;
    UTA_TRY(graph.nodes,
            readVector<unav::WiringNode>(cursor, MIN_WIRING_NODE, "wiring nodes", readWiringNode));
    UTA_TRY(graph.edges,
            readVector<unav::WiringEdge>(cursor, MIN_WIRING_EDGE, "wiring edges", readWiringEdge));
    UTA_TRY(graph.incoming,
            readVector<unav::WiringEdge>(cursor, MIN_WIRING_EDGE, "incoming edges", readWiringEdge));
    UTA_TRY(graph.dangling,
            readVector<unav::DanglingEvent>(cursor, MIN_DANGLING, "dangling events", readDangling));
    return graph;
}

Result<void> validateWiringGraph(const unav::WiringGraph& graph, ErrorCode code) {
    for (std::size_t i = 1; i < graph.nodes.size(); ++i)
        if (graph.nodes[i].exportIndex <= graph.nodes[i - 1].exportIndex)
            return fail(code, "WIRG: nodes are not in strictly ascending exportIndex order");

    // Weaker than the real invariant, which is that `incoming` is a
    // PERMUTATION of `edges`. SS 10 records it as partial rather than
    // claiming the check it is not.
    if (graph.incoming.size() != graph.edges.size())
        return fail(code, "WIRG: incoming and edges hold different numbers of edges");

    for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
        const unav::WiringNode& node = graph.nodes[i];
        if (!runWithin(node.firstOutgoing, node.outgoingCount, graph.edges.size()))
            return fail(code, "WIRG: a node's outgoing run reaches past the edge table");
        for (std::uint32_t j = 0; j < node.outgoingCount; ++j)
            if (graph.edges[static_cast<std::size_t>(node.firstOutgoing) + j].from != i)
                return fail(code, "WIRG: an edge in a node's outgoing run does not name that node");

        if (!runWithin(node.firstIncoming, node.incomingCount, graph.incoming.size()))
            return fail(code, "WIRG: a node's incoming run reaches past the incoming table");
        for (std::uint32_t j = 0; j < node.incomingCount; ++j)
            if (graph.incoming[static_cast<std::size_t>(node.firstIncoming) + j].to != i)
                return fail(code, "WIRG: an edge in a node's incoming run does not name that node");
    }

    for (const unav::WiringEdge& edge : graph.edges)
        if (edge.from >= graph.nodes.size() || edge.to >= graph.nodes.size())
            return fail(code, "WIRG: an edge endpoint names no node");
    for (const unav::WiringEdge& edge : graph.incoming)
        if (edge.from >= graph.nodes.size() || edge.to >= graph.nodes.size())
            return fail(code, "WIRG: an incoming edge endpoint names no node");

    for (const unav::DanglingEvent& dangling : graph.dangling)
        if (dangling.from >= graph.nodes.size())
            return fail(code, "WIRG: a dangling event names no node");

    return {};
}

std::vector<std::byte> encodeWiringGraph(const unav::WiringGraph& graph) {
    Sink sink;
    sink.putVector(graph.nodes, putWiringNode);
    sink.putVector(graph.edges, putWiringEdge);
    sink.putVector(graph.incoming, putWiringEdge);
    sink.putVector(graph.dangling, putDangling);
    return std::move(sink).take();
}

} // namespace uta::ubundle::detail
