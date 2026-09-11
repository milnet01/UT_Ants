#include "PathFixture.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace uta::test::paths {
namespace {

std::array<float, 3> asFloats(const Vec3& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

/// Appends region `k`'s chain, each back link leading to a fresh copy of the
/// regions after it; returns the chain's first node.
std::int32_t chain(ubundle::CollisionTree& tree, const std::vector<Region>& regions, std::size_t k) {
    const Region& region = regions[k];
    const std::size_t first = tree.nodes.size();
    for (const Plane& plane : region) {
        ubundle::CollisionNode node;
        node.normal = asFloats(plane.normal);
        node.distance = static_cast<float>(plane.distance);
        node.outlineCount = 3; // IsCsg needs vertices; which ones does not matter
        tree.nodes.push_back(node);
    }
    for (std::size_t j = 0; j < region.size(); ++j) {
        // By index: the recursion below grows `tree.nodes`.
        const std::int32_t back = k + 1 < regions.size() ? chain(tree, regions, k + 1) : -1;
        tree.nodes[first + j].back = back;
        tree.nodes[first + j].front =
            j + 1 < region.size() ? static_cast<std::int32_t>(first + j + 1) : -1;
    }
    return static_cast<std::int32_t>(first);
}

} // namespace

Region box(const Vec3& min, const Vec3& max) {
    return {
        {{1, 0, 0}, min.x}, {{-1, 0, 0}, -max.x}, {{0, 1, 0}, min.y},
        {{0, -1, 0}, -max.y}, {{0, 0, 1}, min.z}, {{0, 0, -1}, -max.z},
    };
}

ubundle::CollisionTree worldOf(const std::vector<Region>& regions, const Vec3& low, const Vec3& high) {
    ubundle::CollisionTree tree;
    for (int corner = 0; corner < 8; ++corner)
        tree.points.push_back(asFloats({(corner & 1) != 0 ? high.x : low.x,
                                        (corner & 2) != 0 ? high.y : low.y,
                                        (corner & 4) != 0 ? high.z : low.z}));
    tree.outline = {0, 1, 2};
    chain(tree, regions, 0);
    return tree;
}

} // namespace uta::test::paths
