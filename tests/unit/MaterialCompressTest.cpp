// UTA-0052's encoder and budget cases.
//
// docs/specs/UTA-0052-texture-memory-budget.md, INV-6, INV-7, INV-9, INV-10,
// INV-11 and INV-14, plus one case per refusal SS 4.9 lists for `compress`.
// INV-8 and INV-12 are configure-time assertions in src/umat/CMakeLists.txt;
// the container's own cases are tests/unit/BundleTextureTest.cpp.
//
// EVERY IMAGE IS SYNTHETIC. Nothing here reads a package, so the whole file
// runs on a clone with no Unreal Tournament install (S7).
//
// EACH REFUSAL CASE VARIES ONE INPUT of a call that is otherwise sound, and
// chooses the rest so that no NEIGHBOURING rule refuses it too. A case that
// two rules refuse survives the deletion of either, which is the defect
// mutation found twice on the container half.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "umat/Material.h"

#include "core/Jobs.h"
#include "ubundle/Bundle.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <thread>
#include <vector>

using uta::ErrorCode;
using uta::JobSystem;
using uta::ubundle::BlockFormat;
using uta::ubundle::CompressedTexture;
using uta::umat::BudgetReport;
using uta::umat::compress;
using uta::umat::enforceBudget;
using uta::umat::Image;
using uta::umat::measure;
using uta::umat::MAX_OUTPUT_EDGE;
using uta::umat::TEXTURE_BUDGET_BYTES;
using uta::umat::upscaleFactor;
using uta::umat::workingSet;

namespace {

/// A texel, as a pure function of where it is. Varied enough that neighbouring
/// blocks choose different encodings, and integer-only so every compiler
/// builds the same image.
std::uint8_t texel(std::uint32_t x, std::uint32_t y, std::uint32_t channel, std::size_t level) {
    return static_cast<std::uint8_t>(
        (x * 37U + y * 91U + channel * 53U + level * 17U + x * y * 13U) & 0xFFU);
}

Image image(std::uint32_t width, std::uint32_t height, std::uint8_t channels, std::size_t level) {
    Image out;
    out.width = width;
    out.height = height;
    out.channels = channels;
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x)
            for (std::uint32_t c = 0; c < channels; ++c)
                out.pixels.push_back(static_cast<std::byte>(texel(x, y, c, level)));
    return out;
}

/// `count` levels from a base of width x height, each the previous halved and
/// floored at 1 -- the relation compress checks.
std::vector<Image> chain(std::uint32_t width, std::uint32_t height, std::uint8_t channels,
                         std::size_t count) {
    std::vector<Image> out;
    for (std::size_t l = 0; l < count; ++l)
        out.push_back(image(std::max<std::uint32_t>(1, width >> l),
                            std::max<std::uint32_t>(1, height >> l), channels, l));
    return out;
}

std::vector<std::byte> bytes(std::initializer_list<unsigned> values) {
    std::vector<std::byte> out;
    for (const unsigned value : values) out.push_back(static_cast<std::byte>(value));
    return out;
}

/// SS 4.3's formula, stated a second time here so the working set is graded
/// against the rule rather than against the code that implements it.
std::uint64_t chainBytes(std::uint32_t width, std::uint32_t height, std::size_t levels,
                         std::uint64_t bytesPerBlock) {
    std::uint64_t total = 0;
    for (std::size_t l = 0; l < levels; ++l) {
        const std::uint64_t w = std::max<std::uint32_t>(1, width >> l);
        const std::uint64_t h = std::max<std::uint32_t>(1, height >> l);
        total += ((w + 3) / 4) * ((h + 3) / 4) * bytesPerBlock;
    }
    return total;
}

/// A texture of a given stored size, for the budget cases. Its blocks are not
/// a real encoding; the budget reads their length and nothing else.
CompressedTexture sized(std::string name, std::size_t bytes) {
    CompressedTexture out;
    out.name = std::move(name);
    out.blocks.resize(bytes);
    return out;
}

