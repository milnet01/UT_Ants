// UTA-0014 INV-4 -- docs/specs/UTA-0014-vulkan-draw-path.md SS 4.3.
//
// The surfaceless path creates no surface and no swapchain: with no display
// and no surface extension, create, draw and readback succeed and the pixels
// are the ones drawn.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <optional>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;

TEST_CASE("INV-4: the surfaceless path draws and reads back with no display", "[device]") {
    removeDisplay();
    REQUIRE(std::getenv("DISPLAY") == nullptr);
    REQUIRE(std::getenv("WAYLAND_DISPLAY") == nullptr);

    Config config;
    config.width = 64;
    config.height = 64;
    config.linearOutput = true;
    REQUIRE(config.surface == 0u);
    Renderer renderer = requireRenderer(config);

    // A square in front of the camera wearing no material, unlit: it draws the
    // built-in default, whose base is magenta.
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, 50, "", PF_UNLIT);
    const uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    requireOk(renderer.draw(bundle, Camera{}));

    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    REQUIRE(pixels->size() == 64u * 64u * 4u);
    CHECK(pixelAt(*pixels, 64, 32, 32) == Rgba{255, 0, 255, 255});
    CHECK(pixelAt(*pixels, 64, 1, 1) == Rgba{0, 0, 0, 255});
}

TEST_CASE("another bundle of the same sizes in the same storage is drawn and not the one before", "[device]") {
    // A reload the address and the section sizes alone cannot tell apart: one
    // std::optional re-emplaced with a map of identical shape and other colours.
    removeDisplay();
    Config config;
    config.width = 64;
    config.height = 64;
    config.linearOutput = true;
    Renderer renderer = requireRenderer(config);

    const auto squareWearing = [](const Rgba& colour) {
        uta::ubundle::Geometry geometry;
        addSquare(geometry, 100, 0, 0, 50, "paint", PF_UNLIT);
        uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
        addSolidMaterial(bundle, "paint", colour);
        return bundle;
    };
    const Rgba first{201, 21, 41, 255};
    const Rgba second{21, 201, 41, 255};

    std::optional<uta::ubundle::Bundle> slot;
    slot.emplace(squareWearing(first));
    const uta::ubundle::Bundle* const address = &*slot;
    requireOk(renderer.draw(*slot, Camera{}));
    auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    CHECK(pixelAt(*pixels, 64, 32, 32) == first);

    slot.reset();
    slot.emplace(squareWearing(second));
    REQUIRE(&*slot == address); // the case under test: the same storage
    requireOk(renderer.draw(*slot, Camera{}));
    pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    CHECK(pixelAt(*pixels, 64, 32, 32) == second);
}

TEST_CASE("readback before any frame is refused rather than returning garbage", "[device]") {
    removeDisplay();
    Config config;
    config.width = 8;
    config.height = 8;
    Renderer renderer = requireRenderer(config);
    CHECK_FALSE(renderer.readback().has_value());
}
