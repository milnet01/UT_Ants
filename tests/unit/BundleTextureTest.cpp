// UTA-0052's container cases: the TEXS section, its dimension rules and its
// block-length rule.
//
// docs/specs/UTA-0052-texture-memory-budget.md, INV-1, INV-2, INV-3, INV-4,
// INV-5 and INV-13. The encoder's own cases (INV-6 to INV-12 and INV-14) are
// tests/unit/MaterialCompressTest.cpp; nothing here reaches umat.
//
// EVERY FIXTURE IS A WHOLE .utab, and INV-1 forces that. UTA-0008 SS 4.2
// rule 1 refuses a payload larger than the bytes remaining in its section and
// SS 4.4 refuses a section that overruns the file, so a fixture built by
// truncating a valid bundle is killed by one of those before the per-texture
// length rule is reached -- and deleting INV-1 would leave such a case red
// anyway, which is a test that cannot fail for its own reason. Only a table
// and a section extent that are entirely correct, with an element whose
// declared block run disagrees with its own dimensions, can reach it.
//
// THE GOLDEN ARRAY IS AUTHORED FROM SS 4.3, never produced by `write`. A
// transposition present in both the reader and the writer round-trips
// perfectly, so a round-trip test cannot break INV-4. That is why INV-4 grades
// the reader against the bytes and INV-5 grades the writer against the same
// bytes: neither can be satisfied by a compensating error in the other.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "ubundle/Bundle.h"

#include "Bytes.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using uta::ErrorCode;
using uta::testing::Bytes;
using uta::ubundle::BlockFormat;
using uta::ubundle::Bundle;
using uta::ubundle::BundleKind;
using uta::ubundle::CompressedTexture;
using uta::ubundle::expectedBlockBytes;
using uta::ubundle::MAX_UPSCALE_FACTOR;
using uta::ubundle::Origin;
using uta::ubundle::read;
using uta::ubundle::write;