/// compress refused with InvalidArgument.
void refused(const std::vector<Image>& levels, BlockFormat format, std::uint16_t sourceWidth,
             std::uint16_t sourceHeight) {
    JobSystem jobs(1);
    const auto result = compress("t", levels, format, sourceWidth, sourceHeight, jobs);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::InvalidArgument);
}

// INV-6's golden arrays: a 16x16 base and its full five-level chain, source
// 8x8. Captured once, on GCC, from the vendored copy third_party/bc7enc's
// README pins; every leg of the CI matrix compares against the same literal.
// Updating that copy means regenerating these, which is INV-6's third
// breaking case rather than a failure to explain away.
//
// BC5's first eight bytes equal BC4's: a BC5 block is two BC4 blocks, and
// channel 0 is the same image in both fixtures.
const std::vector<std::byte> GOLDEN_BC4 = bytes({
    0xf5, 0x00, 0xb9, 0x5b, 0x0e, 0xc3, 0x9b, 0x0e, 0xf1, 0x03, 0x9c, 0x72,
    0x73, 0xc3, 0x6b, 0xea, 0xed, 0x09, 0x77, 0x89, 0x9b, 0xc3, 0x5b, 0xc5,
    0xed, 0x06, 0x42, 0x3e, 0xdc, 0xc3, 0x3b, 0x98, 0xfd, 0x08, 0xdd, 0xbb,
    0x13, 0x67, 0x5a, 0x14, 0xfd, 0x29, 0x4a, 0xe5, 0xa5, 0xb0, 0x5e, 0x14,
    0xfd, 0x13, 0xa7, 0x0e, 0xef, 0x3b, 0x53, 0x14, 0xfd, 0x4a, 0x45, 0x5a,
    0x7c, 0xc6, 0x65, 0x18, 0xf2, 0x01, 0x2a, 0x78, 0x35, 0xb4, 0x88, 0x39,
    0xd5, 0x0c, 0xd9, 0xb5, 0xa3, 0x07, 0xaf, 0x1e, 0xf1, 0x03, 0x56, 0x91,
    0x59, 0xea, 0x47, 0x63, 0xc1, 0x01, 0xcc, 0xce, 0xc8, 0x8c, 0xda, 0x80,
    0xfa, 0x05, 0x8e, 0xc8, 0x3a, 0xd0, 0x68, 0x3f, 0xf7, 0x09, 0x8e, 0xa8,
    0xd5, 0xf5, 0x01, 0x69, 0xdd, 0x0d, 0x0e, 0x18, 0x8c, 0x18, 0x3b, 0xd6,
    0xf4, 0x05, 0x8e, 0xe8, 0x60, 0x3e, 0xf4, 0x07, 0xd0, 0x02, 0x77, 0xc9,
    0x21, 0x88, 0x79, 0x2a, 0xef, 0x02, 0x13, 0xfe, 0x52, 0x8a, 0xe9, 0xc1,
    0xd8, 0x0a, 0xc4, 0x09, 0x2f, 0xdf, 0xc9, 0x30, 0xff, 0x0e, 0x3a, 0x55,
    0x9d, 0xa8, 0xcc, 0x30, 0xe1, 0x13, 0x77, 0xc9, 0x21, 0x88, 0x79, 0x2a,
    0xc0, 0x33, 0xb1, 0x3d, 0x00, 0x03, 0x30, 0x00, 0x44, 0x44, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
});
const std::vector<std::byte> GOLDEN_BC5 = bytes({
    0xf5, 0x00, 0xb9, 0x5b, 0x0e, 0xc3, 0x9b, 0x0e, 0xf4, 0x26, 0x77, 0xc9,
    0x21, 0x88, 0x79, 0x2a, 0xf1, 0x03, 0x9c, 0x72, 0x73, 0xc3, 0x6b, 0xea,
    0xee, 0x0e, 0x42, 0x6e, 0x0e, 0x78, 0x57, 0xc5, 0xed, 0x09, 0x77, 0x89,
    0x9b, 0xc3, 0x5b, 0xc5, 0xf2, 0x20, 0xee, 0x14, 0x77, 0x88, 0x49, 0x9c,
    0xed, 0x06, 0x42, 0x3e, 0xdc, 0xc3, 0x3b, 0x98, 0xf1, 0x16, 0xc8, 0x8d,
    0xb8, 0x8a, 0xa9, 0x74, 0xfd, 0x08, 0xdd, 0xbb, 0x13, 0x67, 0x5a, 0x14,
    0xfc, 0x2e, 0xc4, 0x09, 0x2f, 0xdf, 0xc9, 0x30, 0xfd, 0x29, 0x4a, 0xe5,
    0xa5, 0xb0, 0x5e, 0x14, 0xfa, 0x05, 0xe9, 0x42, 0x54, 0x67, 0xba, 0xef,
    0xfd, 0x13, 0xa7, 0x0e, 0xef, 0x3b, 0x53, 0x14, 0xf8, 0x1b, 0x5e, 0x7a,
    0xc2, 0xb0, 0xbe, 0xef, 0xfd, 0x4a, 0x45, 0x5a, 0x7c, 0xc6, 0x65, 0x18,
    0xf6, 0x14, 0x7a, 0xb5, 0x13, 0x3b, 0xb3, 0xef, 0xf2, 0x01, 0x2a, 0x78,
    0x35, 0xb4, 0x88, 0x39, 0xd2, 0x02, 0xd9, 0xd5, 0xcc, 0xe0, 0x75, 0xd0,
    0xd5, 0x0c, 0xd9, 0xb5, 0xa3, 0x07, 0xaf, 0x1e, 0xee, 0x04, 0x56, 0xa1,
    0x66, 0xcd, 0x8c, 0x39, 0xf1, 0x03, 0x56, 0x91, 0x59, 0xea, 0x47, 0x63,
    0xfb, 0x02, 0x0d, 0x6f, 0x35, 0x98, 0xb1, 0x5e, 0xc1, 0x01, 0xcc, 0xce,
    0xc8, 0x8c, 0xda, 0x80, 0xf6, 0x36, 0xcc, 0xce, 0xc8, 0x8c, 0xda, 0x80,
    0xfa, 0x05, 0x8e, 0xc8, 0x3a, 0xd0, 0x68, 0x3f, 0xfb, 0x0a, 0x3d, 0x26,
    0xd6, 0x8f, 0xc6, 0xda, 0xf7, 0x09, 0x8e, 0xa8, 0xd5, 0xf5, 0x01, 0x69,
    0xff, 0x0c, 0x3d, 0x16, 0xad, 0xac, 0xff, 0x40, 0xdd, 0x0d, 0x0e, 0x18,
    0x8c, 0x18, 0x3b, 0xd6, 0xe2, 0x03, 0x74, 0xe4, 0x60, 0x81, 0x26, 0x8d,
    0xf4, 0x05, 0x8e, 0xe8, 0x60, 0x3e, 0xf4, 0x07, 0xe6, 0x04, 0x74, 0x44,
    0x1f, 0xf5, 0x53, 0xfb, 0xd0, 0x02, 0x77, 0xc9, 0x21, 0x88, 0x79, 0x2a,
    0xfc, 0x05, 0x2e, 0x47, 0xe5, 0x70, 0x67, 0xc2, 0xef, 0x02, 0x13, 0xfe,
    0x52, 0x8a, 0xe9, 0xc1, 0xff, 0x1f, 0x42, 0x6e, 0x0e, 0x78, 0x57, 0xc5,
    0xd8, 0x0a, 0xc4, 0x09, 0x2f, 0xdf, 0xc9, 0x30, 0xdb, 0x0b, 0x4a, 0x15,
    0xc2, 0x85, 0x25, 0xcb, 0xff, 0x0e, 0x3a, 0x55, 0x9d, 0xa8, 0xcc, 0x30,
    0xd7, 0x0b, 0xa9, 0xbe, 0x10, 0xdf, 0x29, 0xcb, 0xe1, 0x13, 0x77, 0xc9,
    0x21, 0x88, 0x79, 0x2a, 0xe4, 0x00, 0xe5, 0x34, 0xdc, 0x31, 0x55, 0xc5,
    0xc0, 0x33, 0xb1, 0x3d, 0x00, 0x03, 0x30, 0x00, 0xf5, 0x68, 0xb1, 0x3d,
    0x00, 0x03, 0x30, 0x00, 0x44, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x79, 0x79, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
});
const std::vector<std::byte> GOLDEN_BC7 = bytes({
    0x80, 0x60, 0xb1, 0xf5, 0x31, 0x9e, 0xac, 0x1c, 0x8f, 0x67, 0x65, 0xc7,
    0x80, 0x79, 0x86, 0x69, 0x80, 0x91, 0x64, 0x29, 0xdf, 0x15, 0x78, 0x86,
    0x8c, 0x10, 0x3c, 0x57, 0x39, 0xcf, 0x39, 0xf3, 0x80, 0x1f, 0xa8, 0xef,
    0x2e, 0x36, 0xaf, 0x9c, 0x5b, 0xe0, 0x3c, 0x96, 0xce, 0x12, 0x1f, 0x8c,
    0x80, 0x40, 0xe5, 0xae, 0xf0, 0x51, 0xd9, 0x50, 0x5b, 0x26, 0x28, 0xf2,
    0xf9, 0xf9, 0x79, 0x78, 0x80, 0x86, 0x35, 0xfa, 0x75, 0x56, 0xac, 0x7b,
    0xab, 0x0f, 0x4c, 0xc9, 0xe7, 0x79, 0xbe, 0x67, 0x80, 0xa7, 0x7d, 0xfd,
    0xac, 0x4b, 0xdc, 0xe1, 0x8b, 0x56, 0x12, 0x37, 0x78, 0xee, 0x99, 0x66,
    0x20, 0x9f, 0x7b, 0x6c, 0xfa, 0xed, 0xcc, 0xab, 0x48, 0x64, 0x66, 0x32,
    0x2d, 0x61, 0x76, 0x77, 0x80, 0xa0, 0x6c, 0xde, 0x66, 0x5a, 0xba, 0xf1,
    0x6c, 0x00, 0x27, 0x15, 0xe0, 0xe5, 0x45, 0x44, 0x20, 0xe3, 0xd1, 0x43,
    0xba, 0x49, 0x63, 0x47, 0x98, 0x65, 0x97, 0x69, 0xdf, 0x78, 0xd3, 0x49,
    0x80, 0x06, 0x99, 0x50, 0xae, 0x4c, 0x78, 0x04, 0xed, 0xe1, 0x03, 0xc9,
    0x9b, 0x07, 0x41, 0x16, 0x80, 0x84, 0xc1, 0x95, 0x33, 0xc9, 0x07, 0x55,
    0x23, 0xe1, 0x93, 0xe0, 0x11, 0x99, 0x17, 0x64, 0x80, 0x60, 0xab, 0x2e,
    0xd0, 0x5d, 0x0f, 0x1f, 0xdb, 0x19, 0xe6, 0xae, 0x19, 0x79, 0xf9, 0x7a,
    0x20, 0x0f, 0xb6, 0xe7, 0xed, 0x94, 0x9d, 0x77, 0x64, 0x2f, 0x6c, 0x0b,
    0xe4, 0x4f, 0xe9, 0x50, 0x80, 0x8f, 0xc9, 0xb6, 0x2f, 0xdb, 0x09, 0x6d,
    0x44, 0xe1, 0xa2, 0x62, 0x61, 0x5d, 0x04, 0x1f, 0x20, 0x34, 0xa7, 0x38,
    0x83, 0xa7, 0x90, 0x3b, 0x74, 0x5c, 0x96, 0xa0, 0xe4, 0xe5, 0xfa, 0x3e,
    0x80, 0x58, 0xe9, 0x7a, 0x32, 0x22, 0xbc, 0x5c, 0xc5, 0xe6, 0xe8, 0x0e,
    0xc0, 0xb0, 0x30, 0xc0, 0x20, 0x0c, 0x30, 0xc7, 0x6e, 0x0c, 0xb2, 0x5f,
    0x28, 0x3d, 0x26, 0x71, 0xc2, 0x6b, 0xc6, 0x6d, 0x20, 0x63, 0xcd, 0x79,
    0x55, 0xc1, 0x42, 0x78, 0xa3, 0x17, 0x78, 0xa7, 0xa7, 0x53, 0x39, 0xe7,
    0x80, 0xc6, 0x99, 0x65, 0xde, 0xc1, 0x74, 0x4f, 0x8a, 0x3f, 0x3a, 0x89,
    0xb9, 0x2f, 0x8a, 0x67, 0x20, 0x67, 0xd4, 0xc4, 0xfa, 0x71, 0x8f, 0x7c,
    0x73, 0xc6, 0x98, 0x77, 0x8f, 0x34, 0xe2, 0x88, 0x80, 0x5f, 0xc1, 0x90,
    0x31, 0x75, 0x91, 0x34, 0xdd, 0x65, 0x46, 0xea, 0x00, 0xfc, 0x03, 0x33,
    0x80, 0x8e, 0x51, 0x37, 0xb6, 0xe8, 0xf1, 0xbc, 0xc5, 0x6b, 0xbf, 0x8a,
    0xfb, 0x07, 0x06, 0x06, 0x40, 0x11, 0xa8, 0x87, 0xbb, 0x42, 0xe5, 0x6a,
    0x12, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
});

} // namespace

