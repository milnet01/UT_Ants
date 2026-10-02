// UTA-0089: water reflects, and its picture does not repeat exactly --
// docs/specs/UTA-0089-water-reflections.md SS 4.2 to SS 4.4, INV-1, INV-2 and
// INV-4 to INV-6. INV-3 is tests/unit/ZonesSeeingSkyTest.cpp's.
//
// EVERY SQUARE IS UNLIT, so the probes reach its colour only through a
// reflection, and every look has amplitude 0, so UTA-0105's ripple neither
// shifts the picture nor tilts the normal.
//
// EACH FRAME IS A FRESH RENDERER'S FIRST, as RenderLiquidsTest's are, so no
// frame carries history from the one before.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

using namespace uta::test::render;
using uta::ubundle::LiquidKind;
using uta::ubundle::LiquidLook;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;
using uta::urender::Tier;

namespace {

constexpr std::uint32_t SIZE = 128;
constexpr double PINNED = 1.0;
constexpr std::uint32_t SPACING = 128;

LiquidLook still(LiquidKind kind) {
    LiquidLook look;
    look.kind = kind;
    look.amplitude = 0;
    look.frequency = 8;
    look.pan = {128, 128};
    look.size = {16, 16};
    for (auto& entry : look.ramp) entry = {0.5f, 0.5f, 0.5f};
    return look;
}

/// Probes on every face of every cell near the origin's squares, all `colour`,
/// so the indirect light is `colour` in every direction.
void addProbes(uta::ubundle::Bundle& bundle, std::array<float, 3> colour) {
    bundle.lightProbes.emplace();
    bundle.lightProbes->spacing = SPACING;
    for (int z = -2; z <= 1; ++z)
        for (int y = -2; y <= 2; ++y)
            for (int x = -1; x <= 2; ++x) {
                uta::ubundle::LightProbe probe;
                probe.cell = {x, y, z};
                for (auto& face : probe.cube) face = colour;
                bundle.lightProbes->probes.push_back(probe);
            }
}

/// The colour frame of `bundle` seen by `camera`, from a fresh renderer at
/// `tier`. Medium is the lowest the water look draws at (SS 4.5).
std::vector<std::byte> frameOf(const uta::ubundle::Bundle& bundle, const Camera& camera, Tier tier = Tier::Medium) {
    Config config;
    config.width = SIZE;
    config.height = SIZE;
    config.linearOutput = true;
    config.tier = tier;
    config.hazeScale = 0;
    Renderer renderer = requireRenderer(config);
    renderer.pinLightSeconds(PINNED);
    requireOk(renderer.draw(bundle, camera));
    auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    return std::move(*pixels);
}

// -- INV-1 and INV-2: a solid square, seen along its normal or at 80 degrees --

/// An unlit square at x = 100 wearing a solid red "pool", with `look` and
/// probes of `probeColour`.
uta::ubundle::Bundle solidPool(std::optional<LiquidLook> look, std::array<float, 3> probeColour) {
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, 60, "pool", PF_UNLIT);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "pool", Rgba{255, 1, 1, 255});
    (*bundle.materials)[0].liquid = look;
    addProbes(bundle, probeColour);
    return bundle;
}

/// A camera 100 units from the square's centre, `degrees` from its normal.
Camera viewing(double degrees) {
    const double a = degrees * std::numbers::pi / 180.0;
    Camera camera;
    camera.location = {static_cast<float>(100.0 - 100.0 * std::cos(a)), static_cast<float>(100.0 * std::sin(a)), 0.0f};
    camera.rotation = {0, static_cast<std::int32_t>(std::lround(-degrees / 360.0 * 65536.0)), 0};
    return camera;
}

Rgba centreOf(const std::vector<std::byte>& frame) { return pixelAt(frame, SIZE, SIZE / 2, SIZE / 2); }

// -- INV-4 to INV-6: a square filling a narrow view, many repeats across --

/// A vertical field of view of 10 degrees, at the distance where one unit of
/// the square is one pixel: so the view is nearly parallel, the angle to the
/// normal nearly the same everywhere, and a repeat a whole number of pixels.
constexpr float NARROW_FOV = 10.0f;
const float NARROW_DISTANCE = static_cast<float>(SIZE / 2.0 / std::tan(NARROW_FOV / 2 * std::numbers::pi / 180.0));

