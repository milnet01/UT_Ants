#include "unav/Graphs.h"

#include <algorithm>
#include <cstddef>

namespace uta::unav {
namespace {

/// Both graphs hold `nodes` in ascending `exportIndex` order, so the lookup is
/// a binary search. Shared between the two overloads because the only thing
/// that differs between them is the node type.
template <typename Nodes>
std::optional<std::uint32_t> positionOf(const Nodes& nodes, std::uint32_t exportIndex) {
    const auto found =
        std::lower_bound(nodes.begin(), nodes.end(), exportIndex,
                         [](const auto& node, std::uint32_t index) {
                             return node.exportIndex < index;
                         });
    if (found == nodes.end() || found->exportIndex != exportIndex) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(found - nodes.begin());
}

/// A node's run inside one of the edge vectors, bounded by what that vector
/// actually holds rather than by what the node claims. The offset and count
/// come from the builder, and a span built from an unchecked pair hands a
/// consumer an out-of-bounds read -- UTA-0003 SS 4.2's reason for owning
/// bounds in one place, applied to a graph a caller may also build by hand.
template <typename Edge>
std::span<const Edge> run(const std::vector<Edge>& storage, std::uint32_t first,
                          std::uint32_t count) {
    if (first > storage.size() || count > storage.size() - first) {
        return {};
    }
    return std::span<const Edge>{storage.data() + static_cast<std::size_t>(first), count};
}

} // namespace

std::optional<std::uint32_t> nodeOf(const NavGraph& graph, std::uint32_t exportIndex) {
    return positionOf(graph.nodes, exportIndex);
}

std::optional<std::uint32_t> nodeOf(const WiringGraph& graph, std::uint32_t exportIndex) {
    return positionOf(graph.nodes, exportIndex);
}

std::span<const NavEdge> edgesFrom(const NavGraph& graph, std::uint32_t node) {
    if (node >= graph.nodes.size()) {
        return {};
    }
    const NavNode& source = graph.nodes[node];
    return run(graph.edges, source.firstEdge, source.edgeCount);
}

std::span<const WiringEdge> firedBy(const WiringGraph& graph, std::uint32_t node) {
    if (node >= graph.nodes.size()) {
        return {};
    }
    const WiringNode& source = graph.nodes[node];
    return run(graph.edges, source.firstOutgoing, source.outgoingCount);
}

std::span<const WiringEdge> firing(const WiringGraph& graph, std::uint32_t node) {
    if (node >= graph.nodes.size()) {
        return {};
    }
    const WiringNode& target = graph.nodes[node];
    return run(graph.incoming, target.firstIncoming, target.incomingCount);
}

} // namespace uta::unav
