// UTA-0191: Renderer::lightSeconds, pinLightSeconds and unpinLightSeconds.
//
// A pulsing light's brightness is a function of the light time (Lights.cpp's
// LT_PULSE), and that time is read from a clock. So two draws of ONE view
// disagree for a reason that has nothing to do with what is being compared --
// which is exactly what UTA-0191's capture folder hits, holding two images of
// the same view taken a frame apart.
//
// The pulse's cycle is (period + 1) / 32 seconds, so period 31 is one second:
// position 0.25 puts sin at +1 and the light at its brightest, position 0.75
// puts it at -1 and the light at its dimmest. Those two are the pinned times
// below, chosen so the pair differ by the whole swing rather than by a rounding.
//
// linearOutput is set so the difference is read before exposure and the tone
// map, neither of which is the identity and both of which compress it.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;

namespace {

constexpr std::uint32_t WIDTH = 64;
constexpr std::uint32_t HEIGHT = 64;

/// One second, from cycleSeconds(period) = (period + 1) / 32.
constexpr std::uint8_t ONE_SECOND_PERIOD = 31;
constexpr double BRIGHTEST = 0.25; ///< sin(2 pi * 0.25) = +1
constexpr double DIMMEST = 0.75;   ///< sin(2 pi * 0.75) = -1

/// A lit white square with one pulsing light in front of it.
uta::ubundle::Bundle pulsingRoom() {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, 60, "wall", 0); // lit: no PF_UNLIT
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "wall", Rgba{255, 255, 255, 255});

    uta::ubundle::Light light = steadyLight({60.0f, 0.0f, 0.0f}, 255, 200);
    light.type = 2; // LT_PULSE
    light.period = ONE_SECOND_PERIOD;
    light.phase = 0;
    bundle.lights = std::vector<uta::ubundle::Light>{light};
    return bundle;
}

Config linearConfig() {
    Config config;
    config.width = WIDTH;
    config.height = HEIGHT;
    config.linearOutput = true;
    return config;
}

std::vector<std::byte> drawAt(uta::urender::Renderer& renderer, const uta::ubundle::Bundle& bundle,
                              double seconds) {
    renderer.pinLightSeconds(seconds);
    requireOk(renderer.draw(bundle, Camera{}));
    auto pixels = renderer.readback();
    REQUIRE(pixels.has_value());
    return *pixels;
}

} // namespace

TEST_CASE("a pinned light time draws a pulsing light the same way every frame", "[device]") {
    removeDisplay();
    auto renderer = requireRenderer(linearConfig());
    const uta::ubundle::Bundle bundle = pulsingRoom();

    // THE FIRST FRAME OF A RENDERER IS NOT COMPARABLE, and this is measured
    // rather than assumed: the three draws below are at ONE pinned time, and on
    // this machine's GPU the first differs from the second while the second and
    // third agree. SS 4.8 keeps a still light's shadow tiles rather than
    // redrawing them, so the first frame is the one that fills that cache and a
    // later frame reads it. On lavapipe, which takes the Low tier, all three
    // agree -- so a test comparing the first two passes there and fails on a
    // card, which is the worst way for this to be wrong.
    const auto first = drawAt(renderer, bundle, BRIGHTEST);
    const auto second = drawAt(renderer, bundle, BRIGHTEST);
    const auto third = drawAt(renderer, bundle, BRIGHTEST);

    // The rule: at one pinned light time the renderer settles and stays there.
    // Without the pin these are clock readings milliseconds apart and the pulse
    // has moved between them.
    CHECK(second == third);
    CHECK(renderer.lightSeconds() == BRIGHTEST);
    INFO("the first frame matching the second is not required -- see above");
    (void)first;
}

TEST_CASE("a pinned light time reaches the light rather than freezing it", "[device]") {
    removeDisplay();
    auto renderer = requireRenderer(linearConfig());
    const uta::ubundle::Bundle bundle = pulsingRoom();

    // The case above passes just as well if the pin stopped the light being
    // drawn at all, so this one shows the pinned value is what the pulse is
    // evaluated at: half a cycle apart is the whole swing.
    //
    // The warm-up draw is discarded for the reason the case above measures, so
    // that the difference read here is the light's and not the shadow cache's.
    (void)drawAt(renderer, bundle, BRIGHTEST);
    const auto bright = drawAt(renderer, bundle, BRIGHTEST);
    const auto dim = drawAt(renderer, bundle, DIMMEST);
    CHECK(bright != dim);

    const Rgba litBright = pixelAt(bright, WIDTH, WIDTH / 2, HEIGHT / 2);
    const Rgba litDim = pixelAt(dim, WIDTH, WIDTH / 2, HEIGHT / 2);
    CHECK(litBright.r > litDim.r);
}

TEST_CASE("unpinning returns the light time to the clock", "[device]") {
    removeDisplay();
    auto renderer = requireRenderer(linearConfig());
    const uta::ubundle::Bundle bundle = pulsingRoom();

    (void)drawAt(renderer, bundle, BRIGHTEST);
    REQUIRE(renderer.lightSeconds() == BRIGHTEST);

    renderer.unpinLightSeconds();
    requireOk(renderer.draw(bundle, Camera{}));
    // The clock is read from the renderer's own start, so this is however long
    // the fixture has been alive -- never the pinned value, which was chosen
    // for its place in the cycle rather than as an elapsed time.
    CHECK(renderer.lightSeconds() != BRIGHTEST);
}
