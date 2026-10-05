// UTA-0286: fire textures move -- docs/specs/UTA-0286-replayed-fire-textures.md
// SS 4.4, INV-7.
//
// EACH FRAME IS A FRESH RENDERER'S FIRST, as RenderLiquidsTest's are, so two
// draws at one time can be compared byte for byte.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <vector>

using namespace uta::test::render;
using uta::ubundle::FireLook;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;
using uta::urender::Tier;

namespace {

constexpr std::uint32_t WIDTH = 128;
constexpr std::uint32_t HEIGHT = 128;
constexpr float HALF = 60;
constexpr double PINNED = 1.0;
constexpr std::uint8_t SPARKLE = 1;

/// A 32x32 fire whose palette is a grey ramp, with one Sparkle spark in the
/// middle scattering full heat over the whole field.
FireLook sparkling() {
    FireLook look;
    look.size = {32, 32};
    look.renderHeat = 240;
    for (std::size_t i = 0; i < look.palette.size(); ++i) {
        const auto v = static_cast<std::uint8_t>(i);
        look.palette[i] = {v, v, v};
    }
    look.sparks.push_back({.type = SPARKLE, .heat = 255, .x = 16, .y = 16, .byteA = 32, .byteB = 32});
    return look;
}

/// An unlit square at x = 100 wearing material "fire", with the look or not.
/// Its maps are a solid colour, as the bake's still would be.
uta::ubundle::Bundle fireSquare(std::optional<FireLook> look) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, HALF, "fire", PF_UNLIT);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "fire", Rgba{255, 255, 255, 255});
    (*bundle.materials)[0].fire = std::move(look);
    return bundle;
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

TEST_CASE("UTA-0286 INV-7: a fire material is still at one pinned time and moves at another", "[device][fire]") {
    removeDisplay();
    const uta::ubundle::Bundle fire = fireSquare(sparkling());
    const std::vector<std::byte> first = frameAt(fire, PINNED);
    CHECK(changed(first, frameAt(fire, PINNED)) == 0);
    const std::size_t moved = changed(first, frameAt(fire, PINNED + 0.5));
    INFO("pixels changed in half a second " << moved);
    CHECK(moved * 100 >= std::size_t{WIDTH} * HEIGHT); // at least 1% of the frame

    // The same material with no look draws its maps, which do not move.
    const uta::ubundle::Bundle plain = fireSquare(std::nullopt);
    CHECK(changed(frameAt(plain, PINNED), frameAt(plain, PINNED + 0.5)) == 0);
}
