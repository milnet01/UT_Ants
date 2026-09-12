// UTA-0014 INV-8 -- docs/specs/UTA-0014-vulkan-draw-path.md SS 4.5.
//
// A mover's pivot-space point is placed at UTA-0119 SS 4.5's
// location + postScale * (Y * P * R * q), graded against
// tests/support/FCoordsPort.h -- the port UTA-0119's own INV-7 grades that
// formula against, so the two cannot drift apart while both pass.
//
// THE FIXTURE ROTATES ON ALL THREE AXES AT ONCE AND SCALES UNEVENLY. An
// unrotated, unscaled mover places every point the same whatever order the
// rotations compose in, and whether postScale applies before or after them.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "support/FCoordsPort.h"
#include "urender/Placement.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>

using Catch::Matchers::WithinAbs;
namespace fc = uta::test::fcoords;

namespace {

std::array<double, 3> apply(const uta::urender::gpu::Mat4& m, const std::array<double, 3>& q) {
    std::array<double, 3> out{};
    for (int row = 0; row < 3; ++row)
        out[row] = m[0 * 4 + row] * q[0] + m[1 * 4 + row] * q[1] + m[2 * 4 + row] * q[2] + m[3 * 4 + row];
    return out;
}

} // namespace

TEST_CASE("INV-8: a mover's point lands where the engine's FCoords place it", "[render]") {
    uta::ubundle::MoverShape mover;
    mover.location = {1520.0f, -384.0f, 96.0f};
    mover.rotation = {4096, 12000, -7000}; // pitch, yaw, roll
    mover.postScale = {1.5f, 0.75f, 2.0f};
    const uta::urender::gpu::Mat4 model = uta::urender::moverModel(mover);

    // The engine's transform with no PrePivot and a unit MainScale: GEOM
    // arrives in MOVR with both already applied (UTA-0119 SS 4.5).
    const fc::Coords toWorld = fc::toWorld({mover.location[0], mover.location[1], mover.location[2]},
                                           {mover.rotation[0], mover.rotation[1], mover.rotation[2]}, {0, 0, 0},
                                           fc::Scale{}, fc::Scale{{1.5, 0.75, 2.0}, 0, 0}, fc::Trig::Exact);

    const std::array<std::array<double, 3>, 4> points = {{
        {0, 0, 0},
        {128, 0, 0},
        {0, 64, 0},
        {37.5, -91.25, 250},
    }};
    for (const auto& q : points) {
        CAPTURE(q[0], q[1], q[2]);
        const std::array<double, 3> placed = apply(model, q);
        const fc::Vec engine = fc::transformPointBy({q[0], q[1], q[2]}, toWorld);
        // The model is stored in float, as the shader reads it; a hundredth of
        // a unit is far below any ordering or scaling error, each of which
        // moves these points by tens of units.
        CHECK_THAT(placed[0], WithinAbs(engine.x, 0.01));
        CHECK_THAT(placed[1], WithinAbs(engine.y, 0.01));
        CHECK_THAT(placed[2], WithinAbs(engine.z, 0.01));
    }
}
