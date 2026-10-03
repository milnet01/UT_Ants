// UTA-0014 INV-11 and INV-12 -- docs/specs/UTA-0014-vulkan-draw-path.md SS 4.5.
//
// A PF_Masked batch cuts out below the threshold; a PF_TwoSided batch renders
// from both sides; a PF_Translucent batch writes no motion vector; a PF_Portal
// batch IS drawn and a PF_Portal | PF_Invisible one is not; and a batch
// carrying a bit SS 4.5's table does not name renders exactly as it would
// without it.
//
// THE PORTAL PAIR IS UTA-0188's, and the two rows are one case. This file used
// to assert that a portal drew nothing, which was the decision that lost
// AS-Frigate's sea: in UT99 the polygon dividing an air zone from a water zone
// IS the water. One row alone cannot grade the repair -- asserting only that a
// portal draws would pass an implementation that ignored the flags entirely --
// so the invisible partner is what says the remaining guard still holds.
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
#include <utility>
#include <vector>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;

namespace {

constexpr std::uint32_t WIDTH = 160;
constexpr std::uint32_t HEIGHT = 64;

std::uint32_t columnOf(float y) { return static_cast<std::uint32_t>(80.0f + y * 0.32f); }

/// Where square `i` sits, derived once rather than spelled at each use.
///
/// UTA-0188 added a tenth square and the old layout could not hold it: the
/// spacing was written out at three call sites with the last index hard-coded,
/// so the new row pushed the final square off a 160-wide frame and the closing
/// comparison silently named a different square. columnOf maps y to
/// 80 + 0.32y, so the last square at -200 + 45 * 9 = 205 lands at column 146 --
/// inside the frame with its 15-unit half-width to spare.
constexpr float FIRST_Y = -200.0f;
constexpr float SPACING_Y = 45.0f;
float squareY(std::size_t i) { return FIRST_Y + SPACING_Y * static_cast<float>(i); }

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
        {"a portal", "solid", PF_PORTAL, false, drawn(SOLID)},
        {"a portal that is also invisible", "solid", PF_PORTAL | PF_INVISIBLE, false, CLEAR},
        {"masked and two-sided below the threshold from behind", "cut", PF_MASKED | PF_TWO_SIDED, true, CLEAR},
        {"masked and two-sided above the threshold from behind", "kept", PF_MASKED | PF_TWO_SIDED, true, drawn(KEPT)},
        {"carrying PF_NoSmooth which SS 4.5 ignores", "solid", PF_NO_SMOOTH, false, drawn(SOLID)},
    };

    uta::ubundle::Geometry geometry;
    for (std::size_t i = 0; i < squares.size(); ++i)
        addSquare(geometry, 100, squareY(i), 0, 15, squares[i].material, squares[i].flags | PF_UNLIT,
                  squares[i].facingAway);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "cut", CUT);
    addSolidMaterial(bundle, "kept", KEPT);
    addSolidMaterial(bundle, "solid", SOLID);

    requireOk(renderer.draw(bundle, Camera{}));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());

    for (std::size_t i = 0; i < squares.size(); ++i) {
        CAPTURE(squares[i].what);
        CHECK(pixelAt(*pixels, WIDTH, columnOf(squareY(i)), HEIGHT / 2) == squares[i].expected);
    }
    // The ignored bit renders EXACTLY as the batch without it. Both ends are
    // taken from the table rather than written out, so a row added anywhere
    // keeps this comparing PF_NoSmooth against the plain opaque square. It was
    // PF_Modulated until UTA-0271 gave that bit a meaning (INV-12 below).
    CHECK(pixelAt(*pixels, WIDTH, columnOf(squareY(squares.size() - 1)), HEIGHT / 2)
          == pixelAt(*pixels, WIDTH, columnOf(squareY(0)), HEIGHT / 2));
}

TEST_CASE("UTA-0014 INV-12: a modulated surface multiplies what lies behind it by twice its displayed colour",
          "[device]") {
    // UT99 draws PF_Modulated as dst * src * 2 on displayed values, so a
    // mid-grey square leaves the wall nearly as it was and a darker one darkens
    // it: 2 * 129/255 * 201 = 203.4 and 2 * 65/255 * 201 = 102.5 of 255. Ours
    // multiplies linear light by (2 * displayed)^2.2, which lands within a level
    // of each. Drawn opaque, as before UTA-0271, the squares read 129 and 65.
    // Odd channels throughout: bc7Solid needs one parity across all four.
    // A third square carries PF_Translucent as well, which wins as in UT99's
    // renderers: it adds, 0.053 + 0.584 * (1 - 0.053) of linear light, 204 of
    // 255, where a modulated one would read 102.
    removeDisplay();
    Renderer renderer = requireRenderer(smallFrame());
    constexpr Rgba WALL{201, 201, 201, 255};
    constexpr Rgba NEUTRAL{129, 129, 129, 255};
    constexpr Rgba DARK{65, 65, 65, 255};

    uta::ubundle::Geometry geometry;
    addSquare(geometry, 200, 0, 0, 400, "wall", PF_UNLIT);
    addSquare(geometry, 100, -60, 0, 15, "neutral", PF_MODULATED | PF_UNLIT);
    addSquare(geometry, 100, 60, 0, 15, "dark", PF_MODULATED | PF_UNLIT);
    addSquare(geometry, 100, 120, 0, 15, "dark", PF_TRANSLUCENT | PF_MODULATED | PF_UNLIT);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "wall", WALL);
    addSolidMaterial(bundle, "neutral", NEUTRAL);
    addSolidMaterial(bundle, "dark", DARK);

    requireOk(renderer.draw(bundle, Camera{}));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    const Rgba wall = pixelAt(*pixels, WIDTH, columnOf(0), HEIGHT / 2);
    const Rgba neutral = pixelAt(*pixels, WIDTH, columnOf(-60), HEIGHT / 2);
    const Rgba dark = pixelAt(*pixels, WIDTH, columnOf(60), HEIGHT / 2);
    const Rgba both = pixelAt(*pixels, WIDTH, columnOf(120), HEIGHT / 2);
    CAPTURE(wall, neutral, dark, both);
    CHECK(wall == WALL);
    CHECK(std::abs(int(neutral.r) - 203) <= 3);
    CHECK(std::abs(int(dark.r) - 102) <= 3);
    CHECK(std::abs(int(both.r) - 204) <= 3);
}