namespace {

/// One texture's fields, as the fixture chooses them. Separate from
/// CompressedTexture so a case can state a value the struct's own types or
/// the reader's rules would not permit -- a mipCount past the chain, a block
/// run one byte short.
struct TextureSpec {
    std::string name;
    std::uint8_t format = 2;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t sourceWidth = 0;
    std::uint16_t sourceHeight = 0;
    std::uint8_t mipCount = 1;
    /// The block run's declared length AND its actual length; the two are
    /// always equal here, so a case never tests the reader's bounds check by
    /// accident. INV-1 is reached by making this disagree with the DIMENSIONS.
    std::size_t blockBytes = 0;
    /// Distinguishes one texture's block bytes from another's, so INV-4 can
    /// tell them apart after decoding.
    std::uint8_t fill = 0;
};

/// The bytes one texture's block run holds. A formula rather than a literal:
/// the run is opaque to ubundle, so what matters is that it is reproducible
/// and distinct per texture.
std::vector<std::byte> blockRun(const TextureSpec& spec) {
    std::vector<std::byte> out;
    out.reserve(spec.blockBytes);
    for (std::size_t i = 0; i < spec.blockBytes; ++i)
        out.push_back(static_cast<std::byte>((spec.fill + i * 7U) & 0xFFU));
    return out;
}

/// One element, in SS 4.3's field order. The ORDER here is the layout claim.
void putTexture(Bytes& out, const TextureSpec& spec) {
    out.str(spec.name);
    out.u8(spec.format);
    out.u16(spec.width);
    out.u16(spec.height);
    out.u16(spec.sourceWidth);
    out.u16(spec.sourceHeight);
    out.u8(spec.mipCount);
    // The run, split into SS 4.3's levels, each stored uncompressed: a codec
    // byte of 0, a u32 length, the bytes (UTA-0512). Every level but the last
    // takes the size the dimensions give it, stated here from SS 4.3's
    // formula; the last takes what remains, so a run one byte short or long
    // makes the LAST level disagree with its dimensions, and INV-1 is reached.
    const std::vector<std::byte> run = blockRun(spec);
    const std::size_t levels = spec.mipCount > 0 ? spec.mipCount : 1;
    const std::size_t perBlock = spec.format == 0 ? 8 : 16;
    std::size_t at = 0;
    for (std::size_t level = 0; level < levels; ++level) {
        const std::size_t width = std::max<std::size_t>(1, spec.width >> level);
        const std::size_t height = std::max<std::size_t>(1, spec.height >> level);
        const std::size_t own = ((width + 3) / 4) * ((height + 3) / 4) * perBlock;
        const std::size_t size = level + 1 == levels ? run.size() - at : std::min(own, run.size() - at);
        out.u8(0);
        out.u32(static_cast<std::uint32_t>(size));
        for (std::size_t i = 0; i < size; ++i) out.u8(static_cast<std::uint8_t>(run[at + i]));
        at += size;
    }
}

/// A TEXS payload: SS 4.2's vector<CompressedTexture>.
Bytes texsPayload(const std::vector<TextureSpec>& textures) {
    Bytes out;
    out.u32(static_cast<std::uint32_t>(textures.size()));
    for (const TextureSpec& spec : textures) putTexture(out, spec);
    return out;
}

/// A whole .utab carrying exactly one TEXS section with this payload. The
/// table tiles the file, so every case fails on the ONE rule it states rather
/// than on the framing.
std::vector<std::byte> fileWithTexsPayload(const Bytes& payload) {
    Bytes out;
    out.id("UTAB");
    out.u32(39); // formatVersion -- 39 since UTA-0512 gave TEXS levels a codec
    out.u8(1);  // origin: Authored
    out.u8(0);  // kind: Map
    out.u16(0); // reserved
    out.u32(1); // sectionCount

    out.id("TEXS");
    out.u64(16 + 24); // offset: the table ends here
    out.u64(payload.size());
    out.u8(0); // compression: zero, and INV-13 asserts this position
    out.u8(0);
    out.u8(0);
    out.u8(0);

    out.append(payload);
    return out.data();
}

std::vector<std::byte> fileWithTextures(const std::vector<TextureSpec>& textures) {
    return fileWithTexsPayload(texsPayload(textures));
}

/// One BC7 texture of a single level whose bytes are stored under `codec`,
/// exactly as given -- UTA-0512's per-level framing, written by hand. 8x8 over
/// a 4x4 source (factor 2), so the level is four blocks: 64 bytes.
std::vector<std::byte> fileWithOneLevel(std::uint8_t codec, const std::vector<std::uint8_t>& stored) {
    Bytes payload;
    payload.u32(1);
    payload.str("d");
    payload.u8(2); // BC7
    payload.u16(8);
    payload.u16(8);
    payload.u16(4);
    payload.u16(4);
    payload.u8(1); // one level
    payload.u8(codec);
    payload.u32(static_cast<std::uint32_t>(stored.size()));
    for (const std::uint8_t value : stored) payload.u8(value);
    return fileWithTexsPayload(payload);
}

/// Sixty-four zero bytes as one LZ4 block, written from the LZ4 block format
/// by hand: a token of one literal and a long match (0x1F), the literal 0, a
/// match offset of 1, a length byte making the match 58, then a token of five
/// literals (0x50) and the five zeros the format requires a block to end on.
/// LZ4-HC at its default level writes exactly these bytes too, so this is also
/// what write must emit for that level, on every compiler.
const std::vector<std::uint8_t> LZ4_ZEROS_64 = {0x1F, 0x00, 0x01, 0x00, 0x27, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00};

/// The three textures INV-4 requires: one of each format, at least one with a
/// mip chain, and within each element the four consecutive u16 hold four
/// distinct values so that transposing any adjacent pair changes what decodes.
const TextureSpec ALPHA{"alpha", 2, 8, 2, 4, 1, 2, 48, 0x10};  // BC7, 2 levels
const TextureSpec BETA{"be", 1, 16, 4, 8, 2, 1, 64, 0x40};     // BC5, 1 level
const TextureSpec GAMMA{"gam", 0, 4, 16, 2, 8, 2, 48, 0x90};   // BC4, 2 levels

std::vector<std::byte> goldenTextureBundle() {
    return fileWithTextures({ALPHA, BETA, GAMMA});
}

/// `read` refused with the code UTA-0052's failure table gives.
void refused(const std::vector<std::byte>& bytes,
             ErrorCode code = ErrorCode::MalformedData) {
    const auto result = read(bytes);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == code);
}

