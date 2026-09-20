// UTA-0192 -- docs/specs/UTA-0014-vulkan-draw-path.md SS 4.10.
//
// THE OUTPUT STAGE'S OWN GRADER. Every other device test sets
// Config::linearOutput, which skips exposure and the tone map, so the readback
// happens BEFORE the post chain and nothing in the tier graded either. Measured
// on 2026-09-20: PBR Neutral's toe was removed from post.frag, changing every
// displayed pixel, and all 46 device tests stayed green. This is the test that
// would have gone red.
//
// The expectations are NOT a mirror of the shader. They are computed by a
// separate implementation in ut-ants-uta0192/golden.py -- srgbDecode, multiply
// by EXPOSURE, tone map, srgbEncode -- so the two have to agree. Regenerate
// them when EXPOSURE or the tone map changes DELIBERATELY; a red run here
// otherwise means the output stage moved when nobody meant it to.
//
// The colours are PF_Unlit, so no light model reaches them: this grades
// EXPOSURE and the tone map alone, and is unaffected by DISPLAY_LIGHT_POWER or
// AMBIENT_SCALE. Every channel is odd, which bc7Solid requires.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;

namespace {

constexpr std::uint32_t WIDTH = 160;
constexpr std::uint32_t HEIGHT = 64;

/// The pixel column a square centred at world `y` on the plane x = 100 lands
/// on: 250 units either side at that distance, across 160 pixels.
std::uint32_t columnOf(float y) { return static_cast<std::uint32_t>(80.0f + y * 0.32f); }

struct Case {
    float y;
    Rgba stored;   ///< the base colour the material holds
    Rgba expected; ///< what the output stage must show for it
    const char* regime;
};

// ut-ants-uta0192/golden.py 5.03, at the shipped EXPOSURE. `peak` is the exposed
// linear value the tone map sees, and decides which part of the curve each case
// exercises.
const std::array<Case, 4> CASES = {{
    {-150.0f, {61, 61, 61, 255}, {133, 133, 133, 255}, "peak 0.235 -- below the knee, so the tone map is the identity here and "
                                                       "this case pins EXPOSURE alone."},
    {-50.0f, {133, 133, 133, 255}, {245, 245, 245, 255}, "peak 1.180 -- just into the shoulder, and the case that separates a "
                                                         "shoulder from a clip: clipping would show 255 here, ten bytes away."},
    {50.0f, {201, 121, 61, 255}, {252, 182, 145, 255}, "peak 2.938 -- well into the shoulder and saturated, so it exercises the "
                                                       "desaturation term too."},
    {150.0f, {255, 255, 255, 255}, {254, 254, 254, 255}, "peak 5.030 -- full scale, and the shoulder's asymptote. Only one byte "
                                                         "from a clip's 255, so it is NOT the case that grades the shoulder."},
}};

uta::ubundle::Bundle fourColours() {
    uta::ubundle::Geometry geometry;
    for (std::size_t i = 0; i < CASES.size(); ++i)
        addSquare(geometry, 100, CASES[i].y, 0, 30, "colour" + std::to_string(i), PF_UNLIT);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    for (std::size_t i = 0; i < CASES.size(); ++i) addSolidMaterial(bundle, "colour" + std::to_string(i), CASES[i].stored);
    return bundle;
}

/// True where every channel is within `slack` of `wanted`. The sRGB store is the
/// hardware's, so the last byte can differ by one between drivers; a tone-map or
/// exposure change moves these by five to fifteen, which one byte cannot hide.
bool near(const Rgba& got, const Rgba& wanted, int slack = 1) {
    const auto off = [slack](std::uint8_t a, std::uint8_t b) { return std::abs(int{a} - int{b}) <= slack; };
    return off(got.r, wanted.r) && off(got.g, wanted.g) && off(got.b, wanted.b) && got.a == wanted.a;
}

} // namespace

TEST_CASE("UTA-0192: the output stage shows the measured bytes for a known colour", "[device]") {
    removeDisplay();
    Config config;
    config.width = WIDTH;
    config.height = HEIGHT;
    // Deliberately NOT linearOutput: the post chain is the subject.
    auto renderer = requireRenderer(config);

    const uta::ubundle::Bundle bundle = fourColours();
    requireOk(renderer.draw(bundle, Camera{}));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());

    for (const Case& c : CASES) {
        INFO(c.regime);
        CHECK(near(pixelAt(*pixels, WIDTH, columnOf(c.y), HEIGHT / 2), c.expected));
    }
}

TEST_CASE("UTA-0192: the tone map keeps a shoulder rather than clipping", "[device]") {
    // The shoulder is what stops bright lamps, fog glow and UTA-0053's bloom
    // flat-topping, and UTA-0192 measured it as costing nothing against the
    // original -- so a later change must not quietly drop it for a clip.
    //
    // Graded on the {133, 133, 133} square, NOT on full scale. Its exposed peak
    // is 1.18, where the shoulder shows 245 and a clip would show 255; at full
    // scale the two differ by a single byte and would not survive a driver's
    // rounding.
    removeDisplay();
    Config config;
    config.width = WIDTH;
    config.height = HEIGHT;
    auto renderer = requireRenderer(config);

    const uta::ubundle::Bundle bundle = fourColours();
    requireOk(renderer.draw(bundle, Camera{}));
    const auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());

    const Rgba rolled = pixelAt(*pixels, WIDTH, columnOf(-50.0f), HEIGHT / 2);
    CHECK(rolled.r < 252);
    CHECK(rolled.r > 236);
}
