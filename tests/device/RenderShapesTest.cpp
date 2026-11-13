// UTA-0019 SS 4.8: the stand-in shapes -- docs/specs/UTA-0019-weapon-set.md.
//
// A shape is flat, unlit and depth-tested against the level, and the frame
// uses linearOutput, so a drawn shape reads back as exactly its colour, and a
// hidden one leaves the wall exactly as it was. The wall is PF_Unlit, so it
// too reads back as its stored colour.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <utility>
#include <vector>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;
using uta::urender::Shape;
using uta::urender::Tier;

namespace {

constexpr std::uint32_t SIZE = 64;
constexpr std::uint32_t MIDDLE = SIZE / 2;
/// Odd channels throughout: bc7Solid needs one parity across all four.
constexpr Rgba WALL{61, 121, 201, 255};
constexpr Rgba RED{255, 0, 0, 255};

Config frame() {
    Config config;
    config.width = SIZE;
    config.height = SIZE;
    config.linearOutput = true;
    return config;
}

/// A wall filling the view 200 units ahead, which a default Camera faces.
uta::ubundle::Bundle wall() {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 200, 0, 0, 400, "wall", PF_UNLIT);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "wall", WALL);
    return bundle;
}

Shape sphere(float x, float radius, std::array<float, 4> colour = {1, 0, 0, 1}) {
    Shape shape;
    shape.kind = Shape::Kind::Sphere;
    shape.a = {x, 0, 0};
    shape.radius = radius;
    shape.colour = colour;
    return shape;
}

std::vector<std::byte> drawn(Renderer& renderer, const uta::ubundle::Bundle& bundle) {
    requireOk(renderer.draw(bundle, Camera{}));
    auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    REQUIRE(pixels->size() == std::size_t{SIZE} * SIZE * 4);
    return std::move(*pixels);
}

Rgba centre(const std::vector<std::byte>& pixels) { return pixelAt(pixels, SIZE, MIDDLE, MIDDLE); }

bool red(const Rgba& pixel) { return pixel.r > 200 && pixel.g < 40 && pixel.b < 40; }

} // namespace

TEST_CASE("UTA-0019: a sphere before the wall shows its colour and one behind it is hidden", "[device][shapes]") {
    removeDisplay();
    Renderer renderer = requireRenderer(frame());
    const uta::ubundle::Bundle bundle = wall();

    const std::vector<Shape> before{sphere(100, 20)};
    renderer.setShapes(before);
    CHECK(centre(drawn(renderer, bundle)) == RED);

    const std::vector<Shape> behind{sphere(300, 20)};
    renderer.setShapes(behind);
    CHECK(centre(drawn(renderer, bundle)) == WALL);
}

TEST_CASE("UTA-0019: a sphere the wall cuts shows its near half", "[device][shapes]") {
    // Its centre is 10 before the wall, so its near surface is in front and
    // its far surface behind. Culling the wrong faces would draw the far
    // half, which the wall hides.
    removeDisplay();
    Renderer renderer = requireRenderer(frame());
    const std::vector<Shape> cut{sphere(190, 30)};
    renderer.setShapes(cut);
    CHECK(centre(drawn(renderer, wall())) == RED);
}