TEST_CASE("UTA-0269: a batch's pan rate slides its texture with the light clock", "[device]") {
    // An 8 by 4 texture, red on its left half and green on its right, across
    // a square whose u runs 0 to 1 left to right. A quarter repeat a second
    // in a camera zone of u speed 2 (UTA-0276) is half a repeat a second: at
    // second 1 a point a quarter across shows what lay three quarters across,
    // and at second 2 the whole repeat has passed. Odd channels: bc7Solid.
    removeDisplay();
    Renderer renderer = requireRenderer(smallFrame());
    constexpr Rgba LEFT{201, 31, 31, 255};
    constexpr Rgba RIGHT{31, 201, 31, 255};

    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, 30, "halves", PF_UNLIT);
    geometry.batches.back().panRate = {0.25F, 0.0F};
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    bundle.zones = std::vector<uta::ubundle::Zone>{{0, 0, 0, 0, {2.0F, 1.0F}}};
    bundle.materials.emplace();
    bundle.textures.emplace();
    bundle.materials->push_back({"halves", false});
    uta::ubundle::CompressedTexture texture;
    texture.name = "halves:base";
    texture.format = uta::ubundle::BlockFormat::BC7;
    texture.width = texture.sourceWidth = 8;
    texture.height = texture.sourceHeight = 4;
    texture.mipCount = 1;
    texture.blocks = bc7Solid(LEFT);
    const std::vector<std::byte> right = bc7Solid(RIGHT);
    texture.blocks.insert(texture.blocks.end(), right.begin(), right.end());
    bundle.textures->push_back(std::move(texture));

    const auto quarters = [&](double seconds) {
        renderer.pinLightSeconds(seconds);
        requireOk(renderer.draw(bundle, Camera{}));
        const auto pixels = renderer.readback();
        if (!pixels.has_value()) FAIL(pixels.error().message());
        return std::pair{pixelAt(*pixels, WIDTH, columnOf(-15), HEIGHT / 2),
                         pixelAt(*pixels, WIDTH, columnOf(15), HEIGHT / 2)};
    };
    const auto [still, stillRight] = quarters(0);
    const auto [half, halfRight] = quarters(1);
    const auto [whole, wholeRight] = quarters(2);
    CAPTURE(still, stillRight, half, halfRight, whole, wholeRight);
    CHECK(still == LEFT);
    CHECK(stillRight == RIGHT);
    CHECK(half == RIGHT);
    CHECK(halfRight == LEFT);
    CHECK(whole == LEFT);
    CHECK(wholeRight == RIGHT);
}

TEST_CASE("UTA-0252: a sky surface hides what lies behind it", "[device]") {
    // AS-Frigate's sky room sits above the level's sky ceiling. With the sky's
    // depth written at the far plane the room passed the depth test and drew
    // over the sky. The far square is twice as far and twice as far off-axis, so
    // it lands inside the near one's outline, and is
    // added both before and after it so draw order cannot decide the pixel.
    removeDisplay();
    Renderer renderer = requireRenderer(smallFrame());
    constexpr Rgba BEHIND{41, 81, 161, 255};

    uta::ubundle::Geometry geometry;
    addSquare(geometry, 200, -200, 0, 40, "behind", PF_UNLIT);
    addSquare(geometry, 100, -100, 0, 30, "solid", PF_FAKE_BACKDROP);
    addSquare(geometry, 100, 100, 0, 30, "solid", PF_FAKE_BACKDROP);
    addSquare(geometry, 200, 200, 0, 40, "behind", PF_UNLIT);
    // In front of the sky, so a sky that hid everything would fail here.
    addSquare(geometry, 50, 0, 0, 5, "behind", PF_UNLIT);
    addSquare(geometry, 100, 0, 0, 30, "solid", PF_FAKE_BACKDROP);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "behind", BEHIND);
    addSolidMaterial(bundle, "solid", SOLID);

    requireOk(renderer.draw(bundle, Camera{}));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    // No SkyZoneInfo: the sky surface shows its own texture, unlit.
    CHECK(pixelAt(*pixels, WIDTH, columnOf(-100), HEIGHT / 2) == drawn(SOLID));
    CHECK(pixelAt(*pixels, WIDTH, columnOf(100), HEIGHT / 2) == drawn(SOLID));
    CHECK(pixelAt(*pixels, WIDTH, columnOf(0), HEIGHT / 2) == drawn(BEHIND));
}

