// The level's sky -- UTA-0163. Sky.h says what is drawn and what is not.

#include "urender/Sky.h"

#include "urender/Placement.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace uta::urender {

std::array<std::int32_t, 3> skyFaceRotation(std::uint32_t face) noexcept {
    // +X, -X, +Y, -Y, +Z, -Z, as shadows.glsl orders a point light's faces.
    static constexpr std::array<std::array<std::int32_t, 3>, 6> ROTATIONS{{
        {0, 0, 0}, {0, 32768, 0}, {0, 16384, 0}, {0, 49152, 0}, {16384, 0, 0}, {-16384, 0, 0}}};
    return ROTATIONS[face % 6];
}

Camera skyFaceCamera(const SkyView& sky, std::uint32_t face, std::uint32_t width, std::uint32_t height) noexcept {
    Camera camera;
    camera.location = sky.location;
    camera.rotation = skyFaceRotation(face);
    // The shorter side spans 90 degrees; the vertical field follows from it.
    const double side = std::min(width, height);
    camera.verticalFovDegrees =
        static_cast<float>(2.0 * std::atan(static_cast<double>(height) / side) * 180.0 / std::numbers::pi);
    return camera;
}

gpu::ShadowFace skyFaceSample(std::uint32_t face) noexcept {
    Camera camera;
    camera.rotation = skyFaceRotation(face);
    camera.verticalFovDegrees = 90.0f;
    const std::uint32_t columnIndex = face % SKY_COLUMNS, rowIndex = face / SKY_COLUMNS;
    const auto column = static_cast<float>(columnIndex), row = static_cast<float>(rowIndex);
    return {multiply(projectionOf(camera, 1, 1), viewOf(camera)),
            {column / SKY_COLUMNS, row / SKY_ROWS, 1.0f / SKY_COLUMNS, 1.0f / SKY_ROWS}};
}