TEST_CASE("UTA-0019: a shape with alpha below one blends once over the wall", "[device][shapes]") {
    // Half of each in linear light. Drawing both of the sphere's faces would
    // blend it twice: three quarters red.
    removeDisplay();
    Renderer renderer = requireRenderer(frame());
    const std::vector<Shape> faded{sphere(100, 20, {1, 0, 0, 0.5f})};
    renderer.setShapes(faded);
    const Rgba pixel = centre(drawn(renderer, wall()));
    const auto linear = [](std::uint8_t byte) {
        const double c = byte / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    const auto near = [](int got, double want) { return std::abs(got - static_cast<int>(std::lround(want))) <= 1; };
    INFO("pixel " << pixel);
    CHECK(near(pixel.r, srgbByte(0.5 + 0.5 * linear(WALL.r))));
    CHECK(near(pixel.g, srgbByte(0.5 * linear(WALL.g))));
    CHECK(near(pixel.b, srgbByte(0.5 * linear(WALL.b))));
}

TEST_CASE("UTA-0019: no shapes draws the frame that was drawn before any were set", "[device][shapes]") {
    removeDisplay();
    Renderer renderer = requireRenderer(frame());
    renderer.pinLightSeconds(0);
    const uta::ubundle::Bundle bundle = wall();
    (void)drawn(renderer, bundle); // the first frame is not comparable
    const std::vector<std::byte> never = drawn(renderer, bundle);

    const std::vector<Shape> one{sphere(100, 20)};
    renderer.setShapes(one);
    REQUIRE(drawn(renderer, bundle) != never);

    renderer.setShapes({});
    CHECK(drawn(renderer, bundle) == never);
}

TEST_CASE("UTA-0019: an upright cylinder stands its half-height above and below its centre", "[device][shapes]") {
    // At 90 degrees a point z above the axis at distance d is 32 z / d rows
    // from the middle: the near face, 90 away, reaches 30 up -- 10.7 rows.
    removeDisplay();
    Renderer renderer = requireRenderer(frame());
    Shape cylinder;
    cylinder.kind = Shape::Kind::Cylinder;
    cylinder.a = {100, 0, 0};
    cylinder.radius = 10;
    cylinder.halfHeight = 30;
    cylinder.colour = {1, 0, 0, 1};
    const std::vector<Shape> shapes{cylinder};
    renderer.setShapes(shapes);
    const std::vector<std::byte> pixels = drawn(renderer, wall());
    for (const std::uint32_t row : {MIDDLE - 8, MIDDLE, MIDDLE + 8}) {
        INFO("row " << row);
        CHECK(pixelAt(pixels, SIZE, MIDDLE, row) == RED);
    }
    for (const std::uint32_t row : {MIDDLE - 16, MIDDLE + 16}) {
        INFO("row " << row);
        CHECK(pixelAt(pixels, SIZE, MIDDLE, row) == WALL);
    }
    // Narrower than tall: 10 either side is 3.6 columns at the near face.
    CHECK(pixelAt(pixels, SIZE, MIDDLE - 8, MIDDLE) == WALL);
    CHECK(pixelAt(pixels, SIZE, MIDDLE + 8, MIDDLE) == WALL);
}

TEST_CASE("UTA-0019: a far thin line stays visible and stays thin", "[device][shapes]") {
    // 3000 away and 0.01 thick it would cover no pixel at its own size: a
    // pixel spans 94 units there. Nothing is behind it, so every red pixel is the line's.
    removeDisplay();
    Renderer renderer = requireRenderer(frame());
    Shape line;
    line.kind = Shape::Kind::Line;
    line.a = {3000, -600, 0};
    line.b = {3000, 600, 0};
    line.radius = 0.01f;
    line.colour = {1, 0, 0, 1};
    const std::vector<Shape> shapes{line};
    renderer.setShapes(shapes);
    const std::vector<std::byte> pixels = drawn(renderer, bundleOf(uta::ubundle::Geometry{}));
    std::uint32_t reds = 0;
    std::uint32_t lowest = SIZE, highest = 0;
    for (std::uint32_t y = 0; y < SIZE; ++y) {
        for (std::uint32_t x = 0; x < SIZE; ++x) {
            if (!red(pixelAt(pixels, SIZE, x, y))) continue;
            ++reds;
            lowest = std::min(lowest, y);
            highest = std::max(highest, y);
        }
    }
    INFO("red pixels " << reds << " in rows " << lowest << " to " << highest);
    CHECK(red(centre(pixels)));
    CHECK(reds >= 10);
    CHECK(highest - lowest <= 3);
}

TEST_CASE("UTA-0019: a shape draws through a reduced render scale", "[device][shapes]") {
    // The shape is drawn into the region and upscaled with the level.
    removeDisplay();
    Config config = frame();
    config.tier = Tier::Low;
    config.fixedRenderScale = 0.5;
    Renderer renderer = requireRenderer(config);
    const std::vector<Shape> before{sphere(100, 20)};
    renderer.setShapes(before);
    const std::vector<std::byte> pixels = drawn(renderer, wall());
    REQUIRE(renderer.lastFrameStats().renderScale == 0.5);
    INFO("centre " << centre(pixels));
    CHECK(red(centre(pixels)));
    CHECK(red(pixelAt(pixels, SIZE, MIDDLE - 3, MIDDLE + 3)));
    CHECK_FALSE(red(pixelAt(pixels, SIZE, MIDDLE - 12, MIDDLE + 12)));
}