Camera narrow() {
    Camera camera;
    camera.verticalFovDegrees = NARROW_FOV;
    return camera;
}

/// A square filling the narrow view, at `NARROW_DISTANCE` and facing it, whose
/// picture repeats every `repeatUnits` units (pixels) across it.
uta::ubundle::Geometry tiledSquare(float repeatUnits) {
    uta::ubundle::Geometry geometry;
    const float half = SIZE; // past the view's edge on every side
    addSquare(geometry, NARROW_DISTANCE, 0, 0, half, "pool", PF_UNLIT);
    for (std::size_t i = geometry.vertices.size() - 4; i < geometry.vertices.size(); ++i) {
        geometry.vertices[i].u *= 2 * half / repeatUnits;
        geometry.vertices[i].v *= 2 * half / repeatUnits;
    }
    return geometry;
}

/// A BC7 mode 6 block whose texel i is `white` where bit i of `mask` is set and
/// `black` elsewhere. Texel 0's index has an implied top bit of 0, so the
/// endpoints are swapped when texel 0 must be the second one. Mode 6 shares one
/// p-bit across channels, so every channel of both colours must share the
/// parity of `black.r` -- bc7Solid's rule, which the caller checks once.
std::vector<std::byte> bc7TwoTone(std::uint16_t mask, const Rgba& black, const Rgba& white) {
    const bool swap = (mask & 1u) != 0;
    const Rgba& e0 = swap ? white : black;
    const Rgba& e1 = swap ? black : white;
    const int parity = black.r & 1;
    std::array<std::uint8_t, 16> block{};
    int bit = 0;
    const auto put = [&](unsigned value, int count) {
        for (int i = 0; i < count; ++i, ++bit)
            if (((value >> i) & 1u) != 0) block[bit / 8] |= static_cast<std::uint8_t>(1u << (bit % 8));
    };
    put(1u << 6, 7); // mode 6
    put(e0.r >> 1u, 7);
    put(e1.r >> 1u, 7);
    put(e0.g >> 1u, 7);
    put(e1.g >> 1u, 7);
    put(e0.b >> 1u, 7);
    put(e1.b >> 1u, 7);
    put(e0.a >> 1u, 7);
    put(e1.a >> 1u, 7);
    put(static_cast<unsigned>(parity), 1);
    put(static_cast<unsigned>(parity), 1);
    for (int texel = 0; texel < 16; ++texel) {
        const bool second = (((mask >> texel) & 1u) != 0) != swap;
        put(second ? 15u : 0u, texel == 0 ? 3 : 4);
    }
    std::vector<std::byte> bytes;
    for (const std::uint8_t b : block) bytes.push_back(std::byte{b});
    return bytes;
}

/// A 16x16 picture of sixteen 4x4 blocks, each a different colour, so every
/// block of a repeat differs from its neighbours.
uta::ubundle::CompressedTexture patchwork() {
    uta::ubundle::CompressedTexture base;
    base.name = "pool:base";
    base.format = uta::ubundle::BlockFormat::BC7;
    base.width = base.sourceWidth = 16;
    base.height = base.sourceHeight = 16;
    base.mipCount = 1;
    for (std::uint8_t i = 0; i < 16; ++i) {
        const auto channel = [](unsigned v) { return static_cast<std::uint8_t>((v * 2u) & 0xFEu); };
        for (const std::byte b : bc7Solid(Rgba{channel(i * 37u + 20u), channel(i * 71u + 5u), channel(i * 13u + 90u), 254}))
            base.blocks.push_back(b);
    }
    return base;
}

/// The sum of the absolute byte differences between the `edge`-pixel block at
/// (x0, y0) and the one at (x1, y1), over red, green and blue.
long blockDifference(const std::vector<std::byte>& frame, std::uint32_t x0, std::uint32_t y0, std::uint32_t x1,
                     std::uint32_t y1, std::uint32_t edge) {
    long sum = 0;
    for (std::uint32_t y = 0; y < edge; ++y)
        for (std::uint32_t x = 0; x < edge; ++x) {
            const Rgba a = pixelAt(frame, SIZE, x0 + x, y0 + y);
            const Rgba b = pixelAt(frame, SIZE, x1 + x, y1 + y);
            sum += std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b);
        }
    return sum;
}