namespace {

using Vec3 = std::array<double, 3>;

Vec3 minus(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
double dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

Vec3 positionOf(const ubundle::GeometryVertex& vertex) {
    return {vertex.position[0], vertex.position[1], vertex.position[2]};
}

// A coordinate's gradient along a triangle with edges e1 and e2, over which
// it changes by d1 and d2: the vector g in their plane with dot(g, e1) == d1
// and dot(g, e2) == d2.
Vec3 gradientOf(const Vec3& e1, const Vec3& e2, double d1, double d2, double det) {
    const double a11 = dot(e1, e1), a12 = dot(e1, e2), a22 = dot(e2, e2);
    const double alpha = (d1 * a22 - d2 * a12) / det, beta = (d2 * a11 - d1 * a12) / det;
    return {alpha * e1[0] + beta * e2[0], alpha * e1[1] + beta * e2[1], alpha * e1[2] + beta * e2[2]};
}

// Whether two mappings of one coordinate agree at `p`, up to whole repeats,
// and slope alike.
bool sameMapping(const std::array<float, 4>& a, const std::array<float, 4>& b, const Vec3& p) {
    for (int axis = 0; axis < 3; ++axis)
        if (std::abs(a[axis] - b[axis]) > 1e-6f) return false;
    const double at = dot({a[0], a[1], a[2]}, p) + a[3] - dot({b[0], b[1], b[2]}, p) - b[3];
    return std::abs(at - std::round(at)) < 1e-3;
}

} // namespace

SkyLayers skyLayersOf(const ubundle::Bundle& bundle) {
    SkyLayers result;
    const std::optional<SkyView> view = skyViewOf(bundle);
    if (!view || !bundle.geometry || !bundle.rooms || !bundle.zones) return result;
    result.zone = ubundle::zoneAt(*bundle.rooms, view->location, bundle.zones->size());
    // zoneAt's 0 is also where it found no room, so the sky zone is unknown.
    if (result.zone == 0) return result;
    const ubundle::Geometry& geometry = *bundle.geometry;
    for (const ubundle::GeometryBatch& batch : geometry.batches) {
        if (batch.panRate == std::array<float, 2>{} || batch.material.empty()) continue;
        if ((batch.polyFlags & (gpu::PF_MODULATED | gpu::PF_FAKE_BACKDROP | gpu::PF_INVISIBLE)) != 0) continue;
        const std::uint32_t kind = (batch.polyFlags & gpu::PF_TRANSLUCENT) != 0 ? gpu::SKY_LAYER_TRANSLUCENT
                                   : (batch.polyFlags & gpu::PF_MASKED) != 0   ? gpu::SKY_LAYER_MASKED
                                                                                : gpu::SKY_LAYER_OPAQUE;
        // A batch or index past the end is the geometry upload's to refuse.
        const std::size_t end = std::size_t{batch.firstIndex} + batch.indexCount;
        if (end > geometry.indices.size()) continue;
        std::vector<SkyLayer> sheets;
        for (std::size_t i = batch.firstIndex; i + 2 < end; i += 3) {
            if (std::max({geometry.indices[i], geometry.indices[i + 1], geometry.indices[i + 2]})
                >= geometry.vertices.size())
                continue;
            const ubundle::GeometryVertex& a = geometry.vertices[geometry.indices[i]];
            const ubundle::GeometryVertex& c1 = geometry.vertices[geometry.indices[i + 1]];
            const ubundle::GeometryVertex& c2 = geometry.vertices[geometry.indices[i + 2]];
            if (a.zone != result.zone || c1.zone != result.zone || c2.zone != result.zone) continue;
            const Vec3 p0 = positionOf(a);
            const Vec3 e1 = minus(positionOf(c1), p0), e2 = minus(positionOf(c2), p0);
            const Vec3 n = cross(e1, e2);
            const double det = dot(n, n); // |e1|^2 |e2|^2 - (e1.e2)^2
            if (det < 1e-6) continue;     // degenerate
            const double length = std::sqrt(det);
            const Vec3 unit{n[0] / length, n[1] / length, n[2] / length};
            const Vec3 gu = gradientOf(e1, e2, c1.u - a.u, c2.u - a.u, det);
            const Vec3 gv = gradientOf(e1, e2, c1.v - a.v, c2.v - a.v, det);
            SkyLayer sheet;
            sheet.firstIndex = batch.firstIndex;
            sheet.material = batch.material;
            sheet.panRate = batch.panRate;
            sheet.kind = kind;
            sheet.plane = {static_cast<float>(unit[0]), static_cast<float>(unit[1]), static_cast<float>(unit[2]),
                           static_cast<float>(dot(unit, p0))};
            sheet.u = {static_cast<float>(gu[0]), static_cast<float>(gu[1]), static_cast<float>(gu[2]),
                       static_cast<float>(a.u - dot(gu, p0))};
            sheet.v = {static_cast<float>(gv[0]), static_cast<float>(gv[1]), static_cast<float>(gv[2]),
                       static_cast<float>(a.v - dot(gv, p0))};
            const bool known = std::ranges::any_of(sheets, [&](const SkyLayer& other) {
                const bool coplanar = other.plane[0] * sheet.plane[0] + other.plane[1] * sheet.plane[1]
                                              + other.plane[2] * sheet.plane[2]
                                          > 0.9999f
                                      && std::abs(other.plane[3] - sheet.plane[3]) < 0.5f;
                return coplanar && sameMapping(other.u, sheet.u, p0) && sameMapping(other.v, sheet.v, p0);
            });
            if (!known) sheets.push_back(sheet);
        }
        if (result.layers.size() + sheets.size() > gpu::SKY_LAYER_CAPACITY) continue;
        result.layers.insert(result.layers.end(), sheets.begin(), sheets.end());
    }
    // Drawn in this order, each over the last: the farthest from the sky's
    // eye first, as the faces would have layered them looking straight at it.
    const Vec3 eye{view->location[0], view->location[1], view->location[2]};
    const auto reach = [&eye](const SkyLayer& layer) {
        return std::abs(layer.plane[3] - dot({layer.plane[0], layer.plane[1], layer.plane[2]}, eye));
    };
    std::ranges::stable_sort(result.layers, [&](const SkyLayer& a, const SkyLayer& b) { return reach(a) > reach(b); });
    return result;
}

} // namespace uta::urender
