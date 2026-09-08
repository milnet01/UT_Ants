#include "umap/Build.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace uta::umap {
namespace {

/// The most samples one lattice may hold before the build refuses the Model.
///
/// A guard against a file this project did not write, not a tuning knob: the
/// spacing itself is `RoomBuildOptions::sampleSpacing` and SS 14 is where
/// tuning it is open. A box declaring 1e30 units would compute a per-axis
/// count no `std::size_t` holds, so the conversion is undefined before the
/// loop is even slow. At the default spacing this admits a level over 32000
/// units on every axis, which is far past anything the reference install
/// holds -- UNVERIFIED against that corpus, and the real-asset tier is what
/// would report a level that reached it.
constexpr std::size_t MAX_SAMPLES = std::size_t{1} << 30;

/// One lattice column's cell, in the grid `traceComponent` walks.
///
/// A cell is the square of side `sampleSpacing` CENTRED on its column's
/// sample. SS 4.4 step 4 says a cell is IN when a sample in that column
/// resolved to the room, and does not say where the cell's edges fall; the
/// two readings are the centred square and the square whose corner is the
/// sample, and they place every footprint half a cell apart. Centred is
/// chosen because it keeps the sample inside the cell it produced, so a room
/// of one sample is drawn around that sample rather than beside it.
struct Lattice {
    std::size_t nx = 0, ny = 0, nz = 0;
    float minX = 0, minY = 0, minZ = 0;
    float spacing = 0;

    [[nodiscard]] float cornerX(std::size_t vx) const {
        return minX + (static_cast<float>(vx) - 0.5F) * spacing;
    }
    [[nodiscard]] float cornerY(std::size_t vy) const {
        return minY + (static_cast<float>(vy) - 0.5F) * spacing;
    }
};

/// One room's sample bucket, projected to XY -- SS 4.4 steps 3 and 4.
struct Bucket {
    /// nx * ny, row-major by y. std::vector<bool> deliberately: this is a
    /// dense bit grid, which is the one shape that specialisation is right
    /// for, and a byte per cell would cost eight times the memory on every
    /// room of a large level at once.
    std::vector<bool> in;
    bool any = false;
    float minZ = 0, maxZ = 0;
};

/// The four axis directions, in counter-clockwise order: +x, +y, -x, -y.
/// `traceLoops` rotates through these by index, so the order is a contract.
constexpr int DIR_X[4] = {1, 0, -1, 0};
constexpr int DIR_Y[4] = {0, 1, 0, -1};

/// A grid corner, packed so it can key a flat table.
using Corner = std::size_t;

/// Twice the signed area of a closed ring. Positive is counter-clockwise.
[[nodiscard]] double signedArea2(const std::vector<Point2>& ring) {
    double sum = 0;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Point2& a = ring[i];
        const Point2& b = ring[(i + 1) % ring.size()];
        sum += static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
    }
    return sum;
}

/// Perpendicular distance from `p` to the segment `a`-`b`.
[[nodiscard]] float perpDistance(Point2 p, Point2 a, Point2 b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float len2 = dx * dx + dy * dy;
    if (len2 <= 0.0F) {
        const float ax = p.x - a.x;
        const float ay = p.y - a.y;
        return std::sqrt(ax * ax + ay * ay);
    }
    const float cross = dx * (p.y - a.y) - dy * (p.x - a.x);
    return std::abs(cross) / std::sqrt(len2);
}

/// Ramer-Douglas-Peucker over the polyline `pts[first..last]`, marking what
/// survives in `keep`. Iterative rather than recursive: a ring traced from a
/// large level's lattice can be tens of thousands of vertices long, and the
/// recursive form is O(n) stack deep on a staircase.
void simplifyRun(const std::vector<Point2>& pts, std::size_t first, std::size_t last,
                 float tolerance, std::vector<bool>& keep) {
    std::vector<std::pair<std::size_t, std::size_t>> pending;
    pending.emplace_back(first, last);
    while (!pending.empty()) {
        const auto [lo, hi] = pending.back();
        pending.pop_back();
        if (hi <= lo + 1) continue;

        float worst = 0;
        std::size_t at = lo;
        for (std::size_t i = lo + 1; i < hi; ++i) {
            const float d = perpDistance(pts[i], pts[lo], pts[hi]);
            if (d > worst) {
                worst = d;
                at = i;
            }
        }
        if (worst <= tolerance) continue;
        keep[at] = true;
        pending.emplace_back(lo, at);
        pending.emplace_back(at, hi);
    }
}