/// A texture that satisfies every rule. Cases below vary ONE field of it, so
/// each failure is attributable to the field it changed.
TextureSpec sound() { return ALPHA; }

} // namespace

TEST_CASE("expectedBlockBytes sums every stored level and not the base alone",
          "[ubundle][texture]") {
    // The arithmetic INV-1 grades against. Stated here from SS 4.3's formula
    // so the reader's own sum is compared against a second statement of the
    // rule rather than against itself.
    CompressedTexture texture;
    texture.format = BlockFormat::BC7;
    texture.width = 8;
    texture.height = 2;
    texture.sourceWidth = 4;
    texture.sourceHeight = 1;

    // Level 0 is 8x2 -- two blocks of sixteen. Level 1 is 4x1 -- one block.
    texture.mipCount = 1;
    CHECK(expectedBlockBytes(texture) == 32);
    texture.mipCount = 2;
    CHECK(expectedBlockBytes(texture) == 48);

    // BC4 spends eight bytes on a block rather than sixteen.
    texture.format = BlockFormat::BC4;
    CHECK(expectedBlockBytes(texture) == 24);

    // A zero dimension yields zero, which is why INV-2 refuses one BEFORE
    // this is consulted: a zero-length payload would satisfy INV-1 on it.
    texture.width = 0;
    CHECK(expectedBlockBytes(texture) == 0);
}

TEST_CASE("a block run that disagrees with its own dimensions is refused",
          "[ubundle][texture]") {
    // INV-1, one case per format, each one byte short and one byte long. The
    // section extent is exactly consistent in every case -- see the file
    // header for why that is what makes these reach the rule at all.
    struct Case {
        const char* what;
        std::uint8_t format;
        std::size_t correct;
    };
    const Case cases[] = {
        {"BC4", 0, 24}, // 8x2 over two levels at eight bytes a block
        {"BC5", 1, 48}, // the same chain at sixteen
        {"BC7", 2, 48},
    };

    for (const Case& item : cases) {
        TextureSpec spec = sound();
        spec.format = item.format;

        SECTION(std::string("the declared length is correct for ") + item.what) {
            spec.blockBytes = item.correct;
            CHECK(read(fileWithTextures({spec})).has_value());
        }
        SECTION(std::string("one byte short for ") + item.what) {
            spec.blockBytes = item.correct - 1;
            refused(fileWithTextures({spec}));
        }
        SECTION(std::string("one byte long for ") + item.what) {
            spec.blockBytes = item.correct + 1;
            refused(fileWithTextures({spec}));
        }
    }
}

