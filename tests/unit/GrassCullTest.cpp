// UTA-0293: which grass a frame draws -- docs/specs/UTA-0293-grass.md SS 4.4
// and SS 4.6, urender/Grass.h.
//
// INV-11's device case cannot tell a missing distance cull from a missing
// fade: either alone still draws nothing past GRASS_END. This grades the cull.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "urender/Grass.h"
#include "urender/Placement.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

using uta::urender::BLADES_FAR;
using uta::urender::BLADES_NEAR;
using uta::urender::GrassRun;
using uta::urender::Tier;
using uta::urender::visibleGrass;

namespace {

/// A 32-unit box of 10 clumps, 4 with even seeds, `x` units ahead of the eye.
GrassRun runAt(float x) { return GrassRun{7, 4, 10, {x - 16, -16, -40}, {x + 16, 16, -20}}; }

/// The view of a default Camera, along +X, at a square 90-degree view.
uta::urender::gpu::Mat4 view() {
    const uta::urender::Camera camera;
    return uta::urender::multiply(uta::urender::projectionOf(camera, 128, 128), uta::urender::viewOf(camera));
}

const std::array<float, 3> EYE{0, 0, 0};

} // namespace

TEST_CASE("UTA-0293 SS 4.6: grass draws from High and all its clumps at Ultra", "[urender][grass]") {
    const std::vector<GrassRun> runs = {runAt(200)};
    CHECK(visibleGrass(runs, view(), EYE, Tier::Low).empty());
    CHECK(visibleGrass(runs, view(), EYE, Tier::Medium).empty());
    const auto high = visibleGrass(runs, view(), EYE, Tier::High);
    REQUIRE(high.size() == 1);
    CHECK(high[0].firstInstance == 7);
    CHECK(high[0].instanceCount == 4);
    const auto ultra = visibleGrass(runs, view(), EYE, Tier::Ultra);
    REQUIRE(ultra.size() == 1);
    CHECK(ultra[0].instanceCount == 10);
}

TEST_CASE("UTA-0293 INV-11: a cell past GRASS_END is culled", "[urender][grass]") {
    // High's GRASS_END is 1536: the box's near face decides.
    CHECK(visibleGrass(std::vector<GrassRun>{runAt(1500)}, view(), EYE, Tier::High).size() == 1);
    CHECK(visibleGrass(std::vector<GrassRun>{runAt(1600)}, view(), EYE, Tier::High).empty());
    // Ultra reaches 2560.
    CHECK(visibleGrass(std::vector<GrassRun>{runAt(1600)}, view(), EYE, Tier::Ultra).size() == 1);
}

TEST_CASE("UTA-0293 SS 4.4: far cells grow fewer blades", "[urender][grass]") {
    // GRASS_FAR at High is 512.
    const auto near = visibleGrass(std::vector<GrassRun>{runAt(200)}, view(), EYE, Tier::High);
    const auto far = visibleGrass(std::vector<GrassRun>{runAt(900)}, view(), EYE, Tier::High);
    REQUIRE(near.size() == 1);
    REQUIRE(far.size() == 1);
    CHECK(near[0].blades == BLADES_NEAR);
    CHECK(far[0].blades == BLADES_FAR);
}

TEST_CASE("UTA-0293 SS 4.4: a cell outside the view is culled", "[urender][grass]") {
    // Behind the eye, and far off to one side.
    CHECK(visibleGrass(std::vector<GrassRun>{runAt(-200)}, view(), EYE, Tier::High).empty());
    GrassRun aside = runAt(200);
    aside.low[1] += 800;
    aside.high[1] += 800;
    CHECK(visibleGrass(std::vector<GrassRun>{aside}, view(), EYE, Tier::High).empty());
}
