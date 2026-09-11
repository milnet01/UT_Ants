// A level's collision tree and each mover's --
// docs/specs/UTA-0111-level-collision.md SS 4.3 and SS 4.4.

#include "ubake/Collision.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace uta::ubake {
namespace {

/// Bit 30 of a hull entry: the plane is its node's, reversed (SS 4.5).
constexpr std::uint32_t FLIPPED = 0x40000000u;

std::unexpected<Error> refuse(std::size_t node, const std::string& what) {
    return fail(ErrorCode::MalformedData, "the Model's node " + std::to_string(node) + " " + what);
}

bool within(std::int64_t index, std::size_t size) {
    return index >= 0 && static_cast<std::uint64_t>(index) < size;
}

/// The hull whose run starts at `at`, which `node` names -- SS 4.3. Each entry
/// up to the first -1 is a plane, and the six after it are the box's floats,
/// stored as their bits.
Result<ubundle::CollisionHull> decodeHull(const upkg::Model& model, std::size_t at, std::size_t node) {
    const std::vector<std::int32_t>& table = model.leafHulls;
    std::size_t end = at;
    while (end < table.size() && table[end] != -1) ++end;
    if (end == table.size())
        return refuse(node, "names a hull run at " + std::to_string(at) + " with no -1 to end it");
    if (table.size() - (end + 1) < 6)
        return refuse(node, "names a hull run at " + std::to_string(at)
                                + " with fewer than six entries after its -1");

    ubundle::CollisionHull hull;
    for (std::size_t k = at; k < end; ++k) {
        const auto entry = static_cast<std::uint32_t>(table[k]);
        const std::uint32_t index = entry & ~FLIPPED;
        if (index >= model.nodes.size())
            return refuse(node, "names a hull entry " + std::to_string(table[k])
                                    + " that, with bit 30 cleared, names no node of its "
                                    + std::to_string(model.nodes.size()));
        hull.planes.push_back(ubundle::HullPlane{index, (entry & FLIPPED) != 0});
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        hull.min[axis] = std::bit_cast<float>(table[end + 1 + axis]);
        hull.max[axis] = std::bit_cast<float>(table[end + 4 + axis]);
    }
    return hull;
}

} // namespace