TEST_CASE("a texture whose dimensions break the format's rules is refused",
          "[ubundle][texture]") {
    // INV-2. Every clause has its own case, and each varies ONE field of a
    // texture that is otherwise sound.
    SECTION("a zero width") {
        TextureSpec spec = sound();
        spec.width = 0;
        // Its expected length is zero too, so INV-1 is satisfied by the empty
        // run -- which is exactly why a dimension check has to exist.
        spec.blockBytes = 0;
        refused(fileWithTextures({spec}));
    }
    SECTION("a zero height") {
        TextureSpec spec = sound();
        spec.height = 0;
        spec.blockBytes = 0;
        refused(fileWithTextures({spec}));
    }
    SECTION("a width that is not a power of two") {
        TextureSpec spec = sound();
        spec.width = 6;
        spec.sourceWidth = 3;
        // 48, not 32: 6x2 is two blocks and 3x1 is one, so three blocks of
        // sixteen. A short run here is refused by INV-1 first and the case
        // never reaches the rule it names -- measured, it survived the
        // mutation that deletes this check.
        spec.blockBytes = 48;
        refused(fileWithTextures({spec}));
    }
    SECTION("a width above the ceiling") {
        // Height 2 over a source height of 1, so the factor is 2 in BOTH
        // axes. At height 1 the two axes disagreed and that rule refused it
        // first -- measured: the case survived the mutation deleting the
        // ceiling. 16384x2 is still one block row, so the run is unchanged.
        TextureSpec spec = sound();
        spec.width = 16384;
        spec.height = 2;
        spec.sourceWidth = 8192;
        spec.sourceHeight = 1;
        spec.mipCount = 1;
        spec.blockBytes = 4096 * 16;
        refused(fileWithTextures({spec}));
    }
    SECTION("a height that is not a power of two") {
        // 8x6 is four blocks and 4x3 one, so five blocks of sixteen, and the
        // factor is 2 in both axes: only the power-of-two rule refuses it.
        TextureSpec spec = sound();
        spec.height = 6;
        spec.sourceHeight = 3;
        spec.blockBytes = 5 * 16;
        refused(fileWithTextures({spec}));
    }
    SECTION("a height above the ceiling") {
        TextureSpec spec = sound();
        spec.width = 2;
        spec.height = 16384;
        spec.sourceWidth = 1;
        spec.sourceHeight = 8192;
        spec.mipCount = 1;
        spec.blockBytes = 4096 * 16;
        refused(fileWithTextures({spec}));
    }
    SECTION("a mipCount of zero") {
        TextureSpec spec = sound();
        spec.mipCount = 0;
        spec.blockBytes = 0;
        refused(fileWithTextures({spec}));
    }
    SECTION("a mipCount one past the chain's length") {
        // 8x2 has four levels at most: 8x2, 4x1, 2x1, 1x1.
        TextureSpec spec = sound();
        spec.mipCount = 5;
        spec.blockBytes = 5 * 16;
        refused(fileWithTextures({spec}));
    }
    SECTION("a zero sourceWidth") {
        TextureSpec spec = sound();
        spec.sourceWidth = 0;
        refused(fileWithTextures({spec}));
    }
    SECTION("a zero sourceHeight") {
        TextureSpec spec = sound();
        spec.sourceHeight = 0;
        refused(fileWithTextures({spec}));
    }
    SECTION("a sourceWidth larger than the width") {
        // Integer division would make the factor 0 -- the fields would exist
        // and record nothing, which is the one thing they are stored for.
        TextureSpec spec = sound();
        spec.sourceWidth = 16;
        refused(fileWithTextures({spec}));
    }
    SECTION("two axes that disagree about the factor") {
        TextureSpec spec = sound();
        spec.sourceWidth = 8; // factor 1 on x
        spec.sourceHeight = 1; // factor 2 on y
        refused(fileWithTextures({spec}));
    }
    SECTION("a factor above the cap") {
        // 8 against a MAX_UPSCALE_FACTOR of 4.
        TextureSpec spec = sound();
        spec.width = 8;
        spec.height = 8;
        spec.sourceWidth = 1;
        spec.sourceHeight = 1;
        spec.mipCount = 1;
        spec.blockBytes = 4 * 16;
        REQUIRE(MAX_UPSCALE_FACTOR < 8);
        refused(fileWithTextures({spec}));
    }
    SECTION("a source that does not divide the stored size exactly") {
        // sourceWidth 3 against width 8. Every other clause is satisfied --
        // both stored edges are powers of two, neither source is zero, and
        // 8/3 and 2/1 both truncate to 2, so the two axes AGREE. Only the
        // exact-multiple check refuses it, which is what makes this case the
        // one that grades that check rather than a neighbour.
        TextureSpec spec = sound();
        spec.sourceWidth = 3;
        refused(fileWithTextures({spec}));
    }
}

TEST_CASE("a block format outside the defined set is refused and never defaulted",
          "[ubundle][texture]") {
    // INV-3. The payload is the length SIXTEEN bytes per block implies,
    // because bytesPerBlock returns 16 for everything that is not BC4 -- so a
    // fixture sized any other way is refused by INV-1 first and this case
    // would be vacuous.
    for (const std::uint8_t format : {std::uint8_t{3}, std::uint8_t{0xFF}}) {
        TextureSpec spec = sound();
        spec.format = format;
        spec.blockBytes = 48;
        refused(fileWithTextures({spec}));
    }
}

