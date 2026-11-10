// A collision tree's surfaces as triangles -- CollisionMesh.h.

#include "uworld/CollisionMesh.h"

#include <algorithm>
#include <cstddef>

namespace uta::uworld {
namespace {

Vec3 toVec3(const std::array<float, 3>& p) { return {p[0], p[1], p[2]}; }

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

/// UTA-0111 SS 4.5: the nodes a walk from node 0 over back, front and
/// coplanar reaches. Every link is in range (that item's SS 4.2 item 2). A
/// real tree reaches no node twice; one that does is walked once a node.
std::vector<bool> reachedNodes(const ubundle::CollisionTree& tree) {
    std::vector<bool> reached(tree.nodes.size(), false);
    if (tree.nodes.empty()) return reached;
    std::vector<std::int32_t> stack{0};
    while (!stack.empty()) {
        const auto index = static_cast<std::size_t>(stack.back());
        stack.pop_back();
        if (reached[index]) continue;
        reached[index] = true;
        const ubundle::CollisionNode& node = tree.nodes[index];
        for (const std::int32_t next : {node.back, node.front, node.coplanar})
            if (next >= 0) stack.push_back(next);
    }
    return reached;
}

} // namespace

CollisionMesh collisionMesh(const ubundle::CollisionTree& tree) {
    CollisionMesh mesh;
    mesh.points.reserve(tree.points.size());
    for (const auto& p : tree.points) mesh.points.push_back(toVec3(p));

    const std::vector<bool> reached = reachedNodes(tree);
    for (std::size_t i = 0; i < tree.nodes.size(); ++i) {
        const ubundle::CollisionNode& node = tree.nodes[i];
        if (!reached[i] || node.outlineCount < 3) continue;
        const auto first = tree.outline.begin() + node.firstOutline;
        const std::vector<std::uint32_t> ring(first, first + node.outlineCount);

        Vec3 centroid{};
        for (const std::uint32_t k : ring) centroid = centroid + mesh.points[k];
        centroid = centroid * (1.0 / static_cast<double>(ring.size()));
        const Vec3 normal = toVec3(node.normal) * (1.0 / length(toVec3(node.normal)));

        // SS 4.2 items 1 and 2: keep a face where any of its samples lies
        // between empty and solid. A face partly covered still bounds space
        // where it is open, and dropping it would leave a hole there.
        int frontEmptyVotes = 0, boundaries = 0, sheetSamples = 0;
        std::vector<Vec3> samples{centroid};
        for (const std::uint32_t k : ring) samples.push_back((centroid + mesh.points[k]) * 0.5);
        for (const Vec3& sample : samples) {
            const bool frontEmpty = isEmpty(tree, sample + normal * FACE_PROBE);
            const bool backEmpty = isEmpty(tree, sample - normal * FACE_PROBE);
            if (frontEmpty != backEmpty) {
                ++boundaries;
                if (frontEmpty) ++frontEmptyVotes;
            } else if (frontEmpty) {
                ++sheetSamples;
            }
        }
        if (boundaries == 0) {
            if (sheetSamples == static_cast<int>(samples.size()))
                ++mesh.sheets;
            else
                ++mesh.buried;
            continue;
        }
        const bool frontEmpty = 2 * frontEmptyVotes >= boundaries;

        // Item 3: wind it to face the empty side. The outline's own order
        // says nothing about which way it faces (UTA-0111 SS 4.5), so its
        // winding is measured: Newell's sum, robust to a collinear first
        // three points.
        Vec3 winding{};
        for (std::size_t k = 0; k < ring.size(); ++k)
            winding = winding + cross(mesh.points[ring[k]], mesh.points[ring[(k + 1) % ring.size()]]);
        const Vec3 empty = frontEmpty ? normal : normal * -1.0;
        std::vector<std::uint32_t> ordered = ring;
        if (dot(winding, empty) < 0) std::ranges::reverse(ordered);
        for (std::size_t k = 1; k + 1 < ordered.size(); ++k)
            mesh.triangles.push_back({ordered[0], ordered[k], ordered[k + 1]});
    }
    return mesh;
}

} // namespace uta::uworld
