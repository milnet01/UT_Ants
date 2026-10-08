// The renderer's light terms -- docs/specs/UTA-0292-reference-path-tracer.md
// SS 4.3, INV-6.
//
// With Config::lightTerms set, a lit pixel's emission readback holds red the
// direct term and green the indirect, each a luma, as scene.frag adds them
// before reflectance. ut-ref scores the renderer against exact light through
// these two numbers, so a term missing its gain, its power or AO's share would
// shift every score it produces without anything else noticing.
//
// The square wears an emissive material, so a shader writing its emission
// over the terms -- or the terms over nothing -- fails here.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"
#include "ubake/LightModel.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;

namespace {

constexpr std::uint32_t WIDTH = 160;
constexpr std::uint32_t HEIGHT = 64;

Config termsFrame() {
    Config config;
    config.width = WIDTH;
    config.height = HEIGHT;
    config.lightTerms = true;
    // No haze and Low, as RenderLightingTest's frames: nothing but the model.
    config.hazeScale = 0;
    config.tier = uta::urender::Tier::Low;
    return config;
}

/// Every texel of the occlusion atlas `glowingSquare` gives when asked.
constexpr std::uint8_t HALF_OPEN = 128;

/// A lit square facing the camera at x = 100, glowing, lit by one steady light,
/// and with an atlas of HALF_OPEN everywhere when `occluded` -- the shape
/// RenderOcclusionTest's square takes.
uta::ubundle::Bundle glowingSquare(const uta::ubundle::Light& light, bool occluded = false) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, 40, "glow", 0);
    const std::size_t vertices = geometry.vertices.size();
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    if (occluded) {
        uta::ubundle::Occlusion atlas;
        atlas.texelSize = 16;
        atlas.width = 8;
        atlas.height = 8;
        atlas.texels.assign(64, HALF_OPEN);
        atlas.uv.assign(vertices, {6.5f / 8, 6.5f / 8});
        bundle.occlusion = std::move(atlas);
    }
    addEmissiveMaterial(bundle, "glow", {255, 255, 255, 255}, {201, 121, 41, 255});
    bundle.lights = std::vector{light};
    return bundle;
}

/// The four floats at pixel (80, 32) of the emission target.
std::array<float, 4> centreTerms(Renderer& renderer, const uta::ubundle::Bundle& bundle) {
    requireOk(renderer.draw(bundle, Camera{}));
    const auto bytes = renderer.readback(Renderer::Target::Emission);
    REQUIRE(bytes.has_value());
    REQUIRE(bytes->size() == std::size_t{WIDTH} * HEIGHT * 4 * sizeof(float));
    std::array<float, 4> pixel{};
    std::memcpy(pixel.data(), bytes->data() + (std::size_t{32} * WIDTH + 80) * 4 * sizeof(float), sizeof(pixel));
    return pixel;
}

/// The world point pixel (80, 32)'s centre sees on the square -- as
/// RenderLightingTest's centrePixelOnSquare.
uta::ubake::Vec3 centrePixelOnSquare() {
    return {100.0, (80.5 / WIDTH * 2 - 1) * 250.0, -(32.5 / HEIGHT * 2 - 1) * 100.0};
}

} // namespace

TEST_CASE("UTA-0292 INV-6: the emission target holds the direct and indirect terms", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(termsFrame());
    const uta::ubundle::Light light = steadyLight({60, 0, 0}, 128, 2);
    // A white light lights every channel alike, so the luma is one channel's.
    const double direct =
        uta::ubake::shownLight(uta::ubake::lightAt(light, centrePixelOnSquare(), {-1, 0, 0}).r);
    REQUIRE(direct > 0.05);

    SECTION("with no probes the indirect term is zero") {
        const auto terms = centreTerms(renderer, glowingSquare(light));
        CAPTURE(terms[0], terms[1], terms[2], direct);
        CHECK(std::abs(terms[0] - direct) <= 0.01 * direct);
        CHECK(terms[1] == 0.0F);
        CHECK(terms[2] == 0.0F); // the emit map's colour is not there
    }

    SECTION("with probes the indirect term is the probes' light") {
        uta::ubundle::Bundle bundle = glowingSquare(light);
        constexpr float BOUNCE = 0.25F;
        addEvenProbes(bundle, {100, -40, -40}, {100, 40, 40}, BOUNCE);
        const auto terms = centreTerms(renderer, bundle);
        CAPTURE(terms[0], terms[1], terms[2], direct);
        CHECK(std::abs(terms[0] - direct) <= 0.01 * direct);
        CHECK(std::abs(terms[1] - BOUNCE) <= 0.01 * BOUNCE);
        CHECK(terms[2] == 0.0F);
    }

    SECTION("occlusion darkens the indirect term and not the direct") {
        uta::ubundle::Bundle bundle = glowingSquare(light, true);
        constexpr float BOUNCE = 0.25F;
        addEvenProbes(bundle, {100, -40, -40}, {100, 40, 40}, BOUNCE);
        const auto terms = centreTerms(renderer, bundle);
        const double occluded = BOUNCE * HALF_OPEN / 255.0;
        CAPTURE(terms[0], terms[1], direct, occluded);
        CHECK(std::abs(terms[0] - direct) <= 0.01 * direct);
        CHECK(std::abs(terms[1] - occluded) <= 0.01 * occluded);
    }
}
