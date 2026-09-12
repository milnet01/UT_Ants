// Movers through the draw path -- docs/specs/UTA-0014-vulkan-draw-path.md
// SS 4.5, SS 4.8 and SS 4.11.
//
// INV-8 grades the placement matrix device-free. These cases grade what only a
// frame shows: that a mover is drawn where that matrix puts it; that a mirrored
// mover -- a negative postScale reverses its winding on screen -- is still drawn
// rather than culled; that a mover that moved writes motion vectors from where
// it was, while the still level beside it writes none; and that a mover that
// moved inside a light's radius redraws that light's shadow tiles.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;

namespace {

constexpr std::uint32_t WIDTH = 160;
constexpr std::uint32_t HEIGHT = 64;
constexpr Rgba PAINT{201, 121, 61, 255};
constexpr Rgba CLEAR{0, 0, 0, 255};

std::uint32_t columnOf(float y) { return static_cast<std::uint32_t>(80.0f + y * 0.32f); }

Config linearFrame() {
    Config config;
    config.width = WIDTH;
    config.height = HEIGHT;
    config.linearOutput = true;
    return config;
}

/// A mover whose pivot-space shape is a square facing -X, centred on its pivot.
uta::ubundle::MoverShape squareMover(std::array<float, 3> location, std::array<float, 3> postScale,
                                     std::uint32_t polyFlags) {
    uta::ubundle::MoverShape mover;
    mover.exportIndex = 7;
    mover.location = location;
    mover.postScale = postScale;
    addSquare(mover.geometry, 0, 0, 0, 20, "paint", polyFlags);
    return mover;
}

uta::ubundle::Bundle withMover(const uta::ubundle::MoverShape& mover) {
    uta::ubundle::Bundle bundle;
    bundle.movers = std::vector{mover};
    addSolidMaterial(bundle, "paint", PAINT);
    return bundle;
}

Rgba colourAt(Renderer& renderer, const uta::ubundle::Bundle& bundle, float y) {
    requireOk(renderer.draw(bundle, Camera{}));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    return pixelAt(*pixels, WIDTH, columnOf(y), HEIGHT / 2);
}

} // namespace

TEST_CASE("SS 4.5: a mover is drawn where its placement puts it", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());
    // A hundred units ahead and sixty to the right: drawn there and not at the pivot's own place.
    const uta::ubundle::Bundle bundle = withMover(squareMover({100, 60, 0}, {1, 1, 1}, PF_UNLIT));
    CHECK(colourAt(renderer, bundle, 60) == PAINT);
    CHECK(colourAt(renderer, bundle, 0) == CLEAR);
}

TEST_CASE("SS 4.5: a mirrored mover is still drawn and not culled", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());
    // A negative postScale on one axis turns the square inside out in world
    // space: its triangles wind the other way on screen. The surface is still
    // the one facing the camera, so it must still be drawn.
    const uta::ubundle::Bundle bundle = withMover(squareMover({100, 0, 0}, {1, -1, 1}, PF_UNLIT));
    CHECK(colourAt(renderer, bundle, 0) == PAINT);
}

TEST_CASE("SS 4.11: a mover that moved writes motion vectors and the still level beside it none", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());

    uta::ubundle::MoverShape mover = squareMover({100, -100, 0}, {1, 1, 1}, PF_UNLIT);
    uta::ubundle::Bundle bundle = withMover(mover);
    bundle.geometry.emplace();
    addSquare(*bundle.geometry, 100, 100, 0, 20, "paint", PF_UNLIT);

    requireOk(renderer.draw(bundle, Camera{}));
    // The same bundle object, its mover moved: SS 4.5 has a mover move by
    // changing these fields, and the renderer reads them every frame.
    (*bundle.movers)[0].location = {100, -95, 0};
    requireOk(renderer.draw(bundle, Camera{}));

    const auto velocity = renderer.readback(Renderer::Target::Velocity);
    if (!velocity.has_value()) FAIL(velocity.error().message());
    const auto moverMotion = velocityAt(*velocity, WIDTH, columnOf(-96), HEIGHT / 2);
    const auto levelMotion = velocityAt(*velocity, WIDTH, columnOf(100), HEIGHT / 2);
    CAPTURE(moverMotion[0], moverMotion[1], levelMotion[0], levelMotion[1]);
    // Five units to the right at a hundred units is a hundredth of the target.
    CHECK(moverMotion[0] > 0.005f);
    CHECK(levelMotion[0] == 0.0f);
    CHECK(levelMotion[1] == 0.0f);
}

TEST_CASE("SS 4.8: a mover that moved inside a light's radius redraws that light's tiles", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(linearFrame());

    uta::ubundle::Bundle bundle = withMover(squareMover({100, -30, 0}, {1, 1, 1}, 0));
    bundle.geometry.emplace();
    addSquare(*bundle.geometry, 120, 0, 0, 60, "paint", 0);
    bundle.lights = std::vector{steadyLight({60, 0, 0}, 255, 64)};

    requireOk(renderer.draw(bundle, Camera{}));
    CHECK(renderer.lastFrameStats().renderedShadowTiles == 6u);
    requireOk(renderer.draw(bundle, Camera{}));
    CHECK(renderer.lastFrameStats().renderedShadowTiles == 0u);

    (*bundle.movers)[0].location = {100, -25, 0};
    requireOk(renderer.draw(bundle, Camera{}));
    CHECK(renderer.lastFrameStats().renderedShadowTiles == 6u);
    requireOk(renderer.draw(bundle, Camera{}));
    CHECK(renderer.lastFrameStats().renderedShadowTiles == 0u);
}