/// The three most extreme vertices of `ring`, in the ring's own cyclic order.
///
/// SS 4.4 step 6's floor: the simplifier never reduces a ring below three
/// vertices. Without it INV-6 would turn on an implementer's `>` versus `>=`
/// at a one-cell room.
[[nodiscard]] std::vector<Point2> threeExtremes(const std::vector<Point2>& ring) {
    // Lowest (x, y) is the anchor, then the vertex furthest from it, then the
    // vertex furthest from the line between those two. Deterministic, and it
    // cannot pick three collinear vertices unless the whole ring is one line.
    std::size_t a = 0;
    for (std::size_t i = 1; i < ring.size(); ++i) {
        if (ring[i].x < ring[a].x || (ring[i].x == ring[a].x && ring[i].y < ring[a].y)) a = i;
    }
    std::size_t b = a;
    float best = -1;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const float dx = ring[i].x - ring[a].x;
        const float dy = ring[i].y - ring[a].y;
        const float d = dx * dx + dy * dy;
        if (d > best) {
            best = d;
            b = i;
        }
    }
    std::size_t c = a;
    best = -1;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const float d = perpDistance(ring[i], ring[a], ring[b]);
        if (d > best) {
            best = d;
            c = i;
        }
    }

    std::vector<std::size_t> picked{a, b, c};
    std::sort(picked.begin(), picked.end());
    picked.erase(std::unique(picked.begin(), picked.end()), picked.end());
    std::vector<Point2> out;
    out.reserve(picked.size());
    for (std::size_t i : picked) out.push_back(ring[i]);
    return out;
}

/// Drop the vertices SS 4.4 step 6 allows dropping, and no more.
///
/// Two stages, and the first is exact. Collinear midpoints come from the
/// lattice -- a straight wall traced cell by cell is one vertex per cell --
/// and removing them moves nothing at all. Only then does RDP run, on the
/// staircase that is left.
[[nodiscard]] std::vector<Point2> simplifyRing(const std::vector<Point2>& ring, float tolerance) {
    if (ring.size() < 3) return ring;

    std::vector<Point2> straightened;
    straightened.reserve(ring.size());
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Point2& prev = ring[(i + ring.size() - 1) % ring.size()];
        const Point2& here = ring[i];
        const Point2& next = ring[(i + 1) % ring.size()];
        const float cross = (here.x - prev.x) * (next.y - prev.y) //
                            - (here.y - prev.y) * (next.x - prev.x);
        if (cross != 0.0F) straightened.push_back(here);
    }
    if (straightened.size() < 3) return threeExtremes(ring);

    // A closed ring has no ends for RDP to anchor on, so it is cut into two
    // polylines at the two vertices furthest apart and each is simplified.
    // Cutting at one point instead would let the simplifier delete the cut
    // itself and reshape the ring around it.
    std::size_t far = 0;
    float best = -1;
    for (std::size_t i = 1; i < straightened.size(); ++i) {
        const float dx = straightened[i].x - straightened[0].x;
        const float dy = straightened[i].y - straightened[0].y;
        const float d = dx * dx + dy * dy;
        if (d > best) {
            best = d;
            far = i;
        }
    }

    std::vector<bool> keep(straightened.size(), false);
    keep[0] = true;
    keep[far] = true;
    simplifyRun(straightened, 0, far, tolerance, keep);

    // The second polyline runs from `far` back round to 0, so it is walked in
    // a rotated copy and the marks are folded back onto the original indices.
    const std::size_t tailSize = straightened.size() - far + 1;
    std::vector<Point2> tail;
    tail.reserve(tailSize);
    for (std::size_t i = 0; i < tailSize; ++i) {
        tail.push_back(straightened[(far + i) % straightened.size()]);
    }
    std::vector<bool> tailKeep(tail.size(), false);
    tailKeep.front() = true;
    tailKeep.back() = true;
    simplifyRun(tail, 0, tail.size() - 1, tolerance, tailKeep);
    for (std::size_t i = 0; i < tail.size(); ++i) {
        if (tailKeep[i]) keep[(far + i) % straightened.size()] = true;
    }

    std::vector<Point2> out;
    out.reserve(straightened.size());
    for (std::size_t i = 0; i < straightened.size(); ++i) {
        if (keep[i]) out.push_back(straightened[i]);
    }
    if (out.size() < 3) return threeExtremes(straightened);
    return out;
}

