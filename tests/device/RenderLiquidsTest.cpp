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
///
/// UTA-0089 SS 4.2: the ripple tilts an unlit liquid's reflection too. So the
/// unlit square has probes of white all round, and reflects the white it is:
/// only a tilt of its own colour can change it.
uta::ubundle::Bundle flatSquare(LiquidLook look, bool lit) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, HALF, "pool", lit ? 0 : PF_UNLIT);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "pool", Rgba{255, 255, 255, 255});
    (*bundle.materials)[0].liquid = look;
    bundle.lights = std::vector{steadyLight({60, 30, 20}, 255, 8)};
    if (!lit) {
        bundle.lightProbes.emplace();
        bundle.lightProbes->spacing = 128;
        for (int z = -1; z <= 0; ++z)
            for (int y = -1; y <= 0; ++y)
                for (int x = 0; x <= 1; ++x) {
                    uta::ubundle::LightProbe probe;
                    probe.cell = {x, y, z};
                    for (auto& face : probe.cube) face = {1.0f, 1.0f, 1.0f};
                    bundle.lightProbes->probes.push_back(probe);
                }
    }
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

// -- UTA-0270: the Ice look's glass --

namespace {

/// A square wearing "ice": a 16x4 picture of four BC7 blocks, red then three
/// blue, so a shift either way puts the red block somewhere else; with
/// `glass`, a one-level BC4 glass of that grey throughout.
uta::ubundle::Bundle icePicture(std::optional<LiquidLook> look, std::optional<std::uint8_t> glass,
                                int redBlock = 0) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, HALF, "ice", PF_UNLIT);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    uta::ubundle::MaterialRecord record{"ice", false, 0};
    record.liquid = look;
    bundle.materials = std::vector<uta::ubundle::MaterialRecord>{record};
    uta::ubundle::CompressedTexture base;
    base.name = "ice:base";
    base.format = uta::ubundle::BlockFormat::BC7;
    base.width = base.sourceWidth = 16;
    base.height = base.sourceHeight = 4;
    base.mipCount = 1;
    for (int b = 0; b < 4; ++b)
        for (const std::byte x : bc7Solid(b == redBlock ? RED : BLUE)) base.blocks.push_back(x);
    std::vector<uta::ubundle::CompressedTexture> textures{std::move(base)};
    if (glass) {
        uta::ubundle::CompressedTexture g;
        g.name = "ice:glass";
        g.format = uta::ubundle::BlockFormat::BC4;
        g.width = g.sourceWidth = 16;
        g.height = g.sourceHeight = 4;
        g.mipCount = 1;
        for (int b = 0; b < 4; ++b) {
            // Both endpoints the grey and every index 0: each texel is it exactly.
            g.blocks.push_back(std::byte{*glass});
            g.blocks.push_back(std::byte{*glass});
            for (int i = 0; i < 6; ++i) g.blocks.push_back(std::byte{0});
        }
        textures.push_back(std::move(g));
    }
    bundle.textures = std::move(textures);
    return bundle;
}

LiquidLook iceLook(std::uint8_t horizontal, bool moveIce) {
    LiquidLook look = ice(horizontal);
    look.size = {16, 4};
    look.moveIce = moveIce ? 1 : 0;
    return look;
}

} // namespace

TEST_CASE("UTA-0270: the source is read glass minus 46 texels along u", "[device][liquids]") {
    removeDisplay();
    // Glass 50 throughout: each texel shows the source 4 texels on, so the red
    // block moves from the first to the last. Still: pan 128, glass uniform.
    const auto shifted = frameAt(icePicture(iceLook(128, true), 50), PINNED);
    const auto expected = frameAt(icePicture(std::nullopt, std::nullopt, 3), PINNED);
    const std::size_t off = changed(shifted, expected);
    INFO("pixels unlike the moved picture " << off);
    CHECK(off * 100 <= std::size_t{WIDTH} * HEIGHT);
    // Glass 46: no shift.
    CHECK(changed(frameAt(icePicture(iceLook(128, true), 46), PINNED),
                  frameAt(icePicture(std::nullopt, std::nullopt, 0), PINNED)) * 100
          <= std::size_t{WIDTH} * HEIGHT);
}

TEST_CASE("UTA-0270: without MoveIce the source slides toward plus u at 0.9 texels a second a unit", "[device][liquids]") {
    removeDisplay();
    // Pan 138: 9 texels a second. In 4/9 s the picture moves 4 texels on, so
    // the red block is the second; read 4 texels back, each texel shows it.
    const auto moved = frameAt(icePicture(iceLook(138, false), 46), 4.0 / 9.0);
    const auto expected = frameAt(icePicture(std::nullopt, std::nullopt, 1), PINNED);
    const std::size_t off = changed(moved, expected);
    INFO("pixels unlike the moved picture " << off);
    CHECK(off * 100 <= std::size_t{WIDTH} * HEIGHT);
}

TEST_CASE("UTA-0270: with MoveIce the source stays and only the glass slides", "[device][liquids]") {
    removeDisplay();
    // A uniform glass sliding changes nothing.
    const auto bundle = icePicture(iceLook(138, true), 46);
    CHECK(changed(frameAt(bundle, PINNED), frameAt(bundle, PINNED + 0.3)) * 100 <= std::size_t{WIDTH} * HEIGHT);
}