TEST_CASE("the golden texture bytes decode field by field to the values they encode",
          "[ubundle][texture]") {
    // INV-4.
    const auto result = read(goldenTextureBundle());
    REQUIRE(result.has_value());
    const Bundle& bundle = *result;

    CHECK(bundle.header.formatVersion == 39);
    CHECK(bundle.header.origin == Origin::Authored);
    CHECK(bundle.header.kind == BundleKind::Map);

    REQUIRE(bundle.textures.has_value());
    REQUIRE(bundle.textures->size() == 3);

    const std::vector<TextureSpec> expected = {ALPHA, BETA, GAMMA};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const CompressedTexture& got = (*bundle.textures)[i];
        const TextureSpec& want = expected[i];
        CHECK(got.name == want.name);
        CHECK(static_cast<std::uint8_t>(got.format) == want.format);
        CHECK(got.width == want.width);
        CHECK(got.height == want.height);
        CHECK(got.sourceWidth == want.sourceWidth);
        CHECK(got.sourceHeight == want.sourceHeight);
        CHECK(got.mipCount == want.mipCount);
        CHECK(got.blocks == blockRun(want));
        // The stored length is the one the dimensions imply -- INV-1 read
        // from the other side.
        CHECK(got.blocks.size() == expectedBlockBytes(got));
    }

    // The three formats, so no case is graded by a fixture that omits it.
    CHECK((*bundle.textures)[0].format == BlockFormat::BC7);
    CHECK((*bundle.textures)[1].format == BlockFormat::BC5);
    CHECK((*bundle.textures)[2].format == BlockFormat::BC4);
}

TEST_CASE("write reproduces the golden texture bytes exactly", "[ubundle][texture]") {
    // INV-5, and the cross-compiler check for the container half: the array
    // is fixed in source, so every leg of the matrix compares against one
    // constant and any leg whose bytes differ goes red on its own.
    const std::vector<std::byte> golden = goldenTextureBundle();
    const auto decoded = read(golden);
    REQUIRE(decoded.has_value());

    const auto written = write(*decoded);
    REQUIRE(written.has_value());
    CHECK(*written == golden);
}

TEST_CASE("UTA-0368: write refuses a texture read would refuse", "[ubundle][texture]") {
    // review-code 2026-10-15: TEXS is put straight into the file, so no
    // encoder ran its rules and write emitted what read then refused.
    const auto decoded = read(goldenTextureBundle());
    REQUIRE(decoded.has_value());
    SECTION("too few block bytes") {
        uta::ubundle::Bundle bundle = *decoded;
        (*bundle.textures)[0].blocks.pop_back();
        CHECK_FALSE(write(bundle).has_value());
    }
    SECTION("a mip count past its size") {
        uta::ubundle::Bundle bundle = *decoded;
        (*bundle.textures)[0].mipCount = 40;
        CHECK_FALSE(write(bundle).has_value());
    }
}

TEST_CASE("the TEXS descriptor's compression byte is zero", "[ubundle][texture]") {
    // INV-13. The format is carried per texture and never by the section, so
    // the descriptor byte stays zero. Its position is fixed by UTA-0008
    // SS 4.4: four id bytes, a u64 offset, a u64 size, then compression.
    const std::vector<std::byte> golden = goldenTextureBundle();
    constexpr std::size_t COMPRESSION = 16 + 4 + 8 + 8;
    REQUIRE(golden.size() > COMPRESSION);
    CHECK(golden[COMPRESSION] == std::byte{0});

    // The part no section-level byte could express: two formats in one
    // section, round-tripping. This is the case that distinguishes a
    // per-texture field from a per-section one.
    const auto result = read(golden);
    REQUIRE(result.has_value());
    REQUIRE(result->textures.has_value());
    CHECK((*result->textures)[0].format != (*result->textures)[2].format);
}

TEST_CASE("a TEXS section is optional and empty stays distinct from absent",
          "[ubundle][texture]") {
    // UTA-0008 SS 4.4's rule, applied to the new section. An absent TEXS says
    // nothing about the map's materials; a present but empty one says the map
    // was examined and produced none.
    const auto empty = read(fileWithTextures({}));
    REQUIRE(empty.has_value());
    REQUIRE(empty->textures.has_value());
    CHECK(empty->textures->empty());

    Bundle absent;
    absent.header.origin = Origin::Authored;
    const auto written = write(absent);
    REQUIRE(written.has_value());
    const auto back = read(*written);
    REQUIRE(back.has_value());
    CHECK_FALSE(back->textures.has_value());
}

