// UTA-0009's material-generation cases.
//
// docs/specs/UTA-0009-material-from-texture.md, INV-1 to INV-11, plus one
// case per refusal SS 4.7 lists. INV-12 is a configure-time assertion in
// src/umat/CMakeLists.txt; INV-13 is a real-asset census in
// tests/real/RealInstallTest.cpp.
//
// EVERY IMAGE IS SYNTHETIC. Nothing here reads a package, so the whole file
// runs on a clone with no Unreal Tournament install (S7).
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "umat/Derive.h"
#include "umat/Enlarge.h"
#include "umat/Generate.h"
#include "umat/Resolve.h"

#include "core/Jobs.h"
#include "ubundle/Bundle.h"
#include "upkg/Texture.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <span>
#include <string>
#include <thread>
#include <vector>

using uta::ErrorCode;
using uta::JobSystem;
using uta::ubundle::BlockFormat;
using uta::umat::emissiveOf;
using uta::umat::enlarge;
using uta::umat::generate;
using uta::umat::heightOf;
using uta::umat::Image;
using uta::umat::materialId;
using uta::umat::MaterialSettings;
using uta::umat::mipChain;
using uta::umat::normalOf;
using uta::umat::resolve;
using uta::umat::roughnessOf;
using uta::upkg::Mip;
using uta::upkg::Palette;
using uta::upkg::PaletteEntry;
namespace detail = uta::umat::detail;

namespace {

/// A palettised level and the bytes its Mip views.
struct Palettised {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::byte> indices;

    [[nodiscard]] Mip mip() const {
        Mip out;
        out.pixels = indices;
        out.width = width;
        out.height = height;
        return out;
    }
};

Palettised palettised(std::uint32_t width, std::uint32_t height,
                      std::initializer_list<unsigned> indices) {
    Palettised out{width, height, {}};
    for (const unsigned index : indices) out.indices.push_back(static_cast<std::byte>(index));
    return out;
}

Palette palette(std::initializer_list<PaletteEntry> entries) { return Palette{entries}; }

/// An image whose every texel `texel(x, y)` gives.
Image image(std::uint32_t width, std::uint32_t height, std::uint8_t channels,
            const std::function<std::array<std::uint8_t, 4>(std::uint32_t, std::uint32_t)>& texel) {
    Image out;
    out.width = width;
    out.height = height;
    out.channels = channels;
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto value = texel(x, y);
            for (std::uint32_t c = 0; c < channels; ++c)
                out.pixels.push_back(static_cast<std::byte>(value[c]));
        }
    return out;
}

/// Opaque RGBA noise, integer-only so every compiler builds the same image.
Image noise(std::uint32_t width, std::uint32_t height) {
    return image(width, height, 4, [](std::uint32_t x, std::uint32_t y) {
        const auto v = [&](std::uint32_t c) {
            return static_cast<std::uint8_t>((x * 37U + y * 91U + c * 53U + x * y * 13U) & 0xFFU);
        };
        return std::array<std::uint8_t, 4>{v(0), v(1), v(2), 255};
    });
}

std::uint8_t px(const Image& img, std::uint32_t x, std::uint32_t y, std::uint32_t c) {
    return std::to_integer<std::uint8_t>(img.pixels[(std::size_t{y} * img.width + x) * img.channels + c]);
}

/// `img` shifted cyclically by `dx` texels right and `dy` down.
Image shifted(const Image& img, std::uint32_t dx, std::uint32_t dy) {
    Image out = img;
    for (std::uint32_t y = 0; y < img.height; ++y)
        for (std::uint32_t x = 0; x < img.width; ++x)
            for (std::uint32_t c = 0; c < img.channels; ++c)
                out.pixels[(std::size_t{(y + dy) % img.height} * img.width + (x + dx) % img.width)
                               * img.channels
                           + c] = img.pixels[(std::size_t{y} * img.width + x) * img.channels + c];
    return out;
}

