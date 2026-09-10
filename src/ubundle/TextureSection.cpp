// The TEXS section: block-compressed textures --
// docs/specs/UTA-0052-texture-memory-budget.md SS 4.3, INV-1 to INV-3.

#include "Sections.h"

#include <bit>
#include <string>

namespace uta::ubundle {
namespace detail {
namespace {

/// UTA-0052 SS 4.3: a u32 length for an empty name, one u8 format, four u16
/// dimensions, one u8 mipCount, and a u32 count for an empty block run.
constexpr std::uint64_t MIN_TEXTURE = 18;

[[nodiscard]] constexpr bool isPowerOfTwo(std::uint32_t value) noexcept {
    return value != 0 && (value & (value - 1)) == 0;
}

/// UTA-0052's INV-2 -- every dimension rule, applied before a payload byte is
/// read.
///
/// The source clauses have no other grader anywhere. sourceWidth and
/// sourceHeight are read by nothing in this library: they exist so the upscale
/// cap stays auditable after a bake, so INV-1's length arithmetic never
/// touches them and a bundle carrying nonsense there would decode cleanly
/// under every other rule.
[[nodiscard]] Result<void> validateTextureFields(const CompressedTexture& texture) {
    constexpr std::uint32_t MAX_EDGE = 8192;
    const auto code = ErrorCode::MalformedData;

    if (!isPowerOfTwo(texture.width) || texture.width > MAX_EDGE)
        return fail(code, "TEXS: width " + std::to_string(texture.width)
                              + " is not a power of two in [1, 8192]");
    if (!isPowerOfTwo(texture.height) || texture.height > MAX_EDGE)
        return fail(code, "TEXS: height " + std::to_string(texture.height)
                              + " is not a power of two in [1, 8192]");

    // log2(max(width, height)) + 1, which for a power of two is exactly its
    // bit width. A mipCount past this walks the level loop off the payload.
    const std::uint32_t longest = texture.width > texture.height ? texture.width : texture.height;
    const std::uint32_t maxLevels = static_cast<std::uint32_t>(std::bit_width(longest));
    if (texture.mipCount < 1 || texture.mipCount > maxLevels)
        return fail(code, "TEXS: mipCount " + std::to_string(texture.mipCount)
                              + " is outside [1, " + std::to_string(maxLevels) + "]");

    // Not merely a validity rule: `width % sourceWidth` below divides by this,
    // so without the check a zero is undefined behaviour rather than a wrong
    // answer. Measured -- deleting it turns the dimension case into a
    // numerical exception rather than a refusal.
    if (texture.sourceWidth == 0 || texture.sourceHeight == 0)
        return fail(code, "TEXS: a source dimension is zero, so the upscale factor is not derivable");

    // Exact, equal in both axes, a power of two, and within the cap. Without
    // these a sourceWidth larger than width yields a factor of 0 under
    // integer division -- the fields would then exist and record nothing,
    // which is the one thing they are stored for.
    if (texture.width % texture.sourceWidth != 0 || texture.height % texture.sourceHeight != 0)
        return fail(code, "TEXS: the stored dimensions are not an exact multiple of the source");
    const std::uint32_t factorX = texture.width / texture.sourceWidth;
    const std::uint32_t factorY = texture.height / texture.sourceHeight;
    if (factorX != factorY)
        return fail(code, "TEXS: the two axes disagree about the upscale factor");
    // isPowerOfTwo here is IMPLIED by the two checks above and no fixture can
    // reach it: the divisors of a power of two are powers of two, so a width
    // that is one and a source that divides it exactly give a factor that is
    // one too. It states INV-2 literally rather than leaving a reader to
    // re-derive that, and it is named here so its untestability reads as
    // redundancy rather than as a gap in the tests. The CAP beside it is live.
    if (!isPowerOfTwo(factorX) || factorX > MAX_UPSCALE_FACTOR)
        return fail(code, "TEXS: upscale factor " + std::to_string(factorX)
                              + " is not a power of two in [1, "
                              + std::to_string(MAX_UPSCALE_FACTOR) + "]");

    return {};
}

/// One CompressedTexture -- UTA-0052 SS 4.3.
///
/// ORDER IS THE RULE HERE. The dimension checks (that item's INV-2) run
/// BEFORE the block run is read, and the length check (INV-1) immediately
/// after it. Neither subsumes the other: a zero width makes INV-1's expected
/// product zero, so a zero-length payload satisfies it, and only a dimension
/// check can refuse that texture.
[[nodiscard]] Result<CompressedTexture> readCompressedTexture(Cursor& cursor) {
    CompressedTexture texture;
    UTA_TRY(texture.name, readString(cursor));

    // INV-3, and never defaulted to a value. bytesPerBlock returns 16 for
    // everything that is not BC4, so an undefined byte reaching it grades the
    // wrong arithmetic: a payload sized for 16 bytes per block then satisfies
    // INV-1 and the texture decodes as a format that does not exist.
    UTA_TRY(const std::uint8_t format, cursor.readU8());
    if (format > static_cast<std::uint8_t>(BlockFormat::BC7))
        return fail(ErrorCode::MalformedData,
                    "TEXS: block format " + std::to_string(format) + " is not 0, 1 or 2");
    texture.format = static_cast<BlockFormat>(format);

    UTA_TRY(texture.width, cursor.readU16());
    UTA_TRY(texture.height, cursor.readU16());
    UTA_TRY(texture.sourceWidth, cursor.readU16());
    UTA_TRY(texture.sourceHeight, cursor.readU16());
    UTA_TRY(texture.mipCount, cursor.readU8());
    UTA_CHECK(validateTextureFields(texture));

    // A u32 count then that many opaque bytes -- SS 4.2's vector<u8>, read as
    // one run rather than element by element. With a minimum element size of
    // one byte, rule 1's division degenerates to `count > remaining()`, which
    // is exactly the bound readBytes applies; a per-byte decode loop would
    // check the same thing several million times for one texture.
    UTA_TRY(const std::uint32_t blockBytes, cursor.readU32());
    UTA_TRY(const std::span<const std::byte> raw, cursor.readBytes(blockBytes));
    texture.blocks.assign(raw.begin(), raw.end());

    // INV-1. This is what makes the section self-describing: without it a
    // reader takes the declared length on trust and starts the next element
    // wherever the last one happened to stop.
    const std::uint64_t expected = expectedBlockBytes(texture);
    if (texture.blocks.size() != expected)
        return fail(ErrorCode::MalformedData,
                    "TEXS: a texture declares " + std::to_string(texture.blocks.size())
                        + " block bytes; its own dimensions require " + std::to_string(expected));

    return texture;
}

void putCompressedTexture(Sink& sink, const CompressedTexture& texture) {
    sink.putString(texture.name);
    sink.putU8(static_cast<std::uint8_t>(texture.format));
    sink.putU16(texture.width);
    sink.putU16(texture.height);
    sink.putU16(texture.sourceWidth);
    sink.putU16(texture.sourceHeight);
    sink.putU8(texture.mipCount);
    sink.putU32(static_cast<std::uint32_t>(texture.blocks.size()));
    sink.append(texture.blocks);
}

} // namespace

Result<std::vector<CompressedTexture>> readTextures(Cursor& cursor) {
    return readVector<CompressedTexture>(cursor, MIN_TEXTURE, "textures", readCompressedTexture);
}

std::vector<std::byte> encodeTextures(const std::vector<CompressedTexture>& textures) {
    Sink sink;
    sink.putVector(textures, putCompressedTexture);
    return std::move(sink).take();
}

} // namespace detail

std::uint64_t expectedBlockBytes(const CompressedTexture& texture) noexcept {
    // Zero for a combination the format does not permit. UTA-0052's INV-2
    // refuses every one of those before this is reached on the read path, so
    // the zero is a floor for a hand-built structure rather than a state a
    // decoded texture can be in.
    if (texture.width == 0 || texture.height == 0 || texture.mipCount == 0) return 0;

    const std::uint64_t perBlock = bytesPerBlock(texture.format);
    std::uint64_t total = 0;
    // Consecutive halvings, floored at one -- SS 4.3. Written as a halving
    // rather than as `width >> level` because mipCount is a u8 and a shift of
    // 32 or more is undefined; this function is public and a caller need not
    // have passed INV-2 first.
    std::uint32_t width = texture.width;
    std::uint32_t height = texture.height;
    for (std::uint32_t level = 0; level < texture.mipCount; ++level) {
        total += static_cast<std::uint64_t>((width + 3) / 4) * ((height + 3) / 4) * perBlock;
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    return total;
}

} // namespace uta::ubundle