TEST_CASE("UTA-0512: an LZ4 level decodes to the run it packs and writes back the same bytes",
          "[ubundle][texture]") {
    // The hand-written LZ4 block against the reader, then the writer against
    // the same bytes: the pair INV-4 and INV-5 make for a stored level, so a
    // compressor whose output differed between compilers goes red on its leg.
    const std::vector<std::byte> file = fileWithOneLevel(1, LZ4_ZEROS_64);
    const auto result = read(file);
    REQUIRE(result.has_value());
    REQUIRE(result->textures.has_value());
    REQUIRE(result->textures->size() == 1);
    CHECK((*result->textures)[0].blocks == std::vector<std::byte>(64, std::byte{0}));

    const auto written = write(*result);
    REQUIRE(written.has_value());
    CHECK(*written == file);
}

TEST_CASE("UTA-0512: an LZ4 level that decodes to another length is refused", "[ubundle][texture]") {
    // INV-1 on disk: the level's length comes from its dimensions, never
    // from what the LZ4 bytes happen to hold.
    SECTION("one byte short") {
        std::vector<std::uint8_t> stored = LZ4_ZEROS_64;
        stored[4] = 0x26; // a match of 57: 63 bytes in all
        refused(fileWithOneLevel(1, stored));
    }
    SECTION("one byte long") {
        std::vector<std::uint8_t> stored = LZ4_ZEROS_64;
        stored[4] = 0x28; // a match of 59: 65 bytes in all
        refused(fileWithOneLevel(1, stored));
    }
    SECTION("the same bytes marked stored") {
        // Eleven bytes where the dimensions require sixty-four.
        refused(fileWithOneLevel(0, LZ4_ZEROS_64));
    }
}

TEST_CASE("UTA-0512: a level codec other than 0 or 1 is refused", "[ubundle][texture]") {
    // Two fixtures, one per way a reader could misread codec 2. Sixty-four
    // plain bytes are a level a reader taking it as stored would accept; the
    // LZ4 block is one a reader taking it as LZ4 would. Only the codec rule
    // refuses both.
    CHECK(read(fileWithOneLevel(0, std::vector<std::uint8_t>(64, 0))).has_value());
    CHECK(read(fileWithOneLevel(1, LZ4_ZEROS_64)).has_value());
    refused(fileWithOneLevel(2, std::vector<std::uint8_t>(64, 0)));
    refused(fileWithOneLevel(2, LZ4_ZEROS_64));
}

TEST_CASE("UTA-0512: write packs a level only where LZ4 saves at least 5%", "[ubundle][texture]") {
    // One 128-byte level (BC7 8x16): the fixture's pattern, which LZ4 cannot
    // shrink, behind a run of zeros. Measured with LZ4-HC v1.11.1 on
    // 2026-11-25: twelve zeros pack to 122 bytes, 4.7% smaller, and thirteen
    // to 121, 5.5% smaller. So the first stays stored and the second packs.
    const auto codecWritten = [](std::size_t zeros) {
        CompressedTexture texture;
        texture.name = "d";
        texture.format = BlockFormat::BC7;
        texture.width = 8;
        texture.height = 16;
        texture.sourceWidth = 4;
        texture.sourceHeight = 8;
        texture.mipCount = 1;
        for (std::size_t i = 0; i < 128; ++i)
            texture.blocks.push_back(i < zeros ? std::byte{0} : static_cast<std::byte>((0x10 + i * 7) & 0xFFU));
        Bundle bundle;
        bundle.header.origin = Origin::Authored;
        bundle.textures = std::vector<CompressedTexture>{texture};
        const auto written = write(bundle);
        REQUIRE(written.has_value());
        // The header, one descriptor, the count, the name "d", the format,
        // four dimensions and mipCount, then the level's codec byte.
        constexpr std::size_t CODEC = 16 + 24 + 4 + (4 + 1) + 1 + 4 * 2 + 1;
        REQUIRE(written->size() > CODEC);
        // And the level reads back as it went in, whichever codec it took.
        const auto back = read(*written);
        REQUIRE(back.has_value());
        CHECK((*back->textures)[0].blocks == texture.blocks);
        return std::to_integer<int>((*written)[CODEC]);
    };
    CHECK(codecWritten(12) == 0);
    CHECK(codecWritten(13) == 1);
}