/// Trace the closed rings bounding one connected component -- SS 4.4 step 5.
///
/// Marching squares in its edge-cancellation form: every cell contributes its
/// four edges counter-clockwise, and an edge shared with another cell of the
/// same component cancels against that cell's opposite-facing copy. What
/// survives is the boundary of the union, which is what marching squares over
/// this set yields, without the corner-lattice case table.
///
/// A corner where the component touches itself diagonally has two ways out.
/// The rule is to take the first outgoing edge COUNTER-clockwise from the way
/// we came, which walks through such a corner rather than closing at it. That
/// keeps one component to one outer ring, which is the shape SS 4.4 step 5
/// asks for; taking the clockwise edge instead would split the component's
/// boundary into two rings and leave nothing to call the outer one.
[[nodiscard]] std::vector<std::vector<Point2>> traceComponent(
    const Lattice& lattice, const std::vector<std::size_t>& label, std::size_t which) {
    const std::size_t stride = lattice.nx + 1;
    const std::size_t corners = stride * (lattice.ny + 1);

    // edgeTo[corner * 4 + d] is set where an edge leaves `corner` in
    // direction d. A corner has at most one edge per direction, because two
    // cells sharing that edge cancel it.
    std::vector<bool> edgeTo(corners * 4, false);

    const auto inComponent = [&](std::size_t ix, std::size_t iy) {
        return label[iy * lattice.nx + ix] == which;
    };

    for (std::size_t iy = 0; iy < lattice.ny; ++iy) {
        for (std::size_t ix = 0; ix < lattice.nx; ++ix) {
            if (!inComponent(ix, iy)) continue;
            const std::size_t lo = iy * stride + ix;         // (ix,   iy)
            const std::size_t loRight = lo + 1;              // (ix+1, iy)
            const std::size_t hi = (iy + 1) * stride + ix;   // (ix,   iy+1)
            const std::size_t hiRight = hi + 1;              // (ix+1, iy+1)

            if (iy == 0 || !inComponent(ix, iy - 1)) edgeTo[lo * 4 + 0] = true;
            if (ix + 1 == lattice.nx || !inComponent(ix + 1, iy)) edgeTo[loRight * 4 + 1] = true;
            if (iy + 1 == lattice.ny || !inComponent(ix, iy + 1)) edgeTo[hiRight * 4 + 2] = true;
            if (ix == 0 || !inComponent(ix - 1, iy)) edgeTo[hi * 4 + 3] = true;
        }
    }

    const auto step = [&](Corner from, int dir) {
        const std::size_t vx = from % stride;
        const std::size_t vy = from / stride;
        return (vy + static_cast<std::size_t>(DIR_Y[dir])) * stride //
               + (vx + static_cast<std::size_t>(DIR_X[dir]));
    };

    std::vector<std::vector<Point2>> rings;
    for (std::size_t start = 0; start < corners; ++start) {
        for (int firstDir = 0; firstDir < 4; ++firstDir) {
            if (!edgeTo[start * 4 + firstDir]) continue;

            std::vector<Point2> ring;
            Corner at = start;
            int dir = firstDir;
            while (true) {
                edgeTo[at * 4 + dir] = false;
                ring.push_back(Point2{lattice.cornerX(at % stride), //
                                      lattice.cornerY(at / stride)});
                at = step(at, dir);
                if (at == start) break;

                // Counter-clockwise from the way we came: the reverse of the
                // incoming direction, then one quarter turn at a time.
                const int back = (dir + 2) % 4;
                int next = -1;
                for (int turn = 1; turn <= 4; ++turn) {
                    const int candidate = (back + turn) % 4;
                    if (edgeTo[at * 4 + candidate]) {
                        next = candidate;
                        break;
                    }
                }
                if (next < 0) break; // Unreachable: every corner's edges pair up.
                dir = next;
            }
            if (ring.size() >= 3) rings.push_back(std::move(ring));
        }
    }
    return rings;
}