/// FNV-1a over a map's blocks: INV-6's golden, compact enough to keep literal.
std::uint64_t digest(const std::vector<std::byte>& bytes) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const std::byte b : bytes) {
        hash ^= std::to_integer<std::uint64_t>(b);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

/// The Lanczos kernel, computed here with the maths library -- in the test,
/// never in the engine, which carries it as a table (INV-3).
double lanczos(double x) {
    const double a = detail::LANCZOS_A;
    if (x == 0.0) return 1.0;
    if (std::abs(x) >= a) return 0.0;
    const double pi = 3.14159265358979323846;
    return a * std::sin(pi * x) * std::sin(pi * x / a) / (pi * pi * x * x);
}

void phasesMatchKernel(std::span<const detail::Phase> phases, int factor) {
    REQUIRE(phases.size() == static_cast<std::size_t>(factor));
    for (std::size_t p = 0; p < phases.size(); ++p) {
        std::int64_t sum = 0;
        for (const std::int32_t weight : phases[p]) sum += weight;
        CHECK(sum == detail::WEIGHT_ONE);

        const double x = (2.0 * static_cast<double>(p) + 1.0 - factor) / (2.0 * factor);
        const double d = x - std::floor(x);
        std::array<double, detail::TAPS> raw{};
        double total = 0.0;
        for (int t = 0; t < detail::TAPS; ++t) {
            raw[static_cast<std::size_t>(t)] = lanczos(d - (t - (detail::LANCZOS_A - 1)));
            total += raw[static_cast<std::size_t>(t)];
        }
        for (std::size_t t = 0; t < raw.size(); ++t)
            CHECK(std::abs(phases[p][t] - detail::WEIGHT_ONE * raw[t] / total) <= 1.0);
    }
}

} // namespace

TEST_CASE("resolve gives each texel its palette colour and a masked texel its fill",
          "[umat][resolve]") {
    // INV-1. One index-0 texel at (1,1); its eight neighbours are three of
    // colour 1, three of colour 2 and two of colour 3, so SS 4.2's fill is
    // ((300 + 4) / 8, (300 + 4) / 8, (200 + 4) / 8). Index 0 carries alpha 0
    // IN THE PALETTE, so reading the palette's alpha breaks the opaque case.
    const Palettised level = palettised(4, 4, {1, 2, 3, 1,  //
                                               2, 0, 1, 2,  //
                                               3, 1, 2, 3,  //
                                               1, 3, 2, 1});
    const Palette colours =
        palette({{10, 20, 30, 0}, {100, 0, 0, 255}, {0, 100, 0, 255}, {0, 0, 100, 255}});

    const auto opaque = resolve(level.mip(), colours, false);
    const auto masked = resolve(level.mip(), colours, true);
    REQUIRE(opaque.has_value());
    REQUIRE(masked.has_value());
    CHECK(opaque->channels == 4);

    for (std::uint32_t y = 0; y < 4; ++y) {
        for (std::uint32_t x = 0; x < 4; ++x) {
            const PaletteEntry& entry =
                colours.entries[std::to_integer<std::size_t>(level.indices[y * 4 + x])];
            CHECK(px(*opaque, x, y, 0) == entry.r);
            CHECK(px(*opaque, x, y, 1) == entry.g);
            CHECK(px(*opaque, x, y, 2) == entry.b);
            CHECK(px(*opaque, x, y, 3) == 255);
            if (x == 1 && y == 1) continue;
            CHECK(px(*masked, x, y, 0) == entry.r);
            CHECK(px(*masked, x, y, 1) == entry.g);
            CHECK(px(*masked, x, y, 2) == entry.b);
            CHECK(px(*masked, x, y, 3) == 255);
        }
    }
    CHECK(px(*masked, 1, 1, 0) == 38);
    CHECK(px(*masked, 1, 1, 1) == 38);
    CHECK(px(*masked, 1, 1, 2) == 25);
    CHECK(px(*masked, 1, 1, 3) == 0);
}

TEST_CASE("a masked texture with no opaque texel keeps its palette colours", "[umat][resolve]") {
    const auto masked = resolve(palettised(2, 2, {0, 0, 0, 0}).mip(),
                                palette({{10, 20, 30, 0}}), true);
    REQUIRE(masked.has_value());
    for (std::uint32_t i = 0; i < 4; ++i) {
        CHECK(px(*masked, i % 2, i / 2, 0) == 10);
        CHECK(px(*masked, i % 2, i / 2, 1) == 20);
        CHECK(px(*masked, i % 2, i / 2, 2) == 30);
        CHECK(px(*masked, i % 2, i / 2, 3) == 0);
    }
}