Result<ubundle::CollisionTree> buildCollision(const upkg::Model& model) {
    if (model.rootOutside != 0 && model.rootOutside != 1)
        return fail(ErrorCode::MalformedData, "the Model's rootOutside is "
                                                  + std::to_string(model.rootOutside) + ", not 0 or 1");
    const std::size_t count = model.nodes.size();

    ubundle::CollisionTree tree;
    tree.outside = model.rootOutside == 1;
    tree.points.reserve(model.points.size());
    for (const upkg::Vector3& point : model.points) tree.points.push_back({point.x, point.y, point.z});

    // The hulls: one for each distinct iCollisionBound other than -1, in
    // ascending order of that value, each blamed on the first node naming it.
    std::map<std::int32_t, std::size_t> namedBy;
    for (std::size_t n = 0; n < count; ++n) {
        const std::int32_t bound = model.nodes[n].iCollisionBound;
        if (bound == -1) continue;
        if (!within(bound, model.leafHulls.size()))
            return refuse(n, "names iCollisionBound " + std::to_string(bound) + ", which is neither -1 nor within its "
                                 + std::to_string(model.leafHulls.size()) + " leafHulls");
        namedBy.try_emplace(bound, n);
    }
    std::map<std::int32_t, std::int32_t> hullAt;
    for (const auto& [offset, node] : namedBy) {
        UTA_TRY(ubundle::CollisionHull hull, decodeHull(model, static_cast<std::size_t>(offset), node));
        hullAt.emplace(offset, static_cast<std::int32_t>(tree.hulls.size()));
        tree.hulls.push_back(std::move(hull));
    }

    tree.nodes.reserve(count);
    for (std::size_t n = 0; n < count; ++n) {
        const upkg::BspNode& node = model.nodes[n];
        for (const auto& [name, link] : {std::pair<const char*, std::int32_t>{"iBack", node.iBack},
                                         {"iFront", node.iFront},
                                         {"iPlane", node.iPlane}})
            if (link != -1 && !within(link, count))
                return refuse(n, std::string("names ") + name + " " + std::to_string(link)
                                     + ", which is neither -1 nor one of its " + std::to_string(count)
                                     + " nodes");

        ubundle::CollisionNode out;
        out.normal = {node.plane.normal.x, node.plane.normal.y, node.plane.normal.z};
        out.distance = node.plane.w;
        out.back = node.iBack;
        out.front = node.iFront;
        out.coplanar = node.iPlane;
        out.nodeFlags = node.nodeFlags;
        out.hull = node.iCollisionBound == -1 ? -1 : hullAt.at(node.iCollisionBound);
        out.firstOutline = static_cast<std::uint32_t>(tree.outline.size());

        // A node of no vertices has no outline, and its iSurf is not read, as
        // buildGeometry does not read it. Every other refusal reaches a node
        // buildGeometry skips as invisible: it can still be solid.
        if (node.numVertices > 0) {
            if (node.numVertices < 3)
                return refuse(n, "has numVertices " + std::to_string(node.numVertices)
                                     + ", too few for an outline");
            if (!within(node.iSurf, model.surfs.size()))
                return refuse(n, "names iSurf " + std::to_string(node.iSurf) + ", past its "
                                     + std::to_string(model.surfs.size()) + " surfs");
            out.polyFlags = model.surfs[static_cast<std::size_t>(node.iSurf)].polyFlags;
            if (node.iVertPool < 0
                || static_cast<std::uint64_t>(node.iVertPool) + node.numVertices > model.verts.size())
                return refuse(n, "names vertex pool " + std::to_string(node.iVertPool) + ", whose "
                                     + std::to_string(node.numVertices) + " vertices run past its "
                                     + std::to_string(model.verts.size()) + " verts");
            for (std::size_t k = 0; k < node.numVertices; ++k) {
                const std::int32_t point =
                    model.verts[static_cast<std::size_t>(node.iVertPool) + k].pVertex;
                if (!within(point, model.points.size()))
                    return refuse(n, "names pVertex " + std::to_string(point) + ", past its "
                                         + std::to_string(model.points.size()) + " points");
                tree.outline.push_back(static_cast<std::uint32_t>(point));
            }
            out.outlineCount = node.numVertices;
        }
        tree.nodes.push_back(out);
    }

    // SS 4.2 item 2: a walk from node 0 over the three links reaches no node
    // twice. A node no walk reaches is kept, as UT99 keeps it.
    if (count > 0) {
        std::vector<bool> seen(count, false);
        std::vector<std::size_t> stack{0};
        while (!stack.empty()) {
            const std::size_t n = stack.back();
            stack.pop_back();
            if (seen[n]) return refuse(n, "is reached twice by a walk from node 0");
            seen[n] = true;
            const upkg::BspNode& node = model.nodes[n];
            for (const std::int32_t link : {node.iBack, node.iFront, node.iPlane})
                if (link != -1) stack.push_back(static_cast<std::size_t>(link));
        }
    }
    return tree;
}

Result<ubundle::MoverCollision> buildMoverCollision(const MoverSite& mover, const upkg::Model& model,
                                                    const ubundle::Placements& actors) {
    const ubundle::ActorPlacement& actor = actors.actors[mover.placement];
    UTA_TRY(const PivotSpace pivot, pivotSpaceOf(mover, actors));
    auto built = buildCollision(model);
    if (!built.has_value()) return std::unexpected(built.error().withContext("mover " + actor.path));

    ubundle::MoverCollision out;
    out.exportIndex = actor.exportIndex;
    out.tree = std::move(*built);

    // A point takes the function buildMover's vertices take.
    for (auto& point : out.tree.points) point = pivot.point(point);

    // A plane (n, d) becomes (n / S, d - n . P), both divided by the length of
    // n / S, so a point keeps its side under a mirror too. No link is swapped
    // and no outline reversed. A zero normal has no length to divide by and is
    // kept, as buildMover keeps one.
    for (ubundle::CollisionNode& node : out.tree.nodes) {
        std::array<double, 3> normal{};
        double shift = 0;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            normal[axis] = static_cast<double>(node.normal[axis]) / pivot.mainScale[axis];
            shift += static_cast<double>(node.normal[axis]) * static_cast<double>(pivot.prePivot[axis]);
        }
        const double distance = static_cast<double>(node.distance) - shift;
        const double length =
            std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
        const double by = length > 0 ? length : 1;
        for (std::size_t axis = 0; axis < 3; ++axis)
            node.normal[axis] = static_cast<float>(normal[axis] / by);
        node.distance = static_cast<float>(distance / by);
    }

    // A box takes its two corners, swapped on each axis where S is negative.
    for (ubundle::CollisionHull& hull : out.tree.hulls) {
        const std::array<float, 3> low = pivot.point(hull.min);
        const std::array<float, 3> high = pivot.point(hull.max);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const bool mirrored = pivot.mainScale[axis] < 0;
            hull.min[axis] = mirrored ? high[axis] : low[axis];
            hull.max[axis] = mirrored ? low[axis] : high[axis];
        }
    }
    return out;
}

} // namespace uta::ubake