/// One room's footprints, from its projected bucket -- SS 4.4 steps 4 to 6.
[[nodiscard]] std::vector<Footprint> traceBucket(const Lattice& lattice, const Bucket& bucket,
                                                 float tolerance) {
    constexpr std::size_t UNLABELLED = static_cast<std::size_t>(-1);
    std::vector<std::size_t> label(lattice.nx * lattice.ny, UNLABELLED);
    std::vector<Footprint> parts;

    // Four-connected components. Diagonally touching cells are separate
    // rooms-in-one-zone rather than one pinched region, which is what SS 4.4
    // step 5's "two pools of one water zone" describes.
    std::vector<std::size_t> stack;
    std::size_t next = 0;
    for (std::size_t seed = 0; seed < label.size(); ++seed) {
        if (!bucket.in[seed] || label[seed] != UNLABELLED) continue;
        const std::size_t which = next++;
        label[seed] = which;
        stack.push_back(seed);
        while (!stack.empty()) {
            const std::size_t cell = stack.back();
            stack.pop_back();
            const std::size_t ix = cell % lattice.nx;
            const std::size_t iy = cell / lattice.nx;
            const auto visit = [&](std::size_t nxi, std::size_t nyi) {
                const std::size_t neighbour = nyi * lattice.nx + nxi;
                if (!bucket.in[neighbour] || label[neighbour] != UNLABELLED) return;
                label[neighbour] = which;
                stack.push_back(neighbour);
            };
            if (ix > 0) visit(ix - 1, iy);
            if (ix + 1 < lattice.nx) visit(ix + 1, iy);
            if (iy > 0) visit(ix, iy - 1);
            if (iy + 1 < lattice.ny) visit(ix, iy + 1);
        }
    }

    for (std::size_t which = 0; which < next; ++which) {
        std::vector<std::vector<Point2>> rings = traceComponent(lattice, label, which);
        if (rings.empty()) continue;

        // The outer ring is the counter-clockwise one, and a four-connected
        // component has exactly one. Taking the largest positive area rather
        // than asserting that leaves the ring set usable if a level ever
        // produces a shape this reasoning did not foresee: every other ring
        // becomes a hole, which is what SS 4.4 step 5 calls an enclosed ring.
        std::size_t outer = 0;
        double bestArea = -1;
        for (std::size_t i = 0; i < rings.size(); ++i) {
            const double area = signedArea2(rings[i]);
            if (area > bestArea) {
                bestArea = area;
                outer = i;
            }
        }

        Footprint part;
        part.outer = simplifyRing(rings[outer], tolerance);
        for (std::size_t i = 0; i < rings.size(); ++i) {
            if (i == outer) continue;
            part.holes.push_back(simplifyRing(rings[i], tolerance));
        }
        parts.push_back(std::move(part));
    }
    return parts;
}

/// The lattice SS 4.4 step 1 samples, or an empty one where there is no box.
///
/// `Model::boundsValid` is the file's own flag and it can be false. SS 4.4
/// samples between boundsMin and boundsMax without mentioning it; the tail
/// filed on UTA-0007's roadmap bullet left the precondition to be settled
/// here. An invalid box is not a measurement, so it yields no samples --
/// which is the same state SS 4.4 step 3 already defines for a room whose
/// bucket is empty, rather than a fourth behaviour nothing else handles.
[[nodiscard]] Result<Lattice> latticeFor(const uta::upkg::Model& model, float spacing) {
    Lattice lattice;
    lattice.spacing = spacing;
    if (!model.boundsValid) return lattice;

    const float lo[3] = {model.boundsMin.x, model.boundsMin.y, model.boundsMin.z};
    const float hi[3] = {model.boundsMax.x, model.boundsMax.y, model.boundsMax.z};
    std::size_t count[3] = {0, 0, 0};
    double total = 1;
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(lo[axis]) || !std::isfinite(hi[axis]) || hi[axis] < lo[axis]) {
            return lattice; // Degenerate on any axis: no box, so no samples.
        }
        const double span = static_cast<double>(hi[axis]) - lo[axis];
        const double steps = std::floor(span / spacing) + 1;
        // Checked BEFORE the cast, and in double, because that is the order
        // that makes it a guard: a span of 1e30 gives a count no std::size_t
        // holds, so testing the converted value would test a number the
        // conversion had already made undefined.
        total *= steps;
        if (total > static_cast<double>(MAX_SAMPLES)) {
            return fail(ErrorCode::MalformedData,
                        "a level bounding box needs " + std::to_string(total) +
                            " samples at a spacing of " + std::to_string(spacing));
        }
        count[static_cast<std::size_t>(axis)] = static_cast<std::size_t>(steps);
    }

    lattice.nx = count[0];
    lattice.ny = count[1];
    lattice.nz = count[2];
    lattice.minX = lo[0];
    lattice.minY = lo[1];
    lattice.minZ = lo[2];
    return lattice;
}

