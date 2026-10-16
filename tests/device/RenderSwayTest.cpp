// UTA-0055 SS 4.5: scene.vert moves a swaying vertex by its kind and weight,
// on the light clock, and the clock's wrap moves nothing.
//
// docs/specs/UTA-0055-swaying-cloth-and-water.md INV-7 and INV-8.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

using namespace uta::test::render;
using uta::ubundle::SwayKind;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;
using uta::urender::Tier;

namespace {

constexpr std::uint32_t WIDTH = 128;
constexpr std::uint32_t HEIGHT = 128;
constexpr float HALF = 60;
constexpr double PINNED = 1.0;
constexpr double WRAP = 4096.0; // Frame.cpp's FLAME_CLOCK_WRAP
const Rgba RED{255, 1, 1, 255}; // bc7Solid wants every channel odd or every one even
const Rgba BLUE{1, 1, 255, 255};

/// A square at x = 100 wearing an 8x4 picture, red then blue, so a moved
/// corner moves the edge between them; every corner swaying `kind` at
/// `weight`.
uta::ubundle::Bundle swayingSquare(SwayKind kind, std::uint8_t weight) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, HALF, "banner", PF_UNLIT);
    for (auto& vertex : geometry.vertices) {
        vertex.sway = kind;
        vertex.swayWeight = weight;
        vertex.swayCell = kind == SwayKind::None ? 0 : 2; // 32 units, as baked
    }
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    bundle.materials = std::vector<uta::ubundle::MaterialRecord>{{"banner", false, 0}};
    uta::ubundle::CompressedTexture base;
    base.name = "banner:base";
    base.format = uta::ubundle::BlockFormat::BC7;
    base.width = base.sourceWidth = 8;
    base.height = base.sourceHeight = 4;
    base.mipCount = 1;
    base.blocks = bc7Solid(RED);
    for (const std::byte b : bc7Solid(BLUE)) base.blocks.push_back(b);
    bundle.textures = std::vector<uta::ubundle::CompressedTexture>{std::move(base)};
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

/// The summed absolute change over every colour channel of two frames.
std::uint64_t difference(const std::vector<std::byte>& a, const std::vector<std::byte>& b) {
    REQUIRE(a.size() == b.size());
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < a.size(); i += 4)
        for (std::size_t c = 0; c < 3; ++c)
            sum += static_cast<std::uint64_t>(std::abs(std::to_integer<int>(a[i + c]) - std::to_integer<int>(b[i + c])));
    return sum;
}

/// How much `bundle`'s frame changes over a quarter second.
std::uint64_t moved(const uta::ubundle::Bundle& bundle) {
    return difference(frameAt(bundle, PINNED), frameAt(bundle, PINNED + 0.25));
}

} // namespace

TEST_CASE("UTA-0055 INV-7: a swaying surface moves and a still one does not", "[device][sway]") {
    removeDisplay();
    for (const SwayKind kind : {SwayKind::Cloth, SwayKind::Water}) {
        INFO("kind " << static_cast<int>(kind));
        CHECK(moved(swayingSquare(kind, 255)) > 0);
        // Weight 0 is pinned.
        CHECK(moved(swayingSquare(kind, 0)) == 0);
    }
    CHECK(moved(swayingSquare(SwayKind::None, 0)) == 0);
}

TEST_CASE("UTA-0055 INV-8: the light clock's wrap moves a swaying surface no more than a moment does", "[device][sway]") {
    removeDisplay();
    for (const SwayKind kind : {SwayKind::Cloth, SwayKind::Water}) {
        INFO("kind " << static_cast<int>(kind));
        const uta::ubundle::Bundle square = swayingSquare(kind, 255);
        // A millisecond before the wrap and the wrap itself, against a quarter
        // second's motion. A wave that is not a whole number of cycles per
        // wrap jumps here by up to half a cycle.
        const std::uint64_t acrossWrap = difference(frameAt(square, WRAP - 0.001), frameAt(square, WRAP));
        const std::uint64_t quarter = moved(square);
        INFO("across the wrap " << acrossWrap << ", over a quarter second " << quarter);
        REQUIRE(quarter > 0);
        CHECK(acrossWrap * 20 < quarter);
    }
}