TEST_CASE("compress produces the golden block bytes for each format", "[umat][compress]") {
    // INV-6, and the cross-compiler check for the encoder: one constant per
    // format, compared on GCC, Clang and MSVC alike.
    struct Case {
        const char* what;
        BlockFormat format;
        std::uint8_t channels;
        const std::vector<std::byte>* golden;
    };
    const Case cases[] = {
        {"BC4", BlockFormat::BC4, 1, &GOLDEN_BC4},
        {"BC5", BlockFormat::BC5, 2, &GOLDEN_BC5},
        {"BC7", BlockFormat::BC7, 4, &GOLDEN_BC7},
    };

    JobSystem jobs(2);
    for (const Case& item : cases) {
        SECTION(item.what) {
            const auto result = compress(std::string("golden-") + item.what,
                                         chain(16, 16, item.channels, 5), item.format, 8, 8, jobs);
            REQUIRE(result.has_value());
            const CompressedTexture& texture = *result;

            // The fields the container stores, recorded as given.
            CHECK(texture.name == std::string("golden-") + item.what);
            CHECK(texture.format == item.format);
            CHECK(texture.width == 16);
            CHECK(texture.height == 16);
            CHECK(texture.sourceWidth == 8);
            CHECK(texture.sourceHeight == 8);
            CHECK(texture.mipCount == 5);
            CHECK(texture.blocks == *item.golden);

            // The only reader there is accepts what the only writer produced.
            uta::ubundle::Bundle bundle;
            bundle.header.origin = uta::ubundle::Origin::Authored;
            bundle.textures = std::vector<CompressedTexture>{texture};
            const auto written = uta::ubundle::write(bundle);
            REQUIRE(written.has_value());
            CHECK(uta::ubundle::read(*written).has_value());
        }
    }
}