/// Cluster the sampled rooms' midpoints into floor bands -- SS 4.5.
void assignFloors(RoomMap& map, const std::vector<Bucket>& buckets, float separation) {
    std::vector<float> midpoints;
    for (std::size_t i = 0; i < map.rooms.size(); ++i) {
        if (buckets[i].any) midpoints.push_back((map.rooms[i].minZ + map.rooms[i].maxZ) * 0.5F);
    }
    std::sort(midpoints.begin(), midpoints.end());

    // Single linkage: a gap wider than `separation` between consecutive
    // midpoints starts a new cluster. bands[i] is the LOWEST midpoint in
    // cluster i, which SS 4.5 step 3 states because "the lower edge" also
    // reads as the halfway point of the gap beneath, and the two place a room
    // differently at every boundary.
    for (std::size_t i = 0; i < midpoints.size(); ++i) {
        if (i == 0 || midpoints[i] - midpoints[i - 1] > separation) {
            map.bands.push_back(midpoints[i]);
        }
    }

    // INV-8 requires every room to sit on a band, and SS 4.5 step 5 puts an
    // unsampled room on band 0 -- which has to exist for that to mean
    // anything. A level whose rooms all went unsampled reaches here with no
    // midpoints and no bands, so one is opened. A level with no rooms at all
    // keeps none: SS 6 calls that "no rooms and no bands".
    if (map.bands.empty() && !map.rooms.empty()) map.bands.push_back(0.0F);

    for (std::size_t i = 0; i < map.rooms.size(); ++i) {
        Room& room = map.rooms[i];
        if (!buckets[i].any) {
            room.floors.push_back(0);
            continue;
        }
        // Band i covers [bands[i], bands[i+1]); the topmost is unbounded
        // above. A room joins EVERY band its own extent overlaps, so a
        // stairwell appears on each floor it connects rather than vanishing
        // between them.
        for (std::size_t band = 0; band < map.bands.size(); ++band) {
            const bool aboveFloor = room.maxZ >= map.bands[band];
            const bool belowCeiling =
                band + 1 == map.bands.size() || room.minZ < map.bands[band + 1];
            if (aboveFloor && belowCeiling) room.floors.push_back(static_cast<std::uint16_t>(band));
        }
    }
}

} // namespace

