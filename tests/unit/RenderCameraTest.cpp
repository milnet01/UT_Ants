// The camera and UTA-0075's jitter -- docs/specs/UTA-0014-vulkan-draw-path.md
// SS 4.3 and SS 4.11 provision 1. SS 4.12 puts both in the device-free tier.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "urender/Placement.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>

using Catch::Matchers::WithinAbs;
using uta::urender::Camera;
using uta::urender::gpu::Mat4;

namespace {

std::array<double, 4> transform(const Mat4& m, const std::array<double, 4>& p) {
    std::array<double, 4> out{};
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column) out[row] += m[column * 4 + row] * p[column];
    return out;
}

} // namespace

TEST_CASE("the view looks along UTA-0119's rotated +X with +Y right and +Z up", "[render]") {
    Camera camera;
    camera.location = {100, 200, 300};
    camera.rotation = {0, 16384, 0}; // a quarter turn of yaw: looking along +Y
    const Mat4 view = uta::urender::viewOf(camera);

    // A point ahead of the camera is on view +Z; to its right (world -X when
    // facing +Y in UT's left-handed frame) is view +X; above is view +Y.
    const auto ahead = transform(view, {100, 250, 300, 1});
    CHECK_THAT(ahead[0], WithinAbs(0, 1e-3));
    CHECK_THAT(ahead[1], WithinAbs(0, 1e-3));
    CHECK_THAT(ahead[2], WithinAbs(50, 1e-3));
    const auto right = transform(view, {90, 200, 300, 1});
    CHECK_THAT(right[0], WithinAbs(10, 1e-3));
    const auto above = transform(view, {100, 200, 310, 1});
    CHECK_THAT(above[1], WithinAbs(10, 1e-3));
}

TEST_CASE("the projection maps near to depth 0 and far to depth 1 with +Y down", "[render]") {
    Camera camera;
    camera.nearPlane = 1;
    camera.farPlane = 32768;
    const Mat4 projection = uta::urender::projectionOf(camera, 64, 64);

    const auto nearPoint = transform(projection, {0, 0, 1, 1});
    CHECK_THAT(nearPoint[2] / nearPoint[3], WithinAbs(0, 1e-6));
    const auto farPoint = transform(projection, {0, 0, 32768, 1});
    CHECK_THAT(farPoint[2] / farPoint[3], WithinAbs(1, 1e-6));
    const auto up = transform(projection, {0, 10, 100, 1});
    CHECK(up[1] / up[3] < 0);
}

TEST_CASE("the jitter is a Halton sequence of base 2 and 3 over eight frames", "[render]") {
    // index = frame + 1; each value minus a half.
    CHECK_THAT(uta::urender::haltonJitter(0)[0], WithinAbs(0.5 - 0.5, 1e-6));
    CHECK_THAT(uta::urender::haltonJitter(0)[1], WithinAbs(1.0 / 3.0 - 0.5, 1e-6));
    CHECK_THAT(uta::urender::haltonJitter(1)[0], WithinAbs(0.25 - 0.5, 1e-6));
    CHECK_THAT(uta::urender::haltonJitter(1)[1], WithinAbs(2.0 / 3.0 - 0.5, 1e-6));
    CHECK_THAT(uta::urender::haltonJitter(2)[0], WithinAbs(0.75 - 0.5, 1e-6));
    CHECK_THAT(uta::urender::haltonJitter(2)[1], WithinAbs(1.0 / 9.0 - 0.5, 1e-6));
    for (std::uint64_t frame = 0; frame < 32; ++frame) {
        const auto j = uta::urender::haltonJitter(frame);
        CHECK(j[0] >= -0.5f);
        CHECK(j[0] < 0.5f);
        CHECK(j[1] >= -0.5f);
        CHECK(j[1] < 0.5f);
        CHECK(j == uta::urender::haltonJitter(frame + 8));
    }
}

TEST_CASE("a jittered projection moves every point by the same fraction of a pixel", "[render]") {
    const Camera camera;
    const Mat4 projection = uta::urender::projectionOf(camera, 64, 32);
    const Mat4 shifted = uta::urender::jittered(projection, {0.25f, -0.5f}, 64, 32);
    for (const std::array<double, 4>& p : {std::array<double, 4>{0, 0, 10, 1}, {5, -3, 400, 1}}) {
        const auto a = transform(projection, p);
        const auto b = transform(shifted, p);
        // One pixel is 2 / width of NDC.
        CHECK_THAT(b[0] / b[3] - a[0] / a[3], WithinAbs(2.0 * 0.25 / 64, 1e-6));
        CHECK_THAT(b[1] / b[3] - a[1] / a[3], WithinAbs(2.0 * -0.5 / 32, 1e-6));
    }
}
