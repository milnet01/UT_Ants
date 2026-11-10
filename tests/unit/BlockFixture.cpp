// Collision trees built from solid blocks -- BlockFixture.h.

#include "BlockFixture.h"

#include <cmath>
#include <cstdint>
#include <numbers>

namespace uta::test::blocks {
namespace {

/// A box's faces as corner indices, each a quad in order around it.
constexpr std::array<std::array<int, 4>, 6> FACES{{
    {0, 2, 6, 4}, {1, 3, 7, 5}, // -x, +x
    {0, 1, 5, 4}, {2, 3, 7, 6}, // -y, +y
    {0, 1, 3, 2}, {4, 5, 7, 6}, // -z, +z
}};

/// NF_NotCsg -- the 432 headers' Engine/Inc/UnObj.h.
constexpr std::uint8_t NOT_CSG = 0x01;

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

std::array<float, 3> asFloats(const Vec3& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

/// A node on the quad's plane carrying the quad as its outline. Its normal
/// faces away from `inside`.
ubundle::CollisionNode faceNode(ubundle::CollisionTree& tree, const std::array<Vec3, 4>& quad,
                                const Vec3& inside) {
    Vec3 normal = cross(quad[1] - quad[0], quad[3] - quad[0]);
    normal = normal * (1.0 / uworld::length(normal));
    if (uworld::dot(normal, inside - quad[0]) > 0) normal = normal * -1.0;
    ubundle::CollisionNode node;
    node.normal = asFloats(normal);
    node.distance = static_cast<float>(uworld::dot(normal, quad[0]));
    node.firstOutline = static_cast<std::uint32_t>(tree.outline.size());
    node.outlineCount = 4;
    for (const Vec3& corner : quad) {
        tree.outline.push_back(static_cast<std::uint32_t>(tree.points.size()));
        tree.points.push_back(asFloats(corner));
    }
    return node;
}

} // namespace

Block box(const Vec3& min, const Vec3& max, double yawDegrees) {
    const double a = yawDegrees * std::numbers::pi / 180;
    return box(min, max, std::cos(a), std::sin(a));
}

Block box(const Vec3& min, const Vec3& max, double c, double s) {
    const Vec3 centre = (min + max) * 0.5;
    Block block;
    for (int k = 0; k < 8; ++k) {
        const Vec3 p{(k & 1) != 0 ? max.x : min.x, (k & 2) != 0 ? max.y : min.y, (k & 4) != 0 ? max.z : min.z};
        const Vec3 d = p - centre;
        block.corners[static_cast<std::size_t>(k)] = centre + Vec3{c * d.x - s * d.y, s * d.x + c * d.y, d.z};
    }
    return block;
}

ubundle::CollisionTree solidBlocks(const std::vector<Block>& blocks, const std::vector<Sheet>& sheets) {
    ubundle::CollisionTree tree;
    tree.outside = true;
    const auto next = [&] { return static_cast<std::int32_t>(tree.nodes.size()) + 1; };

    for (const Sheet& sheet : sheets) {
        ubundle::CollisionNode node = faceNode(tree, sheet.corners, sheet.corners[0] - Vec3{0, 0, 1});
        node.nodeFlags = NOT_CSG;
        node.front = next();
        node.back = next();
        tree.nodes.push_back(node);
    }
    for (std::size_t b = 0; b < blocks.size(); ++b) {
        Vec3 inside{};
        for (const Vec3& corner : blocks[b].corners) inside = inside + corner * 0.125;
        const auto nextBlock = static_cast<std::int32_t>(tree.nodes.size() + FACES.size());
        for (std::size_t f = 0; f < FACES.size(); ++f) {
            std::array<Vec3, 4> quad{};
            for (std::size_t k = 0; k < 4; ++k)
                quad[k] = blocks[b].corners[static_cast<std::size_t>(FACES[f][k])];
            ubundle::CollisionNode node = faceNode(tree, quad, inside);
            node.back = f + 1 < FACES.size() ? next() : -1;
            node.front = b + 1 < blocks.size() ? nextBlock : -1;
            tree.nodes.push_back(node);
        }
    }
    // A sheet with nothing after it leads nowhere: every leaf is outside.
    for (auto& node : tree.nodes)
        if (node.front >= static_cast<std::int32_t>(tree.nodes.size())) node.front = node.back = -1;
    return tree;
}

} // namespace uta::test::blocks