constexpr float PATCH_REPEAT = 32; // pixels a repeat of the patchwork spans

uta::ubundle::Bundle patchworkPool(std::optional<LiquidLook> look) {
    uta::ubundle::Bundle bundle = bundleOf(tiledSquare(PATCH_REPEAT));
    uta::ubundle::MaterialRecord record{"pool", false, 0};
    record.liquid = look;
    bundle.materials = std::vector<uta::ubundle::MaterialRecord>{record};
    bundle.textures = std::vector<uta::ubundle::CompressedTexture>{patchwork()};
    return bundle;
}

/// A 2048x2048 checker of four 1024-texel squares, black and white, with every
/// mip level down to 1x1. The 1x1 is a dark grey rather than the checker's own
/// mean: every level's centre is where four squares meet, so only a 1x1 that
/// differs from them shows which level the fade read.
uta::ubundle::CompressedTexture checker() {
    const Rgba black{1, 1, 1, 255};
    const Rgba white{255, 255, 255, 255};
    const Rgba grey{61, 61, 61, 255};
    for (const Rgba& c : {black, white})
        REQUIRE(((c.r & 1) == 1 && (c.g & 1) == 1 && (c.b & 1) == 1 && (c.a & 1) == 1));
    constexpr std::uint32_t TOP = 2048;
    uta::ubundle::CompressedTexture base;
    base.name = "pool:base";
    base.format = uta::ubundle::BlockFormat::BC7;
    base.width = base.sourceWidth = TOP;
    base.height = base.sourceHeight = TOP;
    std::uint32_t levels = 0;
    for (std::uint32_t edge = TOP; edge >= 1; edge /= 2, ++levels) {
        if (edge == 1) {
            for (const std::byte b : bc7Solid(grey)) base.blocks.push_back(b);
            continue;
        }
        const std::uint32_t square = edge / 2;
        const auto whiteAt = [&](std::uint32_t x, std::uint32_t y) { return ((x / square) ^ (y / square)) & 1u; };
        const std::uint32_t blocks = std::max(edge / 4, 1u);
        for (std::uint32_t by = 0; by < blocks; ++by)
            for (std::uint32_t bx = 0; bx < blocks; ++bx) {
                std::uint16_t mask = 0;
                for (std::uint32_t t = 0; t < 16; ++t) {
                    const std::uint32_t x = bx * 4 + t % 4, y = by * 4 + t / 4;
                    if (x < edge && y < edge && whiteAt(x, y) != 0) mask |= static_cast<std::uint16_t>(1u << t);
                }
                const auto block = mask == 0 ? bc7Solid(black) : mask == 0xFFFF ? bc7Solid(white)
                                                                                : bc7TwoTone(mask, black, white);
                for (const std::byte b : block) base.blocks.push_back(b);
            }
    }
    base.mipCount = static_cast<std::uint8_t>(levels);
    return base;
}

struct Spread {
    double mean = 0;
    double deviation = 0;
};

/// The mean and standard deviation of the luma over the central half of `frame`.
Spread spreadOf(const std::vector<std::byte>& frame) {
    double sum = 0, squares = 0;
    int count = 0;
    for (std::uint32_t y = SIZE / 4; y < SIZE * 3 / 4; ++y)
        for (std::uint32_t x = SIZE / 4; x < SIZE * 3 / 4; ++x) {
            const Rgba p = pixelAt(frame, SIZE, x, y);
            const double luma = 0.2126 * p.r + 0.7152 * p.g + 0.0722 * p.b;
            sum += luma;
            squares += luma * luma;
            ++count;
        }
    const double mean = sum / count;
    return {mean, std::sqrt(std::max(squares / count - mean * mean, 0.0))};
}

} // namespace

TEST_CASE("UTA-0089 INV-1: water reflects more at a grazing angle than straight down", "[device][water]") {
    removeDisplay();
    const uta::ubundle::Bundle pool = solidPool(still(LiquidKind::Wet), {0.0f, 0.5f, 0.0f});
    const Rgba along = centreOf(frameOf(pool, viewing(0)));
    const Rgba grazing = centreOf(frameOf(pool, viewing(80)));
    CAPTURE(along, grazing);
    CHECK(grazing.r > 100); // the centre is still the square
    // Schlick at R0 0.02: about 0.02 along the normal, about 0.4 at 80 degrees.
    CHECK(grazing.g >= along.g + 20);
    CHECK(grazing.r + 20 <= along.r);
}

