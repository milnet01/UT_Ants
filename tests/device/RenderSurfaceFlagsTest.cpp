// UTA-0014 INV-11 -- docs/specs/UTA-0014-vulkan-draw-path.md SS 4.5.
//
// A PF_Masked batch cuts out below the threshold; a PF_TwoSided batch renders
// from both sides; a PF_Translucent batch writes no motion vector; a PF_Portal
// batch draws nothing; and a batch carrying a bit SS 4.5's table does not name
// renders exactly as it would without it.
//
// THE COMBINED CASES ARE THE POINT. A flag word compared for equality rather
// than tested bit by bit makes PF_Masked | PF_TwoSided match neither case, so
// those two squares are what reject that implementation.
//
// Every square is PF_Unlit and the frame uses linearOutput, so a drawn square
// reads back as exactly its stored colour and an absent one as the clear.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;

namespace {

constexpr std::uint32_t WIDTH = 160;
constexpr std::uint32_t HEIGHT = 64;

std::uint32_t columnOf(float y) { return static_cast<std::uint32_t>(80.0f + y * 0.32f); }

/// Alpha 255, 160 and 100: SS 4.5's threshold is 0.5, so 160 (0.63) is kept
/// and 100 (0.39) is cut. Either side of it, so a threshold moved far enough to
/// matter moves one of the two.
constexpr Rgba SOLID{201, 121, 61, 255};
constexpr Rgba KEPT{200, 120, 60, 160};
constexpr Rgba CUT{200, 120, 60, 100};
constexpr Rgba CLEAR{0, 0, 0, 255};

/// What a drawn square reads back as: the output stage writes alpha 1.
Rgba drawn(Rgba colour) { return {colour.r, colour.g, colour.b, 255}; }

Config smallFrame() {
    Config config;
    config.width = WIDTH;
    config.height = HEIGHT;
    config.linearOutput = true;
    return config;
}

} // namespace

TEST_CASE("INV-11: masking and two-sidedness and portals are honoured bit by bit", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(smallFrame());

    struct Square {
        const char* what;
        const char* material;
        std::uint32_t flags;
        bool facingAway;
        Rgba expected;
    };
    const std::vector<Square> squares = {
        {"opaque", "solid", 0, false, drawn(SOLID)},
        {"masked below the threshold", "cut", PF_MASKED, false, CLEAR},
        {"masked above the threshold", "kept", PF_MASKED, false, drawn(KEPT)},
        {"two-sided seen from behind", "solid", PF_TWO_SIDED, true, drawn(SOLID)},
        {"one-sided seen from behind", "solid", 0, true, CLEAR},
        {"a portal", "solid", PF_PORTAL, false, CLEAR},
        {"masked and two-sided below the threshold from behind", "cut", PF_MASKED | PF_TWO_SIDED, true, CLEAR},
        {"masked and two-sided above the threshold from behind", "kept", PF_MASKED | PF_TWO_SIDED, true, drawn(KEPT)},
        {"carrying PF_Modulated which SS 4.5 ignores", "solid", PF_MODULATED, false, drawn(SOLID)},
    };

    uta::ubundle::Geometry geometry;
    for (std::size_t i = 0; i < squares.size(); ++i) {
        const float y = -200.0f + 50.0f * static_cast<float>(i);
        addSquare(geometry, 100, y, 0, 15, squares[i].material, squares[i].flags | PF_UNLIT, squares[i].facingAway);
    }
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "cut", CUT);
    addSolidMaterial(bundle, "kept", KEPT);
    addSolidMaterial(bundle, "solid", SOLID);

    requireOk(renderer.draw(bundle, Camera{}));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());

    for (std::size_t i = 0; i < squares.size(); ++i) {
        CAPTURE(squares[i].what);
        const float y = -200.0f + 50.0f * static_cast<float>(i);
        CHECK(pixelAt(*pixels, WIDTH, columnOf(y), HEIGHT / 2) == squares[i].expected);
    }
    // The ignored bit renders EXACTLY as the batch without it.
    CHECK(pixelAt(*pixels, WIDTH, columnOf(-200.0f + 50.0f * 8), HEIGHT / 2)
          == pixelAt(*pixels, WIDTH, columnOf(-200.0f), HEIGHT / 2));
}

TEST_CASE("INV-11: a translucent batch writes no motion vector", "[device]") {
    removeDisplay();
    Renderer renderer = requireRenderer(smallFrame());

    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, -100, 0, 30, "solid", PF_UNLIT);
    addSquare(geometry, 100, 100, 0, 30, "solid", PF_UNLIT | PF_TRANSLUCENT);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "solid", SOLID);

    // Two frames with the camera moved sideways, so everything that writes a
    // motion vector writes a non-zero one.
    requireOk(renderer.draw(bundle, Camera{}));
    Camera moved;
    moved.location = {0, 5, 0};
    requireOk(renderer.draw(bundle, moved));

    const auto colour = renderer.readback(Renderer::Target::Colour);
    if (!colour.has_value()) FAIL(colour.error().message());
    // Both squares drew -- so a zero vector below is not a square that is absent.
    // Translucency over black adds its own colour, which is the colour itself.
    CHECK(pixelAt(*colour, WIDTH, columnOf(-100), HEIGHT / 2) == drawn(SOLID));
    CHECK(pixelAt(*colour, WIDTH, columnOf(100), HEIGHT / 2) == drawn(SOLID));

    const auto velocity = renderer.readback(Renderer::Target::Velocity);
    if (!velocity.has_value()) FAIL(velocity.error().message());
    REQUIRE(velocity->size() == WIDTH * HEIGHT * 2 * sizeof(float));
    const auto opaque = velocityAt(*velocity, WIDTH, columnOf(-100), HEIGHT / 2);
    const auto translucent = velocityAt(*velocity, WIDTH, columnOf(100), HEIGHT / 2);
    CAPTURE(opaque[0], opaque[1], translucent[0], translucent[1]);
    // Five units sideways at a hundred is a fiftieth of the half-width: a
    // hundredth of the target in UV.
    CHECK(std::abs(opaque[0]) > 0.005f);
    CHECK(translucent[0] == 0.0f);
    CHECK(translucent[1] == 0.0f);
}

TEST_CASE("a camera that did not move produces zero motion", "[device]") {
    // SS 4.3: the previous frame's view is cached here, not supplied.
    removeDisplay();
    Renderer renderer = requireRenderer(smallFrame());
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, 30, "", PF_UNLIT);
    const uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    requireOk(renderer.draw(bundle, Camera{}));
    requireOk(renderer.draw(bundle, Camera{}));
    const auto velocity = renderer.readback(Renderer::Target::Velocity);
    if (!velocity.has_value()) FAIL(velocity.error().message());
    const auto v = velocityAt(*velocity, WIDTH, columnOf(0), HEIGHT / 2);
    CHECK(v[0] == 0.0f);
    CHECK(v[1] == 0.0f);
}