TEST_CASE("UTA-0281: a sky is drawn without its relief", "[device]") {
    // A lit white square in a sky zone, seen through a sky surface filling the
    // view. Once its normal map leans hard to one side and once it has none:
    // with a light off to that side, relief would light the two differently.
    // A sky is painted light and depth, so the window shows them alike.
    removeDisplay();
    const auto skyWindow = [](bool relief) {
        uta::ubundle::Geometry geometry;
        addSquare(geometry, 100, 0, 0, 60, "window", PF_FAKE_BACKDROP);
        addSquare(geometry, 10100, 0, 0, 300, "cloud", 0); // seen from the sky's view at x = 10000
        uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
        addSolidMaterial(bundle, "window", SOLID);
        addSolidMaterial(bundle, "cloud", Rgba{255, 255, 255, 255});
        if (relief) {
            uta::ubundle::CompressedTexture normal;
            normal.name = "cloud:normal";
            normal.format = uta::ubundle::BlockFormat::BC5;
            normal.width = normal.height = normal.sourceWidth = normal.sourceHeight = 4;
            normal.mipCount = 1;
            normal.blocks = bc5Solid(230, 128);
            bundle.textures->push_back(std::move(normal));
        }
        bundle.lights = std::vector{steadyLight({10060, 0, 0}, 255, 8)};
        uta::ubundle::Placements placements;
        placements.classes = {{.path = "engine.skyzoneinfo", .ancestry = {"engine.zoneinfo", "engine.info"}}};
        placements.actors = {{.exportIndex = 1,
                              .classIndex = 0,
                              .properties = {{.name = "Location",
                                              .kind = uta::ubundle::ValueKind::Vector,
                                              .value = std::array<float, 3>{10000, 0, 0}}}}};
        bundle.placements = std::move(placements);
        return bundle;
    };
    const auto centre = [](const uta::ubundle::Bundle& bundle) {
        Renderer renderer = requireRenderer(smallFrame());
        requireOk(renderer.draw(bundle, Camera{}));
        const auto pixels = renderer.readback();
        if (!pixels.has_value()) FAIL(pixels.error().message());
        return pixelAt(*pixels, WIDTH, WIDTH / 2, HEIGHT / 2);
    };
    const Rgba flat = centre(skyWindow(false));
    const Rgba leaning = centre(skyWindow(true));
    CAPTURE(flat, leaning);
    CHECK(flat != drawn(SOLID)); // the window shows the sky, not its own texture
    CHECK(std::abs(int(flat.r) - int(leaning.r)) <= 1);
    CHECK(std::abs(int(flat.g) - int(leaning.g)) <= 1);
    CHECK(std::abs(int(flat.b) - int(leaning.b)) <= 1);
}

TEST_CASE("UTA-0260: a masked surface's holes show what lies behind it", "[device]") {
    // The depth pass draws the opaque surfaces ahead of the forward pass. A
    // masked surface's texels below the threshold are holes, so its depth must
    // not be drawn ahead: it would hide the square seen through them. Each far
    // square is twice as far and twice as far off-axis, so it lands inside the
    // near one's outline, once added before it and once after.
    removeDisplay();
    Renderer renderer = requireRenderer(smallFrame());
    constexpr Rgba BEHIND{41, 81, 161, 255};

    uta::ubundle::Geometry geometry;
    addSquare(geometry, 200, -200, 0, 40, "behind", PF_UNLIT);
    addSquare(geometry, 100, -100, 0, 30, "cut", PF_MASKED | PF_UNLIT);
    addSquare(geometry, 100, 100, 0, 30, "cut", PF_MASKED | PF_UNLIT);
    addSquare(geometry, 200, 200, 0, 40, "behind", PF_UNLIT);
    // A masked texel above the threshold still hides what is behind it, and an
    // opaque surface in front of one still hides it.
    addSquare(geometry, 200, 0, 0, 40, "behind", PF_UNLIT);
    addSquare(geometry, 100, 0, 0, 30, "kept", PF_MASKED | PF_UNLIT);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "behind", BEHIND);
    addSolidMaterial(bundle, "cut", CUT);
    addSolidMaterial(bundle, "kept", KEPT);

    requireOk(renderer.draw(bundle, Camera{}));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    CHECK(pixelAt(*pixels, WIDTH, columnOf(-100), HEIGHT / 2) == drawn(BEHIND));
    CHECK(pixelAt(*pixels, WIDTH, columnOf(100), HEIGHT / 2) == drawn(BEHIND));
    CHECK(pixelAt(*pixels, WIDTH, columnOf(0), HEIGHT / 2) == drawn(KEPT));
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