Result<RoomBuildResult> buildRoomMap(const uta::upkg::Model& model,
                                     const RoomBuildOptions& options) {
    if (!std::isfinite(options.sampleSpacing) || options.sampleSpacing <= 0.0F) {
        return fail(ErrorCode::InvalidArgument,
                    "sampleSpacing must be a positive finite number, not " +
                        std::to_string(options.sampleSpacing));
    }
    if (model.zones.size() > ZONE_CEILING) {
        return fail(ErrorCode::MalformedData,
                    "a level declares " + std::to_string(model.zones.size()) +
                        " zones, past the engine's ceiling of " + std::to_string(ZONE_CEILING));
    }

    RoomBuildResult result;
    RoomMap& map = result.map;

    // The descent tables, copied narrow -- SS 4.6. The copy is the point: the
    // alternative is the runtime holding a upkg::Model, which design rule 2
    // forbids.
    map.nodes.reserve(model.nodes.size());
    for (const uta::upkg::BspNode& source : model.nodes) {
        RoomMap::Node node;
        node.normal = Point3{source.plane.normal.x, source.plane.normal.y, source.plane.normal.z};
        node.w = source.plane.w;
        node.iFront = source.iFront;
        node.iBack = source.iBack;
        node.iLeaf[0] = source.iLeaf[0];
        node.iLeaf[1] = source.iLeaf[1];
        node.iZone[0] = source.iZone[0];
        node.iZone[1] = source.iZone[1];
        map.nodes.push_back(node);
    }

    // A zone gets a room only where at least one LEAF names it -- SS 4.2,
    // INV-1. Walking the zone table instead emits a room for every declared
    // zone, including ones no point can resolve to: on the 26 single-room
    // maps of that census, up to 63 phantom rooms each serialised, drawn, and
    // tracked for exploration.
    std::vector<bool> named(model.zones.size(), false);
    map.leafZone.reserve(model.leaves.size());
    for (const uta::upkg::Leaf& leaf : model.leaves) {
        const bool inTable = leaf.iZone >= 0 && //
                             static_cast<std::size_t>(leaf.iZone) < model.zones.size();
        if (!inTable) {
            map.leafZone.push_back(ZONE_REFUSED);
            result.report.refusedZones.push_back(static_cast<std::uint32_t>(leaf.iZone));
            continue;
        }
        map.leafZone.push_back(static_cast<std::uint8_t>(leaf.iZone));
        named[static_cast<std::size_t>(leaf.iZone)] = true;
    }
    std::sort(result.report.refusedZones.begin(), result.report.refusedZones.end());
    result.report.refusedZones.erase(
        std::unique(result.report.refusedZones.begin(), result.report.refusedZones.end()),
        result.report.refusedZones.end());

    // Zone 0 is the engine's null zone and is never a room -- SS 4.2 measured
    // no leaf naming it across the reference install.
    map.roomForZone.assign(model.zones.size(), NO_ROOM);
    for (std::size_t zone = 1; zone < named.size(); ++zone) {
        if (!named[zone]) continue;
        map.roomForZone[zone] = static_cast<std::uint32_t>(map.rooms.size());
        Room room;
        room.zoneIndex = static_cast<std::uint32_t>(zone);
        map.rooms.push_back(std::move(room));
    }

    UTA_TRY(const Lattice lattice, latticeFor(model, options.sampleSpacing));

    // ONE pass over the level box, bucketed by room -- SS 4.4 steps 1 to 3.
    // It reuses the lookup of SS 4.3 as its primitive, so the footprint cannot
    // disagree with the lookup about which room a spot belongs to.
    std::vector<Bucket> buckets(map.rooms.size());
    const std::size_t cells = lattice.nx * lattice.ny;
    for (Bucket& bucket : buckets) bucket.in.assign(cells, false);

    for (std::size_t iy = 0; iy < lattice.ny; ++iy) {
        for (std::size_t ix = 0; ix < lattice.nx; ++ix) {
            const std::size_t cell = iy * lattice.nx + ix;
            for (std::size_t iz = 0; iz < lattice.nz; ++iz) {
                const Point3 point{lattice.minX + static_cast<float>(ix) * lattice.spacing,
                                   lattice.minY + static_cast<float>(iy) * lattice.spacing,
                                   lattice.minZ + static_cast<float>(iz) * lattice.spacing};
                const std::uint32_t room = roomAt(map, point);
                if (room == NO_ROOM) continue;

                Bucket& bucket = buckets[room];
                bucket.in[cell] = true;
                if (!bucket.any) {
                    bucket.any = true;
                    bucket.minZ = point.z;
                    bucket.maxZ = point.z;
                } else {
                    bucket.minZ = std::min(bucket.minZ, point.z);
                    bucket.maxZ = std::max(bucket.maxZ, point.z);
                }
            }
        }
    }

    for (std::size_t i = 0; i < map.rooms.size(); ++i) {
        Room& room = map.rooms[i];
        if (!buckets[i].any) {
            // minZ and maxZ keep their struct defaults, which are not a
            // measurement -- SS 4.4 step 3. assignFloors excludes the room
            // from clustering for exactly that reason.
            result.report.roomsWithoutFootprint.push_back(room.zoneIndex);
            continue;
        }
        room.minZ = buckets[i].minZ;
        room.maxZ = buckets[i].maxZ;
        room.parts = traceBucket(lattice, buckets[i], options.simplifyTolerance);
    }

    assignFloors(map, buckets, options.floorSeparation);
    return result;
}

} // namespace uta::umap