TEST_CASE("compress output does not depend on the worker count", "[umat][compress]") {
    // INV-7. Three counts including 1: a pool of one runs every block on one
    // thread, so a scratch buffer shared between blocks gives a CONSISTENT
    // wrong answer there, and two multi-worker runs would agree with each
    // other and with nothing else. 64x64 is enough blocks to spread across a
    // pool and small enough for the suite's 30-second timeout.
    struct Case {
        const char* what;
        BlockFormat format;
        std::uint8_t channels;
    };
    const Case cases[] = {
        {"BC4", BlockFormat::BC4, 1},
        {"BC5", BlockFormat::BC5, 2},
        {"BC7", BlockFormat::BC7, 4},
    };

    JobSystem one(1);
    JobSystem two(2);
    JobSystem all(std::thread::hardware_concurrency());
    for (const Case& item : cases) {
        SECTION(item.what) {
            const std::vector<Image> levels = chain(64, 64, item.channels, 7);
            const auto a = compress("w", levels, item.format, 32, 32, one);
            const auto b = compress("w", levels, item.format, 32, 32, two);
            const auto c = compress("w", levels, item.format, 32, 32, all);
            REQUIRE(a.has_value());
            REQUIRE(b.has_value());
            REQUIRE(c.has_value());
            CHECK(a->blocks == b->blocks);
            CHECK(a->blocks == c->blocks);
        }
    }
}

