// The cluster grid -- docs/specs/UTA-0014-vulkan-draw-path.md SS 4.6. SS 4.12
// grades cluster assignment in the device-free tier.
//
// What this cannot see, and SS 10 says so: that a shaded pixel used its own
// cluster's list. shaders/scene.frag computes the cluster the same way, and the
// device tier's lighting cases are what reach it.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "urender/Clusters.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>

using Catch::Matchers::WithinRel;
using uta::urender::Camera;
using uta::urender::clusterGrid;
using uta::urender::clusterOf;
namespace gpu = uta::urender::gpu;

TEST_CASE("SS 4.6: the grid is 16 by 8 by 24 with slices exponential in depth", "[render]") {
    const Camera camera;
    const auto grid = clusterGrid(camera, 160, 90);
    REQUIRE(grid.bounds.size() == 16u * 8u * 24u);
    for (std::uint32_t slice = 0; slice < 24; ++slice) {
        CAPTURE(slice);
        const double expectedNear = camera.nearPlane * std::pow(camera.farPlane / camera.nearPlane, slice / 24.0);
        CHECK_THAT(grid.bounds[slice * 16 * 8].minimum[2], WithinRel(expectedNear, 1e-4));
    }
    CHECK_THAT(grid.bounds.back().maximum[2], WithinRel(static_cast<double>(camera.farPlane), 1e-4));
}

TEST_CASE("a fragment is assigned the cluster whose box contains it", "[render]") {
    const Camera camera;
    const std::uint32_t width = 160, height = 90;
    const auto grid = clusterGrid(camera, width, height);
    const double f = 1.0 / std::tan(camera.verticalFovDegrees * std::numbers::pi / 360.0);
    const double aspect = static_cast<double>(width) / height;

    for (const double px : {0.5, 37.3, 80.0, 121.9, 159.5}) {
        for (const double py : {0.5, 44.9, 89.5}) {
            for (const double z : {1.5, 9.75, 333.0, 4000.0, 31000.0}) {
                CAPTURE(px, py, z);
                // The view-space point that pixel sees at depth z.
                const double ndcX = px / width * 2 - 1;
                const double ndcY = py / height * 2 - 1;
                const double x = ndcX * z * aspect / f;
                const double y = -ndcY * z / f;
                const std::uint32_t c = clusterOf(grid, static_cast<float>(px), static_cast<float>(py),
                                                  static_cast<float>(z), width, height, camera.nearPlane);
                REQUIRE(c < grid.bounds.size());
                const gpu::ClusterBounds& box = grid.bounds[c];
                const double slack = 1e-4 * z;
                CHECK(x >= box.minimum[0] - slack);
                CHECK(x <= box.maximum[0] + slack);
                CHECK(y >= box.minimum[1] - slack);
                CHECK(y <= box.maximum[1] + slack);
                CHECK(z >= box.minimum[2] - slack);
                CHECK(z <= box.maximum[2] + slack);
            }
        }
    }
}

TEST_CASE("a depth outside the frustum is clamped to the nearest slice", "[render]") {
    const Camera camera;
    const auto grid = clusterGrid(camera, 64, 64);
    CHECK(clusterOf(grid, 0.5f, 0.5f, 0.01f, 64, 64, camera.nearPlane) / (16 * 8) == 0u);
    CHECK(clusterOf(grid, 0.5f, 0.5f, 1e7f, 64, 64, camera.nearPlane) / (16 * 8) == 23u);
}
