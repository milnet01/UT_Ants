// UTA-0275: a texture's DetailTexture is multiplied into it near the camera,
// as twice its grey on displayed values, and fades out with distance.
//
// The checks hold whatever DETAIL_FAR is fitted to: the weight falls linearly
// from the eye, so the near square, at 16 units, takes nearly all of it; the
// far one sits well past where the original shows any detail.
//
// EACH FRAME IS A FRESH RENDERER'S FIRST, as RenderLiquidsTest's are.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <vector>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;
using uta::urender::Tier;

namespace {

constexpr std::uint32_t SIZE = 64;
constexpr float NEAR_DISTANCE = 16;
constexpr float FAR_DISTANCE = 8000;
const Rgba GREY{101, 101, 101, 255};

/// An unlit grey square filling the view at `distance`, wearing "wall"; with
/// `detail`, a one-level 4x4 BC4 detail of that grey throughout.
uta::ubundle::Bundle wall(float distance, std::optional<std::uint8_t> detail) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, distance, 0, 0, distance * 0.8f, "wall", PF_UNLIT);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "wall", GREY);
    if (detail) {
        uta::ubundle::CompressedTexture d;
        d.name = "wall:detail";
        d.format = uta::ubundle::BlockFormat::BC4;
        d.width = d.sourceWidth = 4;
        d.height = d.sourceHeight = 4;
        d.mipCount = 1;
        // Both endpoints the grey and every index 0: each texel is it exactly.
        d.blocks = {std::byte{*detail}, std::byte{*detail}, std::byte{0}, std::byte{0},
                    std::byte{0},       std::byte{0},       std::byte{0}, std::byte{0}};
        bundle.textures->push_back(std::move(d));
    }
    return bundle;
}

/// The centre pixel's red, from a fresh renderer's first frame.
int centre(const uta::ubundle::Bundle& bundle) {
    Config config;
    config.width = SIZE;
    config.height = SIZE;
    config.linearOutput = true;
    config.tier = Tier::Low;
    config.hazeScale = 0;
    Renderer renderer = requireRenderer(config);
    requireOk(renderer.draw(bundle, Camera{}));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    return pixelAt(*pixels, SIZE, SIZE / 2, SIZE / 2).r;
}

} // namespace

TEST_CASE("UTA-0275: a mid-grey detail leaves a near surface as it was", "[device][detail]") {
    removeDisplay();
    const int plain = centre(wall(NEAR_DISTANCE, std::nullopt));
    INFO("plain " << plain);
    CHECK(std::abs(centre(wall(NEAR_DISTANCE, 128)) - plain) <= 1);
}

TEST_CASE("UTA-0275: a dark detail darkens and a bright one brightens a near surface", "[device][detail]") {
    removeDisplay();
    const int plain = centre(wall(NEAR_DISTANCE, std::nullopt));
    const int dark = centre(wall(NEAR_DISTANCE, 64));
    const int bright = centre(wall(NEAR_DISTANCE, 192));
    INFO("plain " << plain << " dark " << dark << " bright " << bright);
    CHECK(dark < plain - 10);
    CHECK(bright > plain + 10);
}

TEST_CASE("UTA-0275: a far surface shows no detail", "[device][detail]") {
    removeDisplay();
    const int plain = centre(wall(FAR_DISTANCE, std::nullopt));
    INFO("plain " << plain);
    CHECK(std::abs(centre(wall(FAR_DISTANCE, 64)) - plain) <= 1);
}