TEST_CASE("compress refuses every input its contract lists", "[umat][compress]") {
    // SS 4.9's refusals. The sound call beside each is an 8x8 base, its full
    // four-level chain, four channels, BC7, and a source of 4x4 -- factor 2.
    SECTION("the sound call succeeds") {
        JobSystem jobs(1);
        CHECK(compress("t", chain(8, 8, 4, 4), BlockFormat::BC7, 4, 4, jobs).has_value());
    }
    SECTION("no levels") { refused({}, BlockFormat::BC7, 4, 4); }
    SECTION("a base width that is not a power of two") {
        // Source 3x4 so the factor is an exact 2 in both axes: only the
        // power-of-two rule refuses this.
        refused(chain(6, 8, 4, 4), BlockFormat::BC7, 3, 4);
    }
    SECTION("a base width above 8192") {
        refused(chain(16384, 1, 1, 1), BlockFormat::BC4, 8192, 1);
    }
    SECTION("one level more than the chain holds") {
        refused(chain(8, 8, 4, 5), BlockFormat::BC7, 4, 4);
    }
    SECTION("a level that is not the previous halved") {
        std::vector<Image> levels = chain(8, 8, 4, 4);
        levels[1] = image(5, 4, 4, 1); // 4x4 expected; pixels match its OWN size
        refused(levels, BlockFormat::BC7, 4, 4);
    }
    SECTION("three channels") {
        // Enough for BC7's minimum, so only the 1-2-or-4 rule refuses it.
        refused(chain(8, 8, 3, 4), BlockFormat::BC7, 4, 4);
    }
    SECTION("fewer channels than BC5 needs") {
        refused(chain(8, 8, 1, 4), BlockFormat::BC5, 4, 4);
    }
    SECTION("fewer channels than BC7 needs") {
        refused(chain(8, 8, 2, 4), BlockFormat::BC7, 4, 4);
    }
    SECTION("pixels one byte short of the dimensions") {
        std::vector<Image> levels = chain(8, 8, 4, 4);
        levels[0].pixels.pop_back();
        refused(levels, BlockFormat::BC7, 4, 4);
    }
    SECTION("a block format outside the enum") {
        refused(chain(8, 8, 4, 4), static_cast<BlockFormat>(3), 4, 4);
    }
    SECTION("a zero sourceWidth") { refused(chain(8, 8, 4, 4), BlockFormat::BC7, 0, 4); }
    SECTION("a zero sourceHeight") { refused(chain(8, 8, 4, 4), BlockFormat::BC7, 4, 0); }
    SECTION("a source that does not divide the base exactly") {
        // 8/3 and 8/4 both truncate to 2, so the axes AGREE and the factor is
        // within the cap: only the exact-multiple rule refuses this.
        refused(chain(8, 8, 4, 4), BlockFormat::BC7, 3, 4);
    }
    SECTION("two axes that disagree about the factor") {
        refused(chain(8, 8, 4, 4), BlockFormat::BC7, 8, 4);
    }
    SECTION("a factor above the cap") {
        // 8 against a MAX_UPSCALE_FACTOR of 4. The power-of-two clause beside
        // the cap is implied by the checks before it and no input reaches it.
        refused(chain(8, 8, 4, 4), BlockFormat::BC7, 1, 1);
    }
}

