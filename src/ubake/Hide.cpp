// The brushes a map's recipe hides, and the openings they leave covered --
// docs/specs/UTA-0109-map-geometry.md SS 4.6 (UTA-0396).

#include "ubake/Hide.h"

#include "ubake/Geometry.h"
#include "ubake/Lamps.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <optional>
#include <set>
#include <string>

namespace uta::ubake {
namespace {

using Vec = std::array<double, 3>;

/// How far from the cover's plane a corner may lie and still be on it: the
/// unit hiddenBehindPortals allows, since BSP corners are stored as f32.
constexpr double ON_PLANE = 1.0;

double dot(const Vec& a, const Vec& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec cross(const Vec& a, const Vec& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

/// Node `node`'s corners, or none where an index leaves its table.
std::vector<Vec> cornersOf(const upkg::Model& model, const upkg::BspNode& node) {
    const std::size_t count = node.numVertices;
    if (count < 3 || node.iVertPool < 0 || static_cast<std::uint64_t>(node.iVertPool) + count > model.verts.size())
        return {};
    std::vector<Vec> corners;
    for (std::size_t k = 0; k < count; ++k) {
        const std::int32_t point = model.verts[static_cast<std::size_t>(node.iVertPool) + k].pVertex;
        if (!within(point, model.points.size())) return {};
        const upkg::Vector3& p = model.points[static_cast<std::size_t>(point)];
        corners.push_back({p.x, p.y, p.z});
    }
    return corners;
}

/// The convex hull of `points` on the plane with unit normal `normal`, in
/// order around it: Andrew's monotone chain over two axes spanning the plane.
/// Repeated and in-line points are dropped.
std::vector<Vec> hullOn(const std::vector<Vec>& points, const Vec& normal) {
    if (points.size() < 3) return {};
    const Vec helper = std::abs(normal[2]) < 0.9 ? Vec{0, 0, 1} : Vec{1, 0, 0};
    const Vec u = cross(normal, helper);
    const Vec v = cross(normal, u);
    struct Flat {
        double x;
        double y;
        std::size_t index;
    };
    std::vector<Flat> flat;
    for (std::size_t i = 0; i < points.size(); ++i) flat.push_back({dot(points[i], u), dot(points[i], v), i});
    std::ranges::sort(flat, [](const Flat& a, const Flat& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    const auto turn = [](const Flat& o, const Flat& a, const Flat& b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };
    std::vector<Flat> hull;
    for (int pass = 0; pass < 2; ++pass) {
        const std::size_t floor = hull.size();
        for (const Flat& point : flat) {
            while (hull.size() >= floor + 2 && turn(hull[hull.size() - 2], hull.back(), point) <= 0) hull.pop_back();
            hull.push_back(point);
        }
        hull.pop_back(); // the next pass starts on it
        std::ranges::reverse(flat);
    }
    std::vector<Vec> out;
    for (const Flat& point : hull) out.push_back(points[point.index]);
    return out;
}

} // namespace

Result<Hiding> hideBrushes(const upkg::Package& map, const upkg::Model& model,
                           const std::vector<urecipe::HiddenBrush>& hidden) {
    Hiding out;
    std::set<std::uint32_t> surfaces;
    for (const urecipe::HiddenBrush& wanted : hidden) {
        const auto refuse = [&wanted](const std::string& why) {
            return fail(ErrorCode::MalformedData, std::format("recipe [hide {}]: {}", wanted.name, why));
        };
        const auto ownedBy = [&model](std::uint32_t brush, std::uint32_t s) {
            const upkg::ObjectReference actor = model.surfs[s].actor;
            return actor.kind() == upkg::ObjectReferenceKind::Export && actor.index() == brush;
        };

        // Step 1: the brush, and every surface it gives.
        const auto brush = exportIndexNamed(map, wanted.brush);
        if (!brush.has_value()) return refuse(std::format("{} is not in the map", wanted.brush));
        std::set<std::uint32_t> own;
        for (std::uint32_t s = 0; s < model.surfs.size(); ++s)
            if (ownedBy(*brush, s)) own.insert(s);
        if (own.empty()) return refuse(std::format("{} gives no surface", wanted.brush));

        // Step 2: the cover -- its brush's polygon, a surface drawn by some node.
        const auto coverBrush = exportIndexNamed(map, wanted.coverBrush);
        if (!coverBrush.has_value()) return refuse(std::format("{} is not in the map", wanted.coverBrush));
        std::optional<std::size_t> node;
        for (std::size_t n = 0; n < model.nodes.size() && !node; ++n) {
            const std::int32_t s = model.nodes[n].iSurf;
            if (model.nodes[n].numVertices >= 3 && within(s, model.surfs.size())
                && ownedBy(*coverBrush, static_cast<std::uint32_t>(s))
                && model.surfs[static_cast<std::size_t>(s)].iBrushPoly == static_cast<std::int64_t>(wanted.coverPolygon))
                node = n;
        }
        if (!node.has_value())
            return refuse(std::format("{} has no drawn polygon {}", wanted.coverBrush, wanted.coverPolygon));
        const auto coverSurface = static_cast<std::uint32_t>(model.nodes[*node].iSurf);
        if (own.contains(coverSurface)) return refuse("the cover is a surface of the brush it hides");
        const upkg::BspSurf& surf = model.surfs[coverSurface];
        if (!within(surf.pBase, model.points.size()) || !within(surf.vNormal, model.vectors.size()))
            return refuse("the cover's surface names a point or vector past its table");
        const upkg::Vector3& base = model.points[static_cast<std::size_t>(surf.pBase)];
        const upkg::Vector3& along = model.vectors[static_cast<std::size_t>(surf.vNormal)];
        Vec normal{along.x, along.y, along.z};
        const double length = std::sqrt(dot(normal, normal));
        if (!(length > 0)) return refuse("the cover's surface has no normal");
        for (double& part : normal) part /= length;
        const double offset = dot(normal, {base.x, base.y, base.z});

        // Step 3: where the brush's polygons meet the plane -- corners on it,
        // and edges through it.
        std::vector<Vec> cut;
        for (const upkg::BspNode& each : model.nodes) {
            if (!within(each.iSurf, model.surfs.size()) || !own.contains(static_cast<std::uint32_t>(each.iSurf)))
                continue;
            const std::vector<Vec> corners = cornersOf(model, each);
            for (std::size_t k = 0; k < corners.size(); ++k) {
                const Vec& p = corners[k];
                const Vec& q = corners[(k + 1) % corners.size()];
                const double dp = dot(normal, p) - offset;
                const double dq = dot(normal, q) - offset;
                if (std::abs(dp) <= ON_PLANE) {
                    cut.push_back({p[0] - dp * normal[0], p[1] - dp * normal[1], p[2] - dp * normal[2]});
                } else if ((dp > ON_PLANE && dq < -ON_PLANE) || (dp < -ON_PLANE && dq > ON_PLANE)) {
                    const double t = dp / (dp - dq);
                    cut.push_back({p[0] + t * (q[0] - p[0]), p[1] + t * (q[1] - p[1]), p[2] + t * (q[2] - p[2])});
                }
            }
        }
        std::vector<Vec> hull = hullOn(cut, normal);
        if (hull.size() < 3) return refuse(std::format("{} does not cross the cover's plane", wanted.brush));

        surfaces.insert(own.begin(), own.end());
        out.covers.push_back(Cover{*node, std::move(hull)});
    }
    out.surfaces.assign(surfaces.begin(), surfaces.end());
    return out;
}

} // namespace uta::ubake
