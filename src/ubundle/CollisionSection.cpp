// The COLL section: the level's collision tree and each mover's --
// docs/specs/UTA-0111-level-collision.md SS 4.2, INV-1 and INV-2.
//
// A TREE IS CHECKED BY WALKING IT, not by index order. UT99's level trees
// store some children before their parents and keep nodes no walk reaches
// (SS 2 item 5), so SS 4.2 item 2's walk is the one rule that accepts every
// real tree and still proves a walk from the root ends.

#include "Sections.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace uta::ubundle::detail {
namespace {

using Point = std::array<float, 3>;

/// SS 4.2: four f32, four i32, a u8 and three u32, fixed.
constexpr std::uint64_t MIN_COLLISION_NODE = 45;
/// SS 4.2: a u32 and a u8, fixed.
constexpr std::uint64_t MIN_HULL_PLANE = 5;
/// SS 4.2: an empty plane vector, then six f32.
constexpr std::uint64_t MIN_COLLISION_HULL = 28;
/// SS 4.2: a u32, then a tree of four empty vectors and a u8.
constexpr std::uint64_t MIN_MOVER_COLLISION = 21;
/// Three f32, fixed.
constexpr std::uint64_t MIN_POINT = 12;

/// A bool byte other than 0 or 1 is refused here. A bool in memory holds no
/// other value, so this rule has no write side.
[[nodiscard]] Result<bool> readBool(Cursor& cursor) {
    UTA_TRY(const std::uint8_t byte, cursor.readU8());
    if (byte > 1)
        return fail(ErrorCode::MalformedData,
                    "COLL: a bool byte " + std::to_string(byte) + " is not 0 or 1");
    return byte == 1;
}

[[nodiscard]] Result<Point> readPoint(Cursor& cursor) {
    Point point{};
    for (float& part : point) {
        UTA_TRY(part, cursor.readF32());
    }
    return point;
}

[[nodiscard]] Result<CollisionNode> readNode(Cursor& cursor) {
    CollisionNode node;
    for (float& part : node.normal) {
        UTA_TRY(part, cursor.readF32());
    }
    UTA_TRY(node.distance, cursor.readF32());
    UTA_TRY(node.back, cursor.readI32());
    UTA_TRY(node.front, cursor.readI32());
    UTA_TRY(node.coplanar, cursor.readI32());
    UTA_TRY(node.hull, cursor.readI32());
    UTA_TRY(node.nodeFlags, cursor.readU8());
    UTA_TRY(node.polyFlags, cursor.readU32());
    UTA_TRY(node.firstOutline, cursor.readU32());
    UTA_TRY(node.outlineCount, cursor.readU32());
    return node;
}

[[nodiscard]] Result<HullPlane> readPlane(Cursor& cursor) {
    HullPlane plane;
    UTA_TRY(plane.node, cursor.readU32());
    UTA_TRY(plane.flipped, readBool(cursor));
    return plane;
}

[[nodiscard]] Result<CollisionHull> readHull(Cursor& cursor) {
    CollisionHull hull;
    UTA_TRY(hull.planes, readVector<HullPlane>(cursor, MIN_HULL_PLANE, "hull planes", readPlane));
    for (float& part : hull.min) {
        UTA_TRY(part, cursor.readF32());
    }
    for (float& part : hull.max) {
        UTA_TRY(part, cursor.readF32());
    }
    return hull;
}

[[nodiscard]] Result<CollisionTree> readTree(Cursor& cursor) {
    CollisionTree tree;
    UTA_TRY(tree.nodes,
            readVector<CollisionNode>(cursor, MIN_COLLISION_NODE, "collision nodes", readNode));
    UTA_TRY(tree.points, readVector<Point>(cursor, MIN_POINT, "collision points", readPoint));
    UTA_TRY(tree.outline,
            readVector<std::uint32_t>(cursor, MIN_U32, "outline entries", readU32Element));
    UTA_TRY(tree.hulls, readVector<CollisionHull>(cursor, MIN_COLLISION_HULL, "hulls", readHull));
    UTA_TRY(tree.outside, readBool(cursor));
    return tree;
}

[[nodiscard]] Result<MoverCollision> readMover(Cursor& cursor) {
    MoverCollision mover;
    UTA_TRY(mover.exportIndex, cursor.readU32());
    UTA_TRY(mover.tree, readTree(cursor));
    return mover;
}

/// `index` names one of `size` entries.
[[nodiscard]] bool names(std::int64_t index, std::size_t size) noexcept {
    return index >= 0 && static_cast<std::uint64_t>(index) < size;
}

/// SS 4.2's six rules over one tree. `where` is "level" or "mover tree <n>".
[[nodiscard]] Result<void> validateTree(const CollisionTree& tree, const std::string& where,
                                        ErrorCode code) {
    const std::size_t count = tree.nodes.size();
    const auto refuse = [&](const std::string& what) {
        return fail(code, "COLL: " + where + ": " + what);
    };

    // 1. Every link is -1 or a node. Checked over every node before the walk,
    // which follows them.
    for (std::size_t n = 0; n < count; ++n) {
        const CollisionNode& node = tree.nodes[n];
        for (const auto& [name, link] : {std::pair<const char*, std::int32_t>{"back", node.back},
                                         {"front", node.front},
                                         {"coplanar", node.coplanar}})
            if (link != -1 && !names(link, count))
                return refuse("node " + std::to_string(n) + "'s " + name + " names node "
                              + std::to_string(link) + " of " + std::to_string(count));
    }

    // 2. A walk from node 0 over the three links reaches no node twice, so no
    // walk loops. A node no walk reaches is kept, as UT99 keeps it.
    if (count > 0) {
        std::vector<bool> seen(count, false);
        std::vector<std::size_t> stack{0};
        while (!stack.empty()) {
            const std::size_t n = stack.back();
            stack.pop_back();
            if (seen[n])
                return refuse("node " + std::to_string(n) + " is reached twice by a walk from node 0");
            seen[n] = true;
            const CollisionNode& node = tree.nodes[n];
            for (const std::int32_t link : {node.back, node.front, node.coplanar})
                if (link != -1) stack.push_back(static_cast<std::size_t>(link));
        }
    }

    // 3 and 4. A node's hull, and its outline's run.
    for (std::size_t n = 0; n < count; ++n) {
        const CollisionNode& node = tree.nodes[n];
        if (node.hull != -1 && !names(node.hull, tree.hulls.size()))
            return refuse("node " + std::to_string(n) + "'s hull names hull "
                          + std::to_string(node.hull) + " of " + std::to_string(tree.hulls.size()));
        if (node.outlineCount == 1 || node.outlineCount == 2)
            return refuse("node " + std::to_string(n) + "'s outlineCount is "
                          + std::to_string(node.outlineCount) + ", neither 0 nor 3 and more");
        if (!runWithin(node.firstOutline, node.outlineCount, tree.outline.size()))
            return refuse("node " + std::to_string(n) + "'s outline runs past the "
                          + std::to_string(tree.outline.size()) + " entries of outline");
    }

    // 5. Every outline entry names a point.
    for (std::size_t i = 0; i < tree.outline.size(); ++i)
        if (tree.outline[i] >= tree.points.size())
            return refuse("outline entry " + std::to_string(i) + " names point "
                          + std::to_string(tree.outline[i]) + " of "
                          + std::to_string(tree.points.size()));

    // 6. Every hull plane names a node.
    for (std::size_t h = 0; h < tree.hulls.size(); ++h)
        for (std::size_t p = 0; p < tree.hulls[h].planes.size(); ++p)
            if (tree.hulls[h].planes[p].node >= count)
                return refuse("hull " + std::to_string(h) + "'s plane " + std::to_string(p)
                              + " names node " + std::to_string(tree.hulls[h].planes[p].node)
                              + " of " + std::to_string(count));
    return {};
}

void putPoint(Sink& sink, const Point& point) {
    for (const float part : point) sink.putF32(part);
}

void putNode(Sink& sink, const CollisionNode& node) {
    for (const float part : node.normal) sink.putF32(part);
    sink.putF32(node.distance);
    sink.putI32(node.back);
    sink.putI32(node.front);
    sink.putI32(node.coplanar);
    sink.putI32(node.hull);
    sink.putU8(node.nodeFlags);
    sink.putU32(node.polyFlags);
    sink.putU32(node.firstOutline);
    sink.putU32(node.outlineCount);
}

void putPlane(Sink& sink, const HullPlane& plane) {
    sink.putU32(plane.node);
    sink.putU8(plane.flipped ? 1 : 0);
}

void putHull(Sink& sink, const CollisionHull& hull) {
    sink.putVector(hull.planes, putPlane);
    for (const float part : hull.min) sink.putF32(part);
    for (const float part : hull.max) sink.putF32(part);
}

void putTree(Sink& sink, const CollisionTree& tree) {
    sink.putVector(tree.nodes, putNode);
    sink.putVector(tree.points, putPoint);
    sink.putVector(tree.outline, [](Sink& out, std::uint32_t index) { out.putU32(index); });
    sink.putVector(tree.hulls, putHull);
    sink.putU8(tree.outside ? 1 : 0);
}

void putMover(Sink& sink, const MoverCollision& mover) {
    sink.putU32(mover.exportIndex);
    putTree(sink, mover.tree);
}

} // namespace

Result<Collision> readCollision(Cursor& cursor) {
    Collision collision;
    UTA_TRY(collision.level, readTree(cursor));
    UTA_TRY(collision.movers,
            readVector<MoverCollision>(cursor, MIN_MOVER_COLLISION, "mover trees", readMover));
    return collision;
}

Result<void> validateCollision(const Collision& collision, ErrorCode code) {
    UTA_CHECK(validateTree(collision.level, "level", code));
    for (std::size_t i = 0; i < collision.movers.size(); ++i) {
        // Strictly ascending, which also makes each slot unique.
        if (i > 0 && !(collision.movers[i - 1].exportIndex < collision.movers[i].exportIndex))
            return fail(code, "COLL: mover tree " + std::to_string(i)
                                  + "'s exportIndex does not sort strictly after the one before it");
        UTA_CHECK(validateTree(collision.movers[i].tree, "mover tree " + std::to_string(i), code));
    }
    return {};
}

std::vector<std::byte> encodeCollision(const Collision& collision) {
    Sink sink;
    putTree(sink, collision.level);
    sink.putVector(collision.movers, putMover);
    return std::move(sink).take();
}

} // namespace uta::ubundle::detail