TEST_CASE("the working set counts every stored level", "[umat][budget]") {
    // INV-9. A full chain is four thirds of its base level, so a sum of the
    // base alone reports three quarters of the truth.
    JobSystem jobs(1);
    const auto full = compress("full", chain(16, 16, 4, 5), BlockFormat::BC7, 16, 16, jobs);
    const auto single = compress("single", chain(8, 8, 1, 1), BlockFormat::BC4, 8, 8, jobs);
    REQUIRE(full.has_value());
    REQUIRE(single.has_value());

    const std::uint64_t expected = chainBytes(16, 16, 5, 16) + chainBytes(8, 8, 1, 8);
    CHECK(expected == 368 + 32); // 16+4+1+1+1 blocks of 16, then 4 blocks of 8
    const std::vector<CompressedTexture> set = {*full, *single};
    CHECK(workingSet(set) == expected);
}

TEST_CASE("the budget refuses one byte over and accepts exactly at it", "[umat][budget]") {
    // INV-10. The at-budget case is what catches `>=`; the two one-byte
    // cases catch an inverted sense between them.
    constexpr std::uint64_t budget = 1000;
    CHECK(enforceBudget(BudgetReport{budget - 1, budget, {}}).has_value());
    CHECK(enforceBudget(BudgetReport{budget, budget, {}}).has_value());

    const auto over = enforceBudget(BudgetReport{budget + 1, budget, {}});
    REQUIRE_FALSE(over.has_value());
    CHECK(over.error().code() == ErrorCode::InvalidArgument);
    // SS 4.6: the message names both figures.
    const std::string message(over.error().message());
    CHECK(message.find("1001") != std::string::npos);
    CHECK(message.find("1000") != std::string::npos);
}

