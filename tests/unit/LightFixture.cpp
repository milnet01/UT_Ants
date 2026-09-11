#include "LightFixture.h"

#include <algorithm>
#include <tuple>

namespace uta::test::light {
namespace {

std::array<float, 3> asFloats(const Vec3& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

} // namespace

std::vector<Triangle> quad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
                           const Vec3& normal, const std::string& material,
                           std::uint32_t polyFlags) {
    return {Triangle{{a, b, c}, normal, material, polyFlags},
            Triangle{{a, c, d}, normal, material, polyFlags}};
}

std::vector<Triangle> room(const Vec3& min, const Vec3& max, const std::array<Face, 6>& faces) {
    const Vec3& lo = min;
    const Vec3& hi = max;
    std::vector<Triangle> out;
    const auto add = [&out](std::vector<Triangle> more) {
        out.insert(out.end(), more.begin(), more.end());
    };
    add(quad({lo.x, lo.y, lo.z}, {lo.x, hi.y, lo.z}, {lo.x, hi.y, hi.z}, {lo.x, lo.y, hi.z},
             {1, 0, 0}, faces[0].material, faces[0].polyFlags));
    add(quad({hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {hi.x, lo.y, hi.z},
             {-1, 0, 0}, faces[1].material, faces[1].polyFlags));
    add(quad({lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z},
             {0, 1, 0}, faces[2].material, faces[2].polyFlags));
    add(quad({lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z},
             {0, -1, 0}, faces[3].material, faces[3].polyFlags));
    add(quad({lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
             {0, 0, 1}, faces[4].material, faces[4].polyFlags));
    add(quad({lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z},
             {0, 0, -1}, faces[5].material, faces[5].polyFlags));
    return out;
}

ubundle::Geometry geometryOf(std::vector<Triangle> triangles) {
    std::stable_sort(triangles.begin(), triangles.end(), [](const Triangle& a, const Triangle& b) {
        return std::tie(a.material, a.polyFlags) < std::tie(b.material, b.polyFlags);
    });
    ubundle::Geometry geometry;
    for (const Triangle& triangle : triangles) {
        if (geometry.batches.empty() || geometry.batches.back().material != triangle.material
            || geometry.batches.back().polyFlags != triangle.polyFlags)
            geometry.batches.push_back(ubundle::GeometryBatch{
                triangle.material, triangle.polyFlags,
                static_cast<std::uint32_t>(geometry.indices.size()), 0});
        for (const Vec3& corner : triangle.corners) {
            geometry.indices.push_back(static_cast<std::uint32_t>(geometry.vertices.size()));
            geometry.vertices.push_back(
                ubundle::GeometryVertex{asFloats(corner), asFloats(triangle.normal), 0, 0});
        }
        geometry.batches.back().indexCount += 3;
    }
    return geometry;
}

} // namespace uta::test::light
