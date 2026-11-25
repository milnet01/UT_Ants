// UTA-0514: suspect spots for UT_MonsterHunt's undrawn-frame probe.

#include "Suspects.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <limits>
#include <tuple>

namespace uta::paths {

BspSide bspSideAt(const upkg::Model& model, const Vec3& point) {
    const auto count = static_cast<std::int64_t>(model.nodes.size());
    std::int64_t node = 0;
    // A sound tree reaches a leaf in fewer steps than it has nodes.
    for (std::int64_t steps = 0; node >= 0 && node < count && steps < count; ++steps) {
        const upkg::BspNode& n = model.nodes[static_cast<std::size_t>(node)];
        const double d = double(n.plane.normal.x) * point.x + double(n.plane.normal.y) * point.y
                         + double(n.plane.normal.z) * point.z - double(n.plane.w);
        const bool front = d >= 0;
        const std::int32_t next = front ? n.iFront : n.iBack;
        if (next < 0) return BspSide{front, n.iLeaf[front ? 1 : 0]};
        node = next;
    }
    return BspSide{};
}

namespace {

/// UTA-0516: which spots a walk from `start` reaches, each join taken both
/// ways. None when the start stands on no spot.
std::vector<char> reachedFrom(const WalkGraph& graph, std::optional<std::uint32_t> start) {
    const std::size_t count = graph.spots.size();
    std::vector<char> reached(count, 0);
    if (!start.has_value() || *start >= count) return reached;
    std::vector<std::vector<std::uint32_t>> both(count);
    for (std::uint32_t a = 0; a < count; ++a)
        for (const std::uint32_t b : graph.joins[a]) {
            both[a].push_back(b);
            both[b].push_back(a);
        }
    std::vector<std::uint32_t> queue{*start};
    reached[*start] = 1;
    while (!queue.empty()) {
        const std::uint32_t at = queue.back();
        queue.pop_back();
        for (const std::uint32_t next : both[at])
            if (reached[next] == 0) {
                reached[next] = 1;
                queue.push_back(next);
            }
    }
    return reached;
}

constexpr std::uint32_t NO_SPOT = std::numeric_limits<std::uint32_t>::max();

/// One cube's suspects.
struct Cube {
    std::size_t count = 0;
    std::uint32_t first = NO_SPOT;        ///< its first suspect spot
    std::uint32_t firstReached = NO_SPOT; ///< its first one a walk reaches
};

} // namespace

Suspects suspectSpots(const upkg::Model& model, const WalkGraph& graph, std::optional<std::uint32_t> start,
                      std::size_t most) {
    Suspects out;
    const auto count = static_cast<std::uint32_t>(graph.spots.size());
    out.standing = count;
    const std::vector<char> reached = reachedFrom(graph, start);

    // Keyed by the cube's corner, so the map's order is the corner order.
    std::map<std::tuple<std::int64_t, std::int64_t, std::int64_t>, Cube> cubes;
    for (std::uint32_t i = 0; i < count; ++i) {
        const Vec3 eye = graph.spots[i].centre + Vec3{0, 0, EYE_HEIGHT};
        const BspSide side = bspSideAt(model, eye);
        if (!side.empty || side.leaf >= 0) continue;
        ++out.leafless;
        const auto cell = [](double v) { return static_cast<std::int64_t>(std::floor(v / SUSPECT_CELL)); };
        Cube& cube = cubes[{cell(eye.x), cell(eye.y), cell(eye.z)}];
        if (cube.count++ == 0) cube.first = i;
        if (reached[i] != 0 && cube.firstReached == NO_SPOT) cube.firstReached = i;
    }
    out.clusters = cubes.size();

    // Reached cubes first, each shown by its first reached spot; then most
    // suspects first; the stable sort keeps corner order for the rest.
    std::vector<Cube> ranked;
    for (const auto& [corner, cube] : cubes) ranked.push_back(cube);
    std::stable_sort(ranked.begin(), ranked.end(), [](const Cube& a, const Cube& b) {
        const bool aReached = a.firstReached != NO_SPOT, bReached = b.firstReached != NO_SPOT;
        if (aReached != bReached) return aReached;
        return a.count > b.count;
    });
    for (std::size_t k = 0; k < ranked.size() && k < most; ++k) {
        const bool isReached = ranked[k].firstReached != NO_SPOT;
        out.spots.push_back(graph.spots[isReached ? ranked[k].firstReached : ranked[k].first].centre);
        if (isReached) ++out.reached;
    }
    return out;
}

std::string addSpotsLines(const std::vector<Vec3>& spots, std::size_t reached) {
    std::ostringstream out;
    const auto whole = [](double v) { return static_cast<long long>(std::llround(v)); };
    out << "; reached " << reached << " of " << spots.size() << "\n";
    for (const Vec3& p : spots)
        out << "AddSpots=(X=" << whole(p.x) << ",Y=" << whole(p.y) << ",Z=" << whole(p.z) << ")\n";
    return out.str();
}

} // namespace uta::paths
