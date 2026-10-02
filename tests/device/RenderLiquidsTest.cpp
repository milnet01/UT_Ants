// UTA-0105: liquids move -- docs/specs/UTA-0105-shader-liquids.md SS 4.4,
// INV-5 to INV-7.
//
// EACH FRAME IS A FRESH RENDERER'S FIRST, as RenderParallaxTest's are, so no
// frame carries history from the one before and two draws at one time can be
// compared byte for byte.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <vector>

using namespace uta::test::render;
using uta::ubundle::LiquidKind;
using uta::ubundle::LiquidLook;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;
using uta::urender::Tier;

namespace {

constexpr std::uint32_t WIDTH = 128;
constexpr std::uint32_t HEIGHT = 128;
constexpr float HALF = 60;
constexpr double PINNED = 1.0;
const Rgba RED{255, 1, 1, 255}; // bc7Solid wants every channel odd or every one even
const Rgba BLUE{1, 1, 255, 255};

/// A square at x = 100 wearing material "pool": an 8x4 picture of two BC7
/// blocks, red then blue, so a shifted picture moves the edge between them.
uta::ubundle::Bundle stripedSquare(std::optional<LiquidLook> look) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, HALF, "pool", PF_UNLIT);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    uta::ubundle::MaterialRecord record{"pool", false, 0};
    record.liquid = look;
    bundle.materials = std::vector<uta::ubundle::MaterialRecord>{record};
    uta::ubundle::CompressedTexture base;
    base.name = "pool:base";
    base.format = uta::ubundle::BlockFormat::BC7;
    base.width = base.sourceWidth = 8;
    base.height = base.sourceHeight = 4;
    base.mipCount = 1;
    base.blocks = bc7Solid(RED);
    for (const std::byte b : bc7Solid(BLUE)) base.blocks.push_back(b);
    bundle.textures = std::vector<uta::ubundle::CompressedTexture>{std::move(base)};
    return bundle;
}

/// A white square at x = 100 wearing "pool", lit or not, and a light in front
/// of it off to one side, so a tilted normal changes how much it receives.
uta::ubundle::Bundle flatSquare(LiquidLook look, bool lit) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, HALF, "pool", lit ? 0 : PF_UNLIT);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "pool", Rgba{255, 255, 255, 255});
    (*bundle.materials)[0].liquid = look;
    bundle.lights = std::vector{steadyLight({60, 30, 20}, 255, 8)};
    return bundle;
}

LiquidLook wet() {
    LiquidLook look;
    look.kind = LiquidKind::Wet;
    look.amplitude = 128;
    look.frequency = 8;
    look.size = {8, 4};
    return look;
}

LiquidLook ice(std::uint8_t horizontal) {
    LiquidLook look;
    look.kind = LiquidKind::Ice;
    look.panning = 0; // Linear
    look.pan = {horizontal, 128};
    look.size = {8, 4};
    return look;
}

/// The colour frame of `bundle` at light time `seconds`, from a fresh renderer.
std::vector<std::byte> frameAt(const uta::ubundle::Bundle& bundle, double seconds) {
    Config config;
    config.width = WIDTH;
    config.height = HEIGHT;
    config.linearOutput = true;
    config.tier = Tier::Low;
    config.hazeScale = 0;
    Renderer renderer = requireRenderer(config);
    renderer.pinLightSeconds(seconds);
    requireOk(renderer.draw(bundle, Camera{}));
    auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    return std::move(*pixels);
}

/// How many pixels differ between two frames.
std::size_t changed(const std::vector<std::byte>& a, const std::vector<std::byte>& b) {
    REQUIRE(a.size() == b.size());
    std::size_t count = 0;
    for (std::size_t i = 0; i < a.size(); i += 4)
        count += a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2];
    return count;
}

} // namespace

TEST_CASE("UTA-0105 INV-5: a pinned liquid is still and a moving one moves", "[device][liquids]") {
    removeDisplay();
    const uta::ubundle::Bundle pool = stripedSquare(wet());
    const std::vector<std::byte> first = frameAt(pool, PINNED);
    CHECK(changed(first, frameAt(pool, PINNED)) == 0);
    const std::size_t moved = changed(first, frameAt(pool, PINNED + 0.25));
    INFO("pixels changed in a quarter second " << moved);
    CHECK(moved * 100 >= std::size_t{WIDTH} * HEIGHT); // at least 1% of the frame

    // The same picture with no look does not move.
    const uta::ubundle::Bundle plain = stripedSquare(std::nullopt);
    CHECK(changed(frameAt(plain, PINNED), frameAt(plain, PINNED + 0.25)) == 0);
}

TEST_CASE("UTA-0105 INV-6: an Ice look pans from a zero point of 128", "[device][liquids]") {
    removeDisplay();
    const uta::ubundle::Bundle still = stripedSquare(ice(128));
    CHECK(changed(frameAt(still, PINNED), frameAt(still, PINNED + 0.25)) == 0);
    const uta::ubundle::Bundle panning = stripedSquare(ice(192));
    const std::size_t moved = changed(frameAt(panning, PINNED), frameAt(panning, PINNED + 0.25));
    INFO("pixels changed in a quarter second " << moved);
    CHECK(moved > 0);
}

TEST_CASE("UTA-0105 INV-7: the ripple tilts the light on a lit surface only", "[device][liquids]") {
    // A flat picture hides the warp, so only the tilt can change these frames.
    removeDisplay();
    const uta::ubundle::Bundle lit = flatSquare(wet(), true);
    const std::size_t litMoved = changed(frameAt(lit, PINNED), frameAt(lit, PINNED + 0.25));
    INFO("lit pixels changed in a quarter second " << litMoved);
    CHECK(litMoved * 100 >= std::size_t{WIDTH} * HEIGHT);
    const uta::ubundle::Bundle unlit = flatSquare(wet(), false);
    CHECK(changed(frameAt(unlit, PINNED), frameAt(unlit, PINNED + 0.25)) == 0);
}