TEST_CASE("the upscale factor follows the cap rule word for word", "[umat][upscale]") {
    // INV-11, SS 4.5's cases.
    CHECK(upscaleFactor(512, 512, 4) == 2);   // the edge limit binds
    CHECK(upscaleFactor(1024, 1024, 4) == 1); // already at the edge
    CHECK(upscaleFactor(64, 64, 8) == 4);     // MAX_UPSCALE_FACTOR binds
    CHECK(upscaleFactor(64, 64, 3) == 2);     // rounds down to a power of two
    CHECK(upscaleFactor(64, 64, 0) == 1);     // 1 is the floor, never 0

    // The case that separates a bound on the UPSCALE from a clamp on the
    // RESULT: a source above the edge is passed through at 1 and stored
    // unreduced, never scaled down to fit.
    const std::uint32_t factor = upscaleFactor(2048, 2048, 4);
    CHECK(factor == 1);
    CHECK(2048 * factor == 2048);
    REQUIRE(MAX_OUTPUT_EDGE < 2048);
}

TEST_CASE("measure orders every texture by size and then by name", "[umat][budget]") {
    // INV-14. Input order differs from the expected order, and the tied pair
    // arrives in reverse name order, so neither can pass by accident.
    const std::vector<CompressedTexture> set = {
        sized("mid", 32), sized("tie-b", 16), sized("big", 64), sized("tie-a", 16),
        sized("small", 8),
    };
    const BudgetReport report = measure(set, 500);

    CHECK(report.workingSetBytes == 136);
    CHECK(report.budgetBytes == 500);
    REQUIRE(report.byTexture.size() == 5);
    const char* names[] = {"big", "mid", "tie-a", "tie-b", "small"};
    const std::uint64_t sizes[] = {64, 32, 16, 16, 8};
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK(report.byTexture[i].name == names[i]);
        CHECK(report.byTexture[i].bytes == sizes[i]);
    }

    // The default is the project's budget, not zero.
    CHECK(measure(set).budgetBytes == TEXTURE_BUDGET_BYTES);
}