TEST_CASE("each fill pass reads only the state the previous pass left", "[umat][resolve]") {
    // SS 4.2. A 4x1 strip [1 0 0 2]: in the first pass texel 1 sees only
    // colour 1 and texel 2 only colour 2. A fill that read its own writes
    // would hand texel 2 a mix of both.
    const auto masked =
        resolve(palettised(4, 1, {1, 0, 0, 2}).mip(),
                palette({{0, 0, 0, 0}, {100, 0, 0, 255}, {0, 100, 0, 255}}), true);
    REQUIRE(masked.has_value());
    CHECK(px(*masked, 1, 0, 0) == 100);
    CHECK(px(*masked, 1, 0, 1) == 0);
    CHECK(px(*masked, 2, 0, 0) == 0);
    CHECK(px(*masked, 2, 0, 1) == 100);
}

TEST_CASE("resolve refuses a level it cannot read", "[umat][resolve]") {
    const Palette two = palette({{0, 0, 0, 0}, {1, 1, 1, 255}});
    SECTION("a sound level succeeds") {
        CHECK(resolve(palettised(2, 2, {0, 1, 1, 0}).mip(), two, false).has_value());
    }
    SECTION("a zero dimension") {
        // No bytes either, so the byte-count rule is satisfied and only the
        // zero-dimension rule refuses it.
        const auto result = resolve(palettised(0, 2, {}).mip(), two, false);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::InvalidArgument);
    }
    SECTION("a byte count that is not width times height") {
        // One byte too MANY, every one a valid index: without the byte-count
        // rule this resolves cleanly, where one too few reads past the view
        // and the index rule may refuse the stray byte instead.
        const auto result = resolve(palettised(2, 2, {0, 1, 1, 0, 1}).mip(), two, false);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::InvalidArgument);
    }
    SECTION("an index past the palette") {
        const auto result = resolve(palettised(2, 2, {0, 1, 2, 0}).mip(), two, false);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("the enlarged base measures the applied factor times the input", "[umat][enlarge]") {
    // INV-2.
    JobSystem jobs(2);
    SECTION("factor 1 returns the input bytes unchanged") {
        const Image input = noise(8, 4);
        const auto same = enlarge(input, 1, jobs);
        REQUIRE(same.has_value());
        CHECK(same->width == 8);
        CHECK(same->height == 4);
        CHECK(same->pixels == input.pixels);
    }
    SECTION("a 64x64 base at requested 1 and 2 and 4") {
        for (const std::uint32_t requested : {1U, 2U, 4U}) {
            MaterialSettings settings;
            settings.requestedUpscale = requested;
            const auto material = generate("t", noise(64, 64), settings, jobs);
            REQUIRE(material.has_value());
            for (const auto& map : material->maps) {
                CHECK(map.width == 64 * requested);
                CHECK(map.height == 64 * requested);
                CHECK(map.sourceWidth == 64);
                CHECK(map.sourceHeight == 64);
            }
        }
    }
    SECTION("a 2048x4 base above MAX_OUTPUT_EDGE passes through at factor 1") {
        const auto material = generate("t", noise(2048, 4), MaterialSettings{}, jobs);
        REQUIRE(material.has_value());
        for (const auto& map : material->maps) {
            CHECK(map.width == 2048);
            CHECK(map.height == 4);
            CHECK(map.sourceWidth == 2048);
            CHECK(map.sourceHeight == 4);
        }
    }
}

TEST_CASE("every Lanczos phase is the normalised kernel in fixed point", "[umat][enlarge]") {
    // INV-3.
    phasesMatchKernel(detail::kWeights2, 2);
    phasesMatchKernel(detail::kWeights4, 4);

    JobSystem jobs(1);
    const Image flat = image(4, 4, 4, [](std::uint32_t, std::uint32_t) {
        return std::array<std::uint8_t, 4>{7, 77, 177, 255};
    });
    for (const std::uint32_t factor : {2U, 4U}) {
        const auto big = enlarge(flat, factor, jobs);
        REQUIRE(big.has_value());
        for (std::uint32_t y = 0; y < big->height; ++y)
            for (std::uint32_t x = 0; x < big->width; ++x) {
                CHECK(px(*big, x, y, 0) == 7);
                CHECK(px(*big, x, y, 1) == 77);
                CHECK(px(*big, x, y, 2) == 177);
                CHECK(px(*big, x, y, 3) == 255);
            }
    }
}

TEST_CASE("enlarging commutes with wrapping", "[umat][enlarge]") {
    // INV-4. A clamping enlarger passes every other case in this file.
    JobSystem jobs(2);
    const Image source = noise(16, 16);
    for (const std::uint32_t factor : {2U, 4U}) {
        const auto moved = enlarge(shifted(source, 1, 1), factor, jobs);
        const auto plain = enlarge(source, factor, jobs);
        REQUIRE(moved.has_value());
        REQUIRE(plain.has_value());
        CHECK(moved->pixels == shifted(*plain, factor, factor).pixels);
    }
}

TEST_CASE("enlarge refuses an image it cannot filter", "[umat][enlarge]") {
    JobSystem jobs(1);
    SECTION("an image that is not RGBA") {
        // Bytes still sized for RGBA, so only the channel rule refuses it.
        Image two = noise(2, 2);
        two.channels = 2;
        const auto result = enlarge(two, 2, jobs);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::InvalidArgument);
    }
    SECTION("a factor other than 1 or 2 or 4") {
        const auto result = enlarge(noise(2, 2), 3, jobs);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("no level of a masked base colour carries the see-through colour", "[umat][resolve]") {
    // INV-5. Index 0 is pure green; the one opaque block is red with no green.
    // Without SS 4.2's fill, the enlarger bleeds green into the block's edge.
    Palettised level{8, 8, std::vector<std::byte>(64, std::byte{0})};
    for (const std::uint32_t y : {2U, 3U})
        for (const std::uint32_t x : {2U, 3U}) level.indices[y * 8 + x] = std::byte{1};
    const auto masked = resolve(level.mip(), palette({{0, 255, 0, 0}, {200, 0, 0, 255}}), true);
    REQUIRE(masked.has_value());

    JobSystem jobs(1);
    for (const std::uint32_t factor : {1U, 2U, 4U}) {
        const auto big = enlarge(*masked, factor, jobs);
        REQUIRE(big.has_value());
        for (const Image& mip : mipChain(*big))
            for (std::uint32_t y = 0; y < mip.height; ++y)
                for (std::uint32_t x = 0; x < mip.width; ++x) CHECK(px(mip, x, y, 1) == 0);
    }
}

TEST_CASE("generate produces fixed bytes for every map", "[umat][generate]") {
    // INV-6: one constant per map, compared on GCC, Clang and MSVC alike.
    // Captured on GCC. A change to any stage -- or to the vendored encoder --
    // moves these, which is the case this exists to catch.
    MaterialSettings settings;
    settings.requestedUpscale = 2;
    settings.emissive = true;
    settings.emissiveThreshold = 128;
    JobSystem jobs(2);
    const auto material = generate("golden", noise(8, 8), settings, jobs);
    REQUIRE(material.has_value());
    REQUIRE(material->maps.size() == 5);
    const std::array<std::uint64_t, 5> expected{
        0x3f85f2f85ac0e5c0ULL, // base
        0xc36a073a8f000e21ULL, // normal
        0x97ee2ba1bc8915f5ULL, // rough
        0x6347ec980c3f407cULL, // height
        0xa20f29bc2905d47cULL, // emit
    };
    for (std::size_t m = 0; m < expected.size(); ++m) {
        CAPTURE(m);
        CHECK(digest(material->maps[m].blocks) == expected[m]);
    }
}

TEST_CASE("generate output does not depend on the worker count", "[umat][generate]") {
    // INV-7. Three counts including 1, for UTA-0052 INV-7's reason: a pool of
    // one gives a CONSISTENT wrong answer where rows share a scratch buffer.
    MaterialSettings settings;
    settings.requestedUpscale = 2;
    settings.emissive = true;
    const Image base = noise(32, 32);
    JobSystem one(1);
    JobSystem two(2);
    JobSystem all(std::thread::hardware_concurrency());
    const auto a = generate("w", base, settings, one);
    const auto b = generate("w", base, settings, two);
    const auto c = generate("w", base, settings, all);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(c.has_value());
    REQUIRE(a->maps.size() == 5);
    for (std::size_t m = 0; m < a->maps.size(); ++m) {
        CAPTURE(m);
        CHECK(a->maps[m].blocks == b->maps[m].blocks);
        CHECK(a->maps[m].blocks == c->maps[m].blocks);
    }
}

TEST_CASE("a material carries its maps in MapKind order and format", "[umat][generate]") {
    // INV-8. UTA-0052's reader accepts any format, so only this grades it.
    JobSystem jobs(1);
    MaterialSettings settings;
    settings.requestedUpscale = 1;
    settings.metallic = true;

    const auto plain = generate("m", noise(8, 8), settings, jobs);
    REQUIRE(plain.has_value());
    CHECK(plain->id == "m");
    CHECK(plain->metallic);
    REQUIRE(plain->maps.size() == 4);
    const std::array<const char*, 5> names{"m:base", "m:normal", "m:rough", "m:height", "m:emit"};
    const std::array<BlockFormat, 5> formats{BlockFormat::BC7, BlockFormat::BC5, BlockFormat::BC4,
                                             BlockFormat::BC4, BlockFormat::BC7};
    for (std::size_t m = 0; m < plain->maps.size(); ++m) {
        CHECK(plain->maps[m].name == names[m]);
        CHECK(plain->maps[m].format == formats[m]);
    }

    settings.emissive = true;
    const auto glowing = generate("m", noise(8, 8), settings, jobs);
    REQUIRE(glowing.has_value());
    REQUIRE(glowing->maps.size() == 5);
    for (std::size_t m = 0; m < glowing->maps.size(); ++m) {
        CHECK(glowing->maps[m].name == names[m]);
        CHECK(glowing->maps[m].format == formats[m]);
    }
}

TEST_CASE("every map carries the full mip chain", "[umat][generate]") {
    // INV-9.
    SECTION("each level is the rounded box average of the one above") {
        for (const auto& [width, height] : {std::array<std::uint32_t, 2>{8, 8},
                                            std::array<std::uint32_t, 2>{8, 2}}) {
            const std::vector<Image> chain = mipChain(noise(width, height));
            REQUIRE(chain.size() == static_cast<std::size_t>(std::bit_width(width)));
            for (std::size_t l = 0; l + 1 < chain.size(); ++l) {
                const Image& above = chain[l];
                const Image& below = chain[l + 1];
                CHECK(below.width == std::max<std::uint32_t>(1, above.width / 2));
                CHECK(below.height == std::max<std::uint32_t>(1, above.height / 2));
                for (std::uint32_t y = 0; y < below.height; ++y)
                    for (std::uint32_t x = 0; x < below.width; ++x)
                        for (std::uint32_t c = 0; c < 4; ++c) {
                            const std::uint32_t x1 = std::min(2 * x + 1, above.width - 1);
                            const std::uint32_t y1 = std::min(2 * y + 1, above.height - 1);
                            const unsigned sum = px(above, 2 * x, 2 * y, c) + px(above, x1, 2 * y, c)
                                                 + px(above, 2 * x, y1, c) + px(above, x1, y1, c);
                            CHECK(px(below, x, y, c) == ((sum + 2) >> 2));
                        }
            }
        }
    }
    SECTION("generate gives every map the full chain") {
        MaterialSettings settings;
        settings.requestedUpscale = 2;
        settings.emissive = true;
        JobSystem jobs(1);
        const auto material = generate("c", noise(8, 8), settings, jobs);
        REQUIRE(material.has_value());
        REQUIRE(material->maps.size() == 5);
        for (const auto& map : material->maps) CHECK(map.mipCount == std::bit_width(16U));
    }
}

TEST_CASE("normals tilt away from a rising height and keep their slope across levels",
          "[umat][derive]") {
    // INV-10. Stored bytes: 128 is zero, below 128 is negative.
    const auto one = [](std::function<std::uint8_t(std::uint32_t, std::uint32_t)> h) {
        return image(8, 8, 1, [h](std::uint32_t x, std::uint32_t y) {
            return std::array<std::uint8_t, 4>{h(x, y), 0, 0, 0};
        });
    };
    SECTION("a flat height gives the straight-up normal") {
        const Image n = normalOf(one([](std::uint32_t, std::uint32_t) { return 100; }), 1, 0);
        for (std::uint32_t y = 0; y < 8; ++y)
            for (std::uint32_t x = 0; x < 8; ++x) {
                CHECK(px(n, x, y, 0) == 128);
                CHECK(px(n, x, y, 1) == 128);
            }
    }
    const Image rightward = one([](std::uint32_t x, std::uint32_t) { return x * 16; });
    SECTION("a height rising to the right tilts X negative") {
        const Image n = normalOf(rightward, 1, 0);
        for (std::uint32_t y = 0; y < 8; ++y)
            for (std::uint32_t x = 1; x < 7; ++x) {
                CHECK(px(n, x, y, 0) < 128);
                CHECK(px(n, x, y, 1) == 128);
            }
    }
    SECTION("a height rising toward row 0 tilts Y negative") {
        const Image n = normalOf(one([](std::uint32_t, std::uint32_t y) { return (7 - y) * 16; }), 1, 0);
        for (std::uint32_t y = 1; y < 7; ++y)
            for (std::uint32_t x = 0; x < 8; ++x) {
                CHECK(px(n, x, y, 1) < 128);
                CHECK(px(n, x, y, 0) == 128);
            }
    }
    SECTION("a linear ramp keeps its slope from level 0 to level 1") {
        const Image level0 = normalOf(rightward, 1, 0);
        const Image level1 = normalOf(mipChain(rightward)[1], 1, 1);
        for (std::uint32_t x = 1; x < 3; ++x) {
            const int drift = px(level1, x, 1, 0) - px(level0, 3, 3, 0);
            CHECK(drift >= -1);
            CHECK(drift <= 1);
        }
    }
}

TEST_CASE("materialId names a texture by its package and group path", "[umat][generate]") {
    // INV-11.
    CHECK(materialId("A", "Wall", false) != materialId("B", "Wall", false));
    CHECK(materialId("A", "G1.Door", false) != materialId("A", "G2.Door", false));
    CHECK(materialId("A", "Wall", true) != materialId("A", "Wall", false));
    CHECK(materialId("PkG", "WaLL", false) == "pkg.wall");
    CHECK(materialId("A", "G1.Door", true) == "a.g1.door#masked");
}

TEST_CASE("generate refuses a base it cannot use", "[umat][generate]") {
    JobSystem jobs(1);
    // SS 6: generate refuses, naming the texture, before compress would. The
    // message is checked because compress refuses a non-power-of-two base
    // too, so the code alone cannot tell whose rule fired.
    const auto refused = [&](const Image& base) {
        const auto result = generate("r", base, MaterialSettings{}, jobs);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::InvalidArgument);
        CHECK(std::string(result.error().message()).find("generate: 'r'") != std::string::npos);
    };
    SECTION("a sound base succeeds") {
        CHECK(generate("r", noise(4, 4), MaterialSettings{}, jobs).has_value());
    }
    SECTION("a base that is not RGBA") {
        // Bytes still sized for RGBA, so only the channel rule refuses it.
        Image base = noise(4, 4);
        base.channels = 2;
        refused(base);
    }
    SECTION("a base width that is not a power of two") { refused(noise(6, 4)); }
    SECTION("a base height that is not a power of two") { refused(noise(4, 6)); }
    SECTION("a base whose bytes disagree with its dimensions") {
        Image base = noise(4, 4);
        base.pixels.pop_back();
        refused(base);
    }
}

TEST_CASE("isqrt is the integer square root rounded down", "[umat][derive]") {
    for (std::uint64_t n = 0; n < 100000; ++n) {
        const std::uint64_t r = detail::isqrt(n);
        REQUIRE(r * r <= n);
        REQUIRE((r + 1) * (r + 1) > n);
    }
    for (const std::uint64_t r : {std::uint64_t{65535}, std::uint64_t{1} << 31,
                                  std::uint64_t{4294967295ULL}}) {
        CHECK(detail::isqrt(r * r) == r);
        CHECK(detail::isqrt(r * r - 1) == r - 1);
    }
    CHECK(detail::isqrt(~std::uint64_t{0}) == 4294967295ULL);
}

TEST_CASE("roughness and emissive follow their formulas", "[umat][derive]") {
    const Image height = image(3, 1, 1, [](std::uint32_t x, std::uint32_t) {
        return std::array<std::uint8_t, 4>{static_cast<std::uint8_t>(x == 0 ? 0 : x == 1 ? 128 : 255)};
    });
    const Image rough = roughnessOf(height, 191);
    CHECK(px(rough, 0, 0, 0) == 191 + 32);
    CHECK(px(rough, 1, 0, 0) == 191);
    CHECK(px(rough, 2, 0, 0) == 191 - 31);
    CHECK(px(roughnessOf(height, 250), 0, 0, 0) == 255);

    const Image colour = image(3, 1, 4, [](std::uint32_t, std::uint32_t) {
        return std::array<std::uint8_t, 4>{9, 99, 199, 255};
    });
    const Image glow = emissiveOf(colour, height, 128);
    CHECK(px(glow, 0, 0, 0) == 0);
    CHECK(px(glow, 1, 0, 1) == 99);
    CHECK(px(glow, 2, 0, 2) == 199);
    CHECK(px(glow, 0, 0, 3) == 255);
    CHECK(px(heightOf(colour), 0, 0, 0) == (54 * 9 + 183 * 99 + 19 * 199 + 128) >> 8);
}