TEST_CASE("UTA-0089 INV-2: only Wet and Wave liquids reflect", "[device][water]") {
    removeDisplay();
    const auto reflects = [](std::optional<LiquidLook> look) {
        const Rgba green = centreOf(frameOf(solidPool(look, {0.0f, 0.5f, 0.0f}), viewing(80)));
        const Rgba blue = centreOf(frameOf(solidPool(look, {0.0f, 0.0f, 0.5f}), viewing(80)));
        CAPTURE(green, blue);
        return green != blue;
    };
    CHECK(reflects(still(LiquidKind::Wet)));
    CHECK(reflects(still(LiquidKind::Wave)));
    CHECK_FALSE(reflects(still(LiquidKind::Ice)));
    CHECK_FALSE(reflects(std::nullopt));
}

TEST_CASE("UTA-0089 INV-4: a Wet picture does not repeat exactly", "[device][water]") {
    removeDisplay();
    constexpr auto R = static_cast<std::uint32_t>(PATCH_REPEAT);
    // Two blocks a repeat apart, across and down, well inside the frame.
    const auto across = [&](const std::vector<std::byte>& frame) {
        return blockDifference(frame, 16, 16, 16 + R, 16, R) + blockDifference(frame, 16, 16, 16, 16 + R, R);
    };
    const long plain = across(frameOf(patchworkPool(std::nullopt), narrow()));
    const long wet = across(frameOf(patchworkPool(still(LiquidKind::Wet)), narrow()));
    CAPTURE(plain, wet);
    CHECK(plain == 0);
    CHECK(wet > 1000);
}

TEST_CASE("UTA-0089 INV-5: a Wet picture drawn small fades to its mean", "[device][water]") {
    removeDisplay();
    // 5.66 pixels a repeat of 2048 texels: the sampler picks level 8.5, past
    // the fade's end. A square spans 2.8 pixels there, and level 8's 8x8 has
    // four-texel squares, so the plain checker keeps its contrast.
    const float repeat = 2048.0f / std::exp2(8.5f);
    const auto pool = [&](std::optional<LiquidLook> look) {
        uta::ubundle::Bundle bundle = bundleOf(tiledSquare(repeat));
        uta::ubundle::MaterialRecord record{"pool", false, 0};
        record.liquid = look;
        bundle.materials = std::vector<uta::ubundle::MaterialRecord>{record};
        bundle.textures = std::vector<uta::ubundle::CompressedTexture>{checker()};
        return bundle;
    };
    const Spread plain = spreadOf(frameOf(pool(std::nullopt), narrow()));
    const Spread wet = spreadOf(frameOf(pool(still(LiquidKind::Wet)), narrow()));
    CAPTURE(plain.mean, plain.deviation, wet.mean, wet.deviation);
    CHECK(plain.deviation > 20);
    CHECK(wet.deviation * 4 < plain.deviation);
    // It fades toward the smallest level, the dark grey, not the checker.
    CHECK(wet.mean * 2 < plain.mean);
}

TEST_CASE("UTA-0089 INV-6: a liquid draws the same twice at a pinned light time", "[device][water]") {
    removeDisplay();
    const uta::ubundle::Bundle pool = patchworkPool(still(LiquidKind::Wet));
    CHECK(frameOf(pool, narrow()) == frameOf(pool, narrow()));
}

TEST_CASE("UTA-0089: below Medium a liquid draws as UTA-0105 left it", "[device][water]") {
    // SS 4.5: the look's cost put it behind Feature::WaterLook. At Low a Wet
    // square neither reflects nor varies its picture.
    removeDisplay();
    const Rgba green = centreOf(frameOf(solidPool(still(LiquidKind::Wet), {0.0f, 0.5f, 0.0f}), viewing(80), Tier::Low));
    const Rgba blue = centreOf(frameOf(solidPool(still(LiquidKind::Wet), {0.0f, 0.0f, 0.5f}), viewing(80), Tier::Low));
    CAPTURE(green, blue);
    CHECK(green == blue);
    const auto low = [](std::optional<LiquidLook> look) { return frameOf(patchworkPool(look), narrow(), Tier::Low); };
    CHECK(low(still(LiquidKind::Wet)) == low(std::nullopt));
}
