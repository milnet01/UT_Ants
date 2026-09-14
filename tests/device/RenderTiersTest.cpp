// UTA-0051 INV-4, INV-7 and INV-8 -- docs/specs/UTA-0051-quality-tiers.md.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <utility>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;
using uta::urender::Tier;

namespace {

// Every channel odd, as bc7Solid requires for an exact colour.
const Rgba RED{201, 21, 41, 255};
const Rgba BLUE{21, 41, 201, 255};

/// The view's left half red and its right half blue. The default camera looks
/// along +X with +Y to its right (tests/unit/RenderCameraTest.cpp), so the red
/// square sits at -Y. Batches and materials go in ascending id order.
uta::ubundle::Bundle halves() {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 120, 0, 120, "blue", PF_UNLIT);
    addSquare(geometry, 100, -120, 0, 120, "red", PF_UNLIT);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "blue", BLUE);
    addSolidMaterial(bundle, "red", RED);
    return bundle;
}

Config squareConfig() {
    Config config;
    config.width = 64; // a multiple of 4, as INV-7 needs
    config.height = 64;
    config.linearOutput = true;
    return config;
}

} // namespace

TEST_CASE("UTA-0051 INV-4: a given tier is the tier in use", "[device]") {
    removeDisplay();
    const uta::ubundle::Bundle bundle = halves();
    // Neither is Low, FrameStats' starting value, so only the override reports it.
    for (const Tier tier : {Tier::Medium, Tier::Ultra}) {
        Config config = squareConfig();
        config.tier = tier;
        Renderer renderer = requireRenderer(config);
        requireOk(renderer.draw(bundle, Camera{}));
        CHECK(renderer.lastFrameStats().tier == tier);
    }
}

TEST_CASE("UTA-0051 INV-7: a half-scale frame is the whole scene upscaled to the output by FSR 1", "[device]") {
    removeDisplay();
    const uta::ubundle::Bundle bundle = halves();

    SECTION("at scale 1 the pixels either side of the centre line are the halves' own") {
        // The control: what a full-size draw puts at INV-7's two FSR 1 probes.
        Config config = squareConfig();
        config.tier = Tier::Low;
        Renderer renderer = requireRenderer(config);
        requireOk(renderer.draw(bundle, Camera{}));
        CHECK(renderer.lastFrameStats().renderScale == 1.0);
        const auto pixels = renderer.readback();
        if (!pixels.has_value()) FAIL(pixels.error().message());
        CHECK(pixelAt(*pixels, 64, 30, 48) == RED);
        CHECK(pixelAt(*pixels, 64, 32, 48) == BLUE);
    }

    SECTION("at scale 0.5 the probes are the upscaled region's") {
        Config config = squareConfig();
        config.tier = Tier::Low;
        config.fixedRenderScale = 0.5;
        Renderer renderer = requireRenderer(config);
        requireOk(renderer.draw(bundle, Camera{}));
        CHECK(renderer.lastFrameStats().renderScale == 0.5);

        const auto pixels = renderer.readback();
        if (!pixels.has_value()) FAIL(pixels.error().message());
        REQUIRE(pixels->size() == 64u * 64u * 4u);
        // Below the half-size region: only the upscale reaches these.
        CHECK(pixelAt(*pixels, 64, 16, 48) == RED);
        CHECK(pixelAt(*pixels, 64, 48, 48) == BLUE);
        // UTA-0154: FSR 1 crosses the centre line, not a linear stretch. RCAS
        // overshoots two columns left of it, where a stretch stays between its
        // two colours. EASU's edge is steeper at column 32: measured on
        // lavapipe, its red channel is 57, a linear stretch's 109, and a linear
        // stretch sharpened by RCAS 71.
        // As int, so a failure prints the channel as a number rather than a character.
        CHECK(static_cast<int>(pixelAt(*pixels, 64, 30, 48).r) > static_cast<int>(RED.r));
        CHECK(static_cast<int>(pixelAt(*pixels, 64, 32, 48).r) < 64);
    }
}

TEST_CASE("UTA-0051 INV-8: velocity readback is refused after a frame below scale 1", "[device]") {
    removeDisplay();
    Config config = squareConfig();
    config.tier = Tier::Low;
    config.fixedRenderScale = 0.5;
    Renderer renderer = requireRenderer(config);
    requireOk(renderer.draw(halves(), Camera{}));
    const auto velocity = renderer.readback(Renderer::Target::Velocity);
    REQUIRE_FALSE(velocity.has_value());
    CHECK(velocity.error().code() == uta::ErrorCode::InvalidArgument);
    CHECK(renderer.readback().has_value()); // colour is still read back
}
