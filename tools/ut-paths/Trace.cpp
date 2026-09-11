// Point and segment checks against a collision tree --
// docs/specs/UTA-0121-bot-path-seeds.md SS 4.4, INV-2.

#include "Trace.h"

#include <cstdint>
#include <vector>

namespace uta::paths {
namespace {

using ubundle::CollisionNode;
using ubundle::CollisionTree;

/// NF_NotCsg and NF_IsNew -- the 432 headers' Engine/Inc/UnObj.h.
constexpr std::uint8_t NOT_CSG = 0x01;
constexpr std::uint8_t IS_NEW = 0x20;

/// FBspNode::IsCsg with no extra flags (UTA-0111 SS 4.5). A node's outline is
/// its vertices: UTA-0111 SS 4.3 keeps outlineCount at NumVertices.
bool isCsg(const CollisionNode& node) {
    return node.outlineCount > 0 && (node.nodeFlags & (NOT_CSG | IS_NEW)) == 0;
}

/// FBspNode::ChildOutside with no extra flags; `front` is child 1.
bool childOutside(const CollisionNode& node, bool front, bool outside) {
    return front ? (outside || isCsg(node)) : (outside && !isCsg(node));
}

Vec3 normalOf(const CollisionNode& node) {
    return {node.normal[0], node.normal[1], node.normal[2]};
}

/// Positive in front of the node's plane; on it and behind, not.
double side(const CollisionNode& node, const Vec3& p) {
    return dot(normalOf(node), p) - node.distance;
}

/// A part of the segment, from t0 to t1 along it, still to descend from
/// `node`, and the plane it starts on, facing the segment's start: a hit at
/// its start is on that plane.
struct Piece {
    std::int32_t node = -1;
    double t0 = 0, t1 = 1;
    bool outside = false;
    Vec3 startNormal{};
};

/// The first point from a to b where space turns solid, or where it turns
/// empty. A split pushes its far part before its near one, so parts are
/// walked in order along the segment and the first leaf of the kind wanted is
/// the first crossing. Explicit rather than recursive: a real tree is deep.
Hit firstChange(const CollisionTree& tree, const Vec3& a, const Vec3& b, bool intoSolid) {
    const Vec3 delta = b - a;
    std::vector<Piece> pieces{Piece{tree.nodes.empty() ? -1 : 0, 0, 1, tree.outside, {}}};
    while (!pieces.empty()) {
        const Piece piece = pieces.back();
        pieces.pop_back();
        if (piece.node < 0) {
            // A leaf is empty where it is outside.
            if (piece.outside != intoSolid) return Hit{piece.t0, piece.startNormal};
            continue;
        }
        const CollisionNode& node = tree.nodes[static_cast<std::size_t>(piece.node)];
        const double d0 = side(node, a + delta * piece.t0);
        const double d1 = side(node, a + delta * piece.t1);
        // Split only where the piece straddles the plane (SS 4.4). A piece
        // touching it at one end goes with its other end: a split's far part
        // starts ON the plane, and where a later node shares that plane -- a
        // coplanar node, or two rooms sharing a face -- it would otherwise
        // leave a solid piece of no length, a hit where nothing is.
        if ((d0 >= 0 && d1 >= 0 && (d0 > 0 || d1 > 0)) || (d0 <= 0 && d1 <= 0)) {
            const bool front = d0 > 0 || d1 > 0;
            pieces.push_back(Piece{front ? node.front : node.back, piece.t0, piece.t1,
                                   childOutside(node, front, piece.outside), piece.startNormal});
            continue;
        }
        const bool front0 = d0 > 0;
        const bool front1 = d1 > 0;
        const double t = piece.t0 + (piece.t1 - piece.t0) * (d0 / (d0 - d1));
        const Vec3 facing = front0 ? normalOf(node) : normalOf(node) * -1.0;
        pieces.push_back(Piece{front1 ? node.front : node.back, t, piece.t1,
                               childOutside(node, front1, piece.outside), facing});
        pieces.push_back(Piece{front0 ? node.front : node.back, piece.t0, t,
                               childOutside(node, front0, piece.outside), piece.startNormal});
    }
    return {};
}

} // namespace

bool isEmpty(const CollisionTree& tree, const Vec3& p) {
    bool outside = tree.outside;
    for (std::int32_t at = tree.nodes.empty() ? -1 : 0; at >= 0;) {
        const CollisionNode& node = tree.nodes[static_cast<std::size_t>(at)];
        const bool front = side(node, p) > 0;
        outside = childOutside(node, front, outside);
        at = front ? node.front : node.back;
    }
    return outside;
}

Hit trace(const CollisionTree& tree, const Vec3& a, const Vec3& b) {
    return firstChange(tree, a, b, true);
}

Hit traceOut(const CollisionTree& tree, const Vec3& a, const Vec3& b) {
    return firstChange(tree, a, b, false);
}

} // namespace uta::paths
