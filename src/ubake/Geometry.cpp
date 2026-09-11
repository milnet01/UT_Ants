// The level's drawable surfaces as triangles --
// docs/specs/UTA-0109-map-geometry.md SS 4.3.

#include "ubake/Geometry.h"

#include <algorithm>
#include <array>
#include <cstddef>
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

bool within(std::int64_t index, std::size_t size) {
    return index >= 0 && static_cast<std::uint64_t>(index) < size;
}

std::unexpected<Error> refuse(std::size_t node, std::string_view what, std::int64_t value,
                              std::size_t size, std::string_view table) {
    return fail(ErrorCode::MalformedData,
                "the Model's node " + std::to_string(node) + " names " + std::string(what) + " "
                    + std::to_string(value) + ", past its " + std::to_string(size) + " "
                    + std::string(table));
}

/// One drawn node, before it is batched.
struct Drawn {
    std::string material;
    std::uint32_t polyFlags = 0;
    std::vector<ubundle::GeometryVertex> corners;
};

} // namespace

Result<ubundle::Geometry> buildGeometry(const upkg::Model& model, const MaterialLookup& materials) {
    std::vector<Drawn> drawn;
    std::uint64_t vertexTotal = 0;
    std::uint64_t indexTotal = 0;

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
        // refuse a bake.
        if ((surf.polyFlags & PF_INVISIBLE) != 0) continue;

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
        }
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
        if (s == 0) continue;
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
        for (const Vec& point : points) {
            ubundle::GeometryVertex vertex;
            vertex.position = {static_cast<float>(point[0]), static_cast<float>(point[1]),
                               static_cast<float>(point[2])};
            vertex.normal = {normal.x, normal.y, normal.z};
            if (made != nullptr) {
                const Vec offset = minus(point, base);
                vertex.u = static_cast<float>((dot(offset, textureU) + surf.panU) / made->uSize);
                vertex.v = static_cast<float>((dot(offset, textureV) + surf.panV) / made->vSize);
            }
            out.corners.push_back(vertex);
        }
        drawn.push_back(std::move(out));
    }

    // Batches in ascending key order; a stable sort keeps node order within a
    // key (INV-8).
    std::stable_sort(drawn.begin(), drawn.end(), [](const Drawn& a, const Drawn& b) {
        return std::tie(a.material, a.polyFlags) < std::tie(b.material, b.polyFlags);
    });

    ubundle::Geometry geometry;
    geometry.vertices.reserve(static_cast<std::size_t>(vertexTotal));
    geometry.indices.reserve(static_cast<std::size_t>(indexTotal));
    for (Drawn& node : drawn) {
        if (geometry.batches.empty() || geometry.batches.back().material != node.material
            || geometry.batches.back().polyFlags != node.polyFlags)
            geometry.batches.push_back(ubundle::GeometryBatch{
                node.material, node.polyFlags, static_cast<std::uint32_t>(geometry.indices.size()), 0});

        // 8. A fan from the first vertex.
        const auto first = static_cast<std::uint32_t>(geometry.vertices.size());
        const auto corners = static_cast<std::uint32_t>(node.corners.size());
        for (std::uint32_t k = 1; k + 1 < corners; ++k) {
            geometry.indices.push_back(first);
            geometry.indices.push_back(first + k);
            geometry.indices.push_back(first + k + 1);
        }
        geometry.batches.back().indexCount += 3 * (corners - 2);
        geometry.vertices.insert(geometry.vertices.end(), node.corners.begin(), node.corners.end());
    }
    return geometry;
}

} // namespace uta::ubake
