// The level's drawable surfaces as triangles --
// docs/specs/UTA-0109-map-geometry.md SS 4.3.

#include "ubake/Geometry.h"

#include "ubake/Sway.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace uta::ubake {
namespace {

using Vec = std::array<double, 3>;

Vec toDouble(const upkg::Vector3& v) {
    return {v.x, v.y, v.z};
}

Vec minus(const Vec& a, const Vec& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

double dot(const Vec& a, const Vec& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec cross(const Vec& a, const Vec& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

/// Vertices and indices are counted by a u32 -- SS 4.3.
constexpr std::uint64_t COUNT_LIMIT = std::uint64_t{1} << 32;

std::unexpected<Error> refuse(std::size_t node, std::string_view what, std::int64_t value,
                              std::size_t size, std::string_view table) {
    return fail(ErrorCode::MalformedData,
                "the Model's node " + std::to_string(node) + " names " + std::string(what) + " "
                    + std::to_string(value) + ", past its " + std::to_string(size) + " "
                    + std::string(table));
}

constexpr std::uint32_t PF_PORTAL = 0x04000000u;

/// A node's polygon, or none where an index it needs leaves its table -- that
/// node is left to buildGeometry's loop, which refuses it.
std::vector<Vec> polygonOf(const upkg::Model& model, const upkg::BspNode& node) {
    const std::size_t count = node.numVertices;
    if (count < 3 || node.iVertPool < 0
        || static_cast<std::uint64_t>(node.iVertPool) + count > model.verts.size())
        return {};
    std::vector<Vec> points;
    points.reserve(count);
    for (std::size_t k = 0; k < count; ++k) {
        const std::int32_t point = model.verts[static_cast<std::size_t>(node.iVertPool) + k].pVertex;
        if (!within(point, model.points.size())) return {};
        points.push_back(toDouble(model.points[static_cast<std::size_t>(point)]));
    }
    return points;
}

using Flat = std::vector<std::array<double, 2>>;

/// A polygon seen along `axis`, which is dropped.
Flat flatten(const std::vector<Vec>& points, std::size_t axis) {
    Flat flat;
    flat.reserve(points.size());
    for (const Vec& p : points) flat.push_back({p[axis == 0 ? 1 : 0], p[axis == 2 ? 1 : 2]});
    return flat;
}

double signedArea(const Flat& polygon) {
    double twice = 0;
    for (std::size_t k = 0; k < polygon.size(); ++k) {
        const auto& a = polygon[k];
        const auto& b = polygon[(k + 1) % polygon.size()];
        twice += a[0] * b[1] - b[0] * a[1];
    }
    return twice / 2;
}

/// The part of convex `subject` on the left of the line a to b, or on its
/// right when `left` is false (one Sutherland-Hodgman step).
Flat halfOf(const Flat& subject, const std::array<double, 2>& a, const std::array<double, 2>& b, bool left) {
    const auto side = [&](const std::array<double, 2>& p) {
        const double s = (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]);
        return left ? s : -s;
    };
    Flat kept;
    for (std::size_t j = 0; j < subject.size(); ++j) {
        const auto& p = subject[j];
        const auto& q = subject[(j + 1) % subject.size()];
        const double sp = side(p);
        const double sq = side(q);
        if (sp >= 0) kept.push_back(p);
        if ((sp >= 0) != (sq >= 0)) {
            const double t = sp / (sp - sq);
            kept.push_back({p[0] + t * (q[0] - p[0]), p[1] + t * (q[1] - p[1])});
        }
    }
    return kept;
}

/// The area of convex `subject` that no polygon of `covers` (each convex)
/// reaches. Each cover splits every piece still uncovered into its parts
/// outside the cover's edges, so an area two covers share is taken once.
double uncoveredArea(const Flat& subject, std::vector<Flat> covers) {
    std::vector<Flat> pieces{subject};
    for (Flat& clip : covers) {
        if (signedArea(clip) < 0) std::ranges::reverse(clip);
        std::vector<Flat> outside;
        for (Flat piece : pieces) {
            for (std::size_t k = 0; k < clip.size() && piece.size() >= 3; ++k) {
                const auto& a = clip[k];
                const auto& b = clip[(k + 1) % clip.size()];
                Flat out = halfOf(piece, a, b, false);
                if (out.size() >= 3) outside.push_back(std::move(out));
                piece = halfOf(piece, a, b, true);
            }
        }
        pieces = std::move(outside);
    }
    double area = 0;
    for (const Flat& piece : pieces) area += std::abs(signedArea(piece));
    return area;
}

/// UTA-0310 SS 4.3 step 3a: the nodes UT99 does not draw because invisible
/// zone portals in their coplanar chain cover them, with an open zone on both
/// sides. Measured in the original client, not read from its source -- the
/// cases are tests/unit/BakeGeometryTest.cpp's.
std::vector<bool> hiddenBehindPortals(const upkg::Model& model) {
    const std::size_t count = model.nodes.size();
    std::vector<bool> hidden(count, false);

    // The coplanar chains, joined through iPlane. The smaller index is kept
    // as a chain's root, so the grouping does not depend on visiting order.
    std::vector<std::size_t> root(count);
    for (std::size_t n = 0; n < count; ++n) root[n] = n;
    const auto find = [&](std::size_t n) {
        while (root[n] != n) n = root[n] = root[root[n]];
        return n;
    };
    for (std::size_t n = 0; n < count; ++n) {
        if (!within(model.nodes[n].iPlane, count)) continue;
        const std::size_t a = find(n);
        const std::size_t b = find(static_cast<std::size_t>(model.nodes[n].iPlane));
        root[std::max(a, b)] = std::min(a, b);
    }

    struct Portal {
        std::vector<Vec> points;
        Vec normal;
        std::size_t axis = 0;
    };
    std::map<std::size_t, std::vector<Portal>> portals; // by chain root
    for (std::size_t n = 0; n < count; ++n) {
        const upkg::BspNode& node = model.nodes[n];
        if (!within(node.iSurf, model.surfs.size())) continue;
        const upkg::BspSurf& surf = model.surfs[static_cast<std::size_t>(node.iSurf)];
        if ((surf.polyFlags & (PF_PORTAL | PF_INVISIBLE)) != (PF_PORTAL | PF_INVISIBLE)) continue;
        if (!within(surf.vNormal, model.vectors.size())) continue;
        std::vector<Vec> points = polygonOf(model, node);
        Vec normal = toDouble(model.vectors[static_cast<std::size_t>(surf.vNormal)]);
        const double length = std::sqrt(dot(normal, normal));
        if (points.empty() || !(length > 0)) continue;
        for (double& part : normal) part /= length;
        std::size_t axis = 0;
        for (std::size_t k = 1; k < 3; ++k)
            if (std::abs(normal[k]) > std::abs(normal[axis])) axis = k;
        portals[find(n)].push_back(Portal{std::move(points), normal, axis});
    }
    if (portals.empty()) return hidden;

    for (std::size_t n = 0; n < count; ++n) {
        const upkg::BspNode& node = model.nodes[n];
        if (node.iZone[0] == 0 || node.iZone[1] == 0) continue; // rock behind it
        if (!within(node.iSurf, model.surfs.size())) continue;
        if ((model.surfs[static_cast<std::size_t>(node.iSurf)].polyFlags & (PF_PORTAL | PF_INVISIBLE)) != 0)
            continue;
        const auto chain = portals.find(find(n));
        if (chain == portals.end()) continue;
        const std::vector<Vec> points = polygonOf(model, node);
        if (points.empty()) continue;

        // The portals whose plane it lies on, to within a unit, seen along
        // the first one's axis: coplanar, so they share it.
        std::optional<std::size_t> axis;
        std::vector<Flat> covers;
        for (const Portal& portal : chain->second) {
            const bool onPlane = std::ranges::all_of(points, [&](const Vec& p) {
                return std::abs(dot(minus(p, portal.points[0]), portal.normal)) <= 1;
            });
            if (!onPlane) continue;
            if (!axis.has_value()) axis = portal.axis;
            covers.push_back(flatten(portal.points, *axis));
        }
        if (!axis.has_value()) continue;
        const Flat flat = flatten(points, *axis);
        const double own = std::abs(signedArea(flat));
        // UTA-0364: the area covered once, however many portals overlap on it.
        const double covered = own - uncoveredArea(flat, std::move(covers));
        // NaN compares false, so a node with a non-finite point is drawn and refused.
        if (own > 0 && covered >= 0.99 * own) hidden[n] = true;
    }
    return hidden;
}

/// One drawn node, before it is batched.
/// How a point of one node's surface becomes a vertex.
struct Mapping {
    Vec base{};
    Vec textureU{};
    Vec textureV{};
    double panU = 0;
    double panV = 0;
    const SurfaceMaterial* made = nullptr;
    std::array<float, 3> normal{};
    std::uint8_t zone = 0;

    [[nodiscard]] ubundle::GeometryVertex vertexAt(const Vec& point) const {
        ubundle::GeometryVertex vertex;
        vertex.zone = zone;
        vertex.position = {static_cast<float>(point[0]), static_cast<float>(point[1]), static_cast<float>(point[2])};
        vertex.normal = normal;
        if (made != nullptr) {
            const Vec offset = minus(point, base);
            vertex.u = static_cast<float>((dot(offset, textureU) + panU) / made->uSize);
            vertex.v = static_cast<float>((dot(offset, textureV) + panV) / made->vSize);
        }
        return vertex;
    }
};

struct Drawn {
    std::string material;
    std::uint32_t polyFlags = 0;
    std::array<float, 2> panRate{}; ///< UTA-0269
    std::vector<ubundle::GeometryVertex> corners;
    /// UTA-0055 SS 4.2: where it sways, its pieces, each a fan drawn in place
    /// of `corners`.
    std::vector<std::vector<ubundle::GeometryVertex>> pieces;
    Mapping mapping;
    std::size_t sway = SIZE_MAX; ///< its index among the polygons sliceSway is given
};

} // namespace

Result<ubundle::Geometry> buildGeometry(const upkg::Model& model, const MaterialLookup& materials,
                                        std::size_t zoneCount, std::span<const std::uint32_t> omitted,
                                        SwayReport* sway, const umap::RoomMap* rooms,
                                        std::span<const Cover> covers) {
    std::vector<Drawn> drawn;
    std::vector<SwayPolygon> swaying;
    std::uint64_t vertexTotal = 0;
    std::uint64_t indexTotal = 0;
    const std::vector<bool> hidden = hiddenBehindPortals(model);

    // Steps 4 to 7a for one polygon, `points`, drawn as node n's surface.
    const auto draw = [&](std::size_t n, std::vector<Vec> points) -> Result<void> {
        const upkg::BspNode& node = model.nodes[n];
        const std::size_t count = points.size();
        const upkg::BspSurf& surf = model.surfs[static_cast<std::size_t>(node.iSurf)];
        if (!within(surf.pBase, model.points.size()))
            return refuse(n, "pBase", surf.pBase, model.points.size(), "points");
        for (const auto& [what, index] : {std::pair<std::string_view, std::int32_t>{"vNormal", surf.vNormal},
                                          {"vTextureU", surf.vTextureU},
                                          {"vTextureV", surf.vTextureV}}) {
            if (!within(index, model.vectors.size()))
                return refuse(n, what, index, model.vectors.size(), "vectors");
        }
        const Vec base = toDouble(model.points[static_cast<std::size_t>(surf.pBase)]);
        const upkg::Vector3& normal = model.vectors[static_cast<std::size_t>(surf.vNormal)];
        const Vec along = toDouble(normal);
        const Vec textureU = toDouble(model.vectors[static_cast<std::size_t>(surf.vTextureU)]);
        const Vec textureV = toDouble(model.vectors[static_cast<std::size_t>(surf.vTextureV)]);

        // 5. Its orientation: the fan's summed cross product along the
        // surface's normal. Zero is no area; negative is wound the other way.
        double s = 0;
        for (std::size_t k = 1; k + 1 < count; ++k)
            s += dot(cross(minus(points[k], points[0]), minus(points[k + 1], points[0])), along);
        if (s == 0) return {};
        if (s < 0) std::reverse(points.begin() + 1, points.end()); // P0, Pn-1, ..., P1

        vertexTotal += count;
        indexTotal += 3 * (count - 2);
        if (vertexTotal >= COUNT_LIMIT || indexTotal >= COUNT_LIMIT)
            return fail(ErrorCode::MalformedData,
                        "the Model's geometry would pass the 2^32 vertices or indices a u32 counts,"
                        " at node " + std::to_string(n));

        // 6. Its material. A null texture asks nothing and wears none.
        const SurfaceMaterial* made = nullptr;
        if (surf.texture.kind() != upkg::ObjectReferenceKind::Null)
            made = materials(surf.texture, (surf.polyFlags & PF_MASKED) != 0);

        // 7. Its vertices, in the polygon's order.
        Drawn out;
        out.material = made != nullptr ? made->id : std::string{};
        out.polyFlags = surf.polyFlags;
        out.corners.reserve(count);
        // UTA-0156 SS 4.3: the zone on the plane's front, the side the surface faces.
        auto zone = static_cast<std::uint8_t>(node.iZone[1] < zoneCount ? node.iZone[1] : 0);
        const double alongLength = std::sqrt(dot(along, along));
        // UTA-0390: an unzoned node is looked up in front of its centroid,
        // stepping out until a step finds a zone.
        if (zone == 0 && rooms != nullptr && alongLength > 0) {
            Vec centroid{};
            for (const Vec& point : points)
                for (std::size_t k = 0; k < 3; ++k) centroid[k] += point[k] / static_cast<double>(count);
            for (double step = 1; zone == 0 && step <= ZONE_SEARCH_REACH; step *= 2) {
                const double reach = step / alongLength;
                zone = ubundle::zoneAt(*rooms,
                                       {static_cast<float>(centroid[0] + along[0] * reach),
                                        static_cast<float>(centroid[1] + along[1] * reach),
                                        static_cast<float>(centroid[2] + along[2] * reach)},
                                       zoneCount);
            }
        }
        // UTA-0269: UT99 pans PAN_TEXELS_PER_SECOND, which in repeats a second
        // is that over the texture's size. UTA-0276: times the viewer's zone
        // speed, which the renderer applies.
        if (made != nullptr) {
            if ((surf.polyFlags & PF_AUTO_U_PAN) != 0)
                out.panRate[0] = static_cast<float>(PAN_TEXELS_PER_SECOND / made->uSize);
            if ((surf.polyFlags & PF_AUTO_V_PAN) != 0)
                out.panRate[1] = static_cast<float>(PAN_TEXELS_PER_SECOND / made->vSize);
        }
        out.mapping = Mapping{base, textureU, textureV, static_cast<double>(surf.panU), static_cast<double>(surf.panV),
                              made, {normal.x, normal.y, normal.z}, zone};
        for (const Vec& point : points) out.corners.push_back(out.mapping.vertexAt(point));

        // 7a. UTA-0055 SS 4.1: whether it sways.
        ubundle::SwayKind kind = ubundle::SwayKind::None;
        if (made != nullptr && made->cloth && (surf.polyFlags & (PF_TWO_SIDED | PF_NOT_SOLID)) != 0)
            kind = ubundle::SwayKind::Cloth;
        else if (made != nullptr && made->water && std::abs(along[2]) >= FACING_UP * alongLength)
            kind = ubundle::SwayKind::Water;
        if (kind != ubundle::SwayKind::None && alongLength > 0) {
            out.sway = swaying.size();
            swaying.push_back(SwayPolygon{kind, made->id, points,
                                          {along[0] / alongLength, along[1] / alongLength, along[2] / alongLength}});
        }
        drawn.push_back(std::move(out));
        return {};
    };

    for (std::size_t n = 0; n < model.nodes.size(); ++n) {
        const upkg::BspNode& node = model.nodes[n];
        const std::size_t count = node.numVertices;

        // 1. Fewer than three vertices draws nothing, and is checked no further.
        if (count < 3) continue;

        // 2. Its surface.
        if (!within(node.iSurf, model.surfs.size()))
            return refuse(n, "iSurf", node.iSurf, model.surfs.size(), "surfs");
        const upkg::BspSurf& surf = model.surfs[static_cast<std::size_t>(node.iSurf)];

        // 3. A surface UT99 never draws is checked no further, so it cannot
        // refuse GEOM. COLL checks it, since an invisible node can still be
        // solid (UTA-0111 SS 4.3).
        if ((surf.polyFlags & PF_INVISIBLE) != 0) continue;
        // 3a. UTA-0310: nor one an invisible zone portal covers.
        if (hidden[n]) continue;
        // UTA-0263 SS 4.3: a flame sheet is drawn as a FLAM record instead.
        if (std::ranges::binary_search(omitted, static_cast<std::uint32_t>(node.iSurf))) continue;

        // 4. Its polygon, and its surface's point and vectors. Every index is
        // the file's own and unchecked by upkg (UTA-0069 SS 3.2).
        if (node.iVertPool < 0
            || static_cast<std::uint64_t>(node.iVertPool) + count > model.verts.size())
            return refuse(n, "vertex pool", node.iVertPool, model.verts.size(), "verts");
        std::vector<Vec> points;
        points.reserve(count);
        for (std::size_t k = 0; k < count; ++k) {
            const std::int32_t point = model.verts[static_cast<std::size_t>(node.iVertPool) + k].pVertex;
            if (!within(point, model.points.size()))
                return refuse(n, "pVertex", point, model.points.size(), "points");
            points.push_back(toDouble(model.points[static_cast<std::size_t>(point)]));
            // UTA-0217: finite and inside the bound, before step 5 -- a NaN
            // makes s neither zero nor negative, so it would be drawn.
            for (const double part : points.back())
                if (!(std::abs(part) <= MAX_COORDINATE))
                    return fail(ErrorCode::MalformedData,
                                "the Model's node " + std::to_string(n) + " has a point at "
                                    + std::to_string(part) + ", which is not finite or lies past the bound of "
                                    + std::to_string(MAX_COORDINATE));
        }
        UTA_CHECK(draw(n, std::move(points)));
    }
    // UTA-0109 SS 4.6: a recipe's covers, each drawn as its node's surface.
    for (const Cover& cover : covers) UTA_CHECK(draw(cover.node, cover.points));

    // 7b. UTA-0055 SS 4.2 and SS 4.3: the swaying polygons cut into pieces,
    // each corner weighted. One the budget left whole keeps its corners.
    const SwaySlicing slicing = sliceSway(swaying);
    if (sway != nullptr)
        *sway = SwayReport{slicing.waterSlice, slicing.waterStill, slicing.clothStill, slicing.vertices,
                           slicing.clothVertices};
    for (Drawn& node : drawn) {
        if (node.sway == SIZE_MAX || slicing.pieces[node.sway].empty()) continue;
        vertexTotal -= node.corners.size();
        indexTotal -= 3 * (node.corners.size() - 2);
        const ubundle::SwayKind kind = swaying[node.sway].kind;
        for (const SwayPiece& piece : slicing.pieces[node.sway]) {
            std::vector<ubundle::GeometryVertex>& fan = node.pieces.emplace_back();
            for (std::size_t k = 0; k < piece.points.size(); ++k) {
                ubundle::GeometryVertex vertex = node.mapping.vertexAt(piece.points[k]);
                vertex.sway = kind;
                vertex.swayWeight = piece.weights[k];
                vertex.swayCell = piece.cell;
                // SS 4.2: the shader moves it along its normal, one a point.
                for (std::size_t axis = 0; axis < 3; ++axis)
                    vertex.normal[axis] = static_cast<float>(piece.directions[k][axis]);
                fan.push_back(vertex);
            }
            vertexTotal += fan.size();
            indexTotal += 3 * (fan.size() - 2);
        }
    }
    if (vertexTotal >= COUNT_LIMIT || indexTotal >= COUNT_LIMIT)
        return fail(ErrorCode::MalformedData,
                    "the Model's geometry would pass the 2^32 vertices or indices a u32 counts, once sliced");

    // Batches in ascending key order; a stable sort keeps node order within a
    // key (INV-8).
    std::stable_sort(drawn.begin(), drawn.end(), [](const Drawn& a, const Drawn& b) {
        return std::tie(a.material, a.polyFlags, a.panRate) < std::tie(b.material, b.polyFlags, b.panRate);
    });

    ubundle::Geometry geometry;
    geometry.vertices.reserve(static_cast<std::size_t>(vertexTotal));
    geometry.indices.reserve(static_cast<std::size_t>(indexTotal));
    for (Drawn& node : drawn) {
        if (geometry.batches.empty() || geometry.batches.back().material != node.material
            || geometry.batches.back().polyFlags != node.polyFlags || geometry.batches.back().panRate != node.panRate)
            geometry.batches.push_back(ubundle::GeometryBatch{node.material, node.polyFlags,
                                                              static_cast<std::uint32_t>(geometry.indices.size()),
                                                              0, node.panRate});

        // 8. A fan from the first vertex; a swaying node's, one a piece.
        const auto fan = [&](const std::vector<ubundle::GeometryVertex>& corners) {
            const auto first = static_cast<std::uint32_t>(geometry.vertices.size());
            const auto count = static_cast<std::uint32_t>(corners.size());
            for (std::uint32_t k = 1; k + 1 < count; ++k) {
                geometry.indices.push_back(first);
                geometry.indices.push_back(first + k);
                geometry.indices.push_back(first + k + 1);
            }
            geometry.batches.back().indexCount += 3 * (count - 2);
            geometry.vertices.insert(geometry.vertices.end(), corners.begin(), corners.end());
        };
        if (node.pieces.empty()) fan(node.corners);
        for (const std::vector<ubundle::GeometryVertex>& piece : node.pieces) fan(piece);
    }
    return geometry;
}

} // namespace uta::ubake
