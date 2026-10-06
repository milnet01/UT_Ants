// Render bounds that miss the geometry they guard -- UTA-0328.

#include "Scan.h"

#include <algorithm>
#include <array>

namespace uta::boundscan {

namespace {

/// An axis-aligned box that may hold nothing yet.
struct Extent {
    std::array<double, 3> lo{}, hi{};
    bool any = false;

    void add(const std::array<double, 3>& point) {
        if (!any) {
            lo = hi = point;
            any = true;
            return;
        }
        for (std::size_t c = 0; c < 3; ++c) {
            lo[c] = std::min(lo[c], point[c]);
            hi[c] = std::max(hi[c], point[c]);
        }
    }
    void add(const Extent& other) {
        if (!other.any) return;
        add(other.lo);
        add(other.hi);
    }
};

bool inTable(std::int32_t index, std::size_t size) {
    return index >= 0 && static_cast<std::size_t>(index) < size;
}

} // namespace

Scan scanModel(const upkg::Model& model, double tolerance) {
    const std::size_t count = model.nodes.size();
    Scan scan;
    scan.nodes = count;

    // Each node's own drawn polygon.
    std::vector<Extent> own(count);
    for (std::size_t i = 0; i < count; ++i) {
        const upkg::BspNode& node = model.nodes[i];
        if (node.numVertices < 3 || !inTable(node.iSurf, model.surfs.size()) ||
            (model.surfs[static_cast<std::size_t>(node.iSurf)].polyFlags & PF_INVISIBLE) != 0)
            continue;
        for (int k = 0; k < node.numVertices; ++k) {
            const std::int32_t vert = node.iVertPool + k;
            if (!inTable(vert, model.verts.size())) continue;
            const std::int32_t point = model.verts[static_cast<std::size_t>(vert)].pVertex;
            if (!inTable(point, model.points.size())) continue;
            const upkg::Vector3& p = model.points[static_cast<std::size_t>(point)];
            own[i].add(std::array<double, 3>{p.x, p.y, p.z});
        }
    }

    // Each node's whole subtree -- own, coplanars, front, back -- in post-order
    // from node 0, iteratively so a deep tree cannot overflow the stack. A
    // node reached twice is counted once, and a link back up the tree is cut.
    enum : char { Unseen, Open, Done };
    std::vector<char> state(count, Unseen);
    std::vector<Extent> whole(count);
    std::vector<std::size_t> stack;
    if (count > 0) stack.push_back(0);
    while (!stack.empty()) {
        const std::size_t i = stack.back();
        const upkg::BspNode& node = model.nodes[i];
        const std::array<std::int32_t, 3> children{node.iFront, node.iBack, node.iPlane};
        if (state[i] == Unseen) {
            state[i] = Open;
            for (const std::int32_t child : children)
                if (inTable(child, count) && state[static_cast<std::size_t>(child)] == Unseen)
                    stack.push_back(static_cast<std::size_t>(child));
            continue;
        }
        stack.pop_back();
        if (state[i] == Done) continue;
        state[i] = Done;
        whole[i] = own[i];
        for (const std::int32_t child : children)
            if (inTable(child, count) && state[static_cast<std::size_t>(child)] == Done)
                whole[i].add(whole[static_cast<std::size_t>(child)]);
    }

    for (std::size_t i = 0; i < count; ++i) {
        const upkg::BspNode& node = model.nodes[i];
        if (!inTable(node.iRenderBound, model.bounds.size()) || !whole[i].any) continue;
        const upkg::Box& box = model.bounds[static_cast<std::size_t>(node.iRenderBound)];
        Finding finding{.kind = Kind::Miss, .node = i, .bound = node.iRenderBound, .valid = box.valid};
        if (box.min.x > box.max.x || box.min.y > box.max.y || box.min.z > box.max.z) {
            finding.kind = Kind::Inverted;
            ++scan.inverted;
            scan.findings.push_back(finding);
            continue;
        }
        if (!box.valid) {
            finding.kind = Kind::Invalid;
            ++scan.invalid;
            scan.findings.push_back(finding);
            continue;
        }
        ++scan.checked;
        const std::array<double, 3> lo{box.min.x, box.min.y, box.min.z};
        const std::array<double, 3> hi{box.max.x, box.max.y, box.max.z};
        double excess = 0;
        for (std::size_t c = 0; c < 3; ++c)
            excess = std::max({excess, lo[c] - whole[i].lo[c], whole[i].hi[c] - hi[c]});
        if (excess <= tolerance) continue;
        finding.excess = excess;
        ++scan.misses;
        scan.worst = std::max(scan.worst, excess);
        scan.findings.push_back(finding);
    }
    return scan;
}

} // namespace uta::boundscan
