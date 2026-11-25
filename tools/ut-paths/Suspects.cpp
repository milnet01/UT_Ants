// UTA-0514: suspect spots for UT_MonsterHunt's undrawn-frame probe.

#include "Suspects.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
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

Suspects suspectSpots(const upkg::Model& model, const WalkGraph& graph, std::size_t most) {
    Suspects out;
    const auto count = static_cast<std::uint32_t>(graph.spots.size());
    out.standing = count;

    // Each cube's count and first spot, keyed by the cube's corner.
    using Cube = std::tuple<std::int64_t, std::int64_t, std::int64_t>;
    std::map<Cube, std::pair<std::size_t, std::uint32_t>> cubes;
    for (std::uint32_t i = 0; i < count; ++i) {
        const Vec3 eye = graph.spots[i].centre + Vec3{0, 0, EYE_HEIGHT};
        const BspSide side = bspSideAt(model, eye);
        if (!side.empty || side.leaf >= 0) continue;
        ++out.leafless;
        const auto cell = [](double v) { return static_cast<std::int64_t>(std::floor(v / SUSPECT_CELL)); };
        auto& cube = cubes[Cube{cell(eye.x), cell(eye.y), cell(eye.z)}];
        if (cube.first++ == 0) cube.second = i;
    }
    out.clusters = cubes.size();

    // Most suspects first; the map's order already breaks ties by corner.
    std::vector<std::pair<std::size_t, std::uint32_t>> ranked;
    for (const auto& [corner, cube] : cubes) ranked.push_back(cube);
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    for (std::size_t k = 0; k < ranked.size() && k < most; ++k)
        out.spots.push_back(graph.spots[ranked[k].second].centre);
    return out;
}

std::string addSpotsLines(const std::vector<Vec3>& spots) {
    std::ostringstream out;
    const auto whole = [](double v) { return static_cast<long long>(std::llround(v)); };
    for (const Vec3& p : spots)
        out << "AddSpots=(X=" << whole(p.x) << ",Y=" << whole(p.y) << ",Z=" << whole(p.z) << ")\n";
    return out.str();
}

} // namespace uta::paths
