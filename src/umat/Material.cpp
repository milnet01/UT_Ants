// umat -- see Material.h.
//
// The vendored encoders are third_party/bc7enc/: BC7 from bc7enc.c, BC4 and
// BC5 from rgbcx.h. rgbcx.h is a single-header library, and its
// implementation is instantiated HERE and in no other translation unit.

#include "umat/Material.h"

#include "bc7enc.h"

// rgbcx.h calls memset, memcpy, fabs and fabsf unqualified and includes
// neither header, relying on <algorithm> to pull them in -- which current
// libstdc++ no longer does. The C headers, because they are the ones that
// guarantee the global names. Supplied here rather than by patching the
// vendored file, which would make it no longer the copy its README pins.
#include <math.h>
#include <string.h>

#define RGBCX_IMPLEMENTATION
#include "rgbcx.h"

#include <algorithm>
#include <array>
#include <bit>
#include <mutex>
#include <string>
#include <utility>

namespace uta::umat {

namespace {

/// SS 4.3's ceiling on a stored edge.
constexpr std::uint32_t MAX_EDGE = 8192;

/// The fewest source channels each format can be encoded from -- SS 4.9.
/// Zero for a value outside the enum, which the caller refuses.
[[nodiscard]] std::uint32_t minimumChannels(ubundle::BlockFormat format) noexcept {
    switch (format) {
    case ubundle::BlockFormat::BC4: return 1;
    case ubundle::BlockFormat::BC5: return 2;
    case ubundle::BlockFormat::BC7: return 3;
    }
    return 0;
}

/// Every refusal SS 4.9 lists, before a block is encoded.
[[nodiscard]] Result<void> validate(std::span<const Image> levels,
                                    ubundle::BlockFormat format,
                                    std::uint16_t sourceWidth,
                                    std::uint16_t sourceHeight) {
    const auto code = ErrorCode::InvalidArgument;
    if (levels.empty()) return fail(code, "compress: no levels");

    const std::uint32_t width = levels[0].width;
    const std::uint32_t height = levels[0].height;
    if (!std::has_single_bit(width) || width > MAX_EDGE || !std::has_single_bit(height)
        || height > MAX_EDGE)
        return fail(code, "compress: base level " + std::to_string(width) + "x"
                              + std::to_string(height)
                              + " is not a power of two in [1, 8192] in both axes");

    // log2(max(width, height)) + 1, which for a power of two is its bit width.
    // Checked before the loop, so the shift below never exceeds it.
    const auto maxLevels = static_cast<std::size_t>(std::bit_width(std::max(width, height)));
    if (levels.size() > maxLevels)
        return fail(code, "compress: " + std::to_string(levels.size())
                              + " levels where the chain holds at most "
                              + std::to_string(maxLevels));

    const std::uint32_t needed = minimumChannels(format);
    if (needed == 0)
        return fail(code, "compress: block format "
                              + std::to_string(static_cast<unsigned>(format))
                              + " is not BC4, BC5 or BC7");

    for (std::size_t l = 0; l < levels.size(); ++l) {
        const Image& level = levels[l];
        const std::uint32_t wantWidth = std::max<std::uint32_t>(1, width >> l);
        const std::uint32_t wantHeight = std::max<std::uint32_t>(1, height >> l);
        const std::string where = "compress: level " + std::to_string(l);
        if (level.width != wantWidth || level.height != wantHeight)
            return fail(code, where + " is " + std::to_string(level.width) + "x"
                                  + std::to_string(level.height) + " where the chain needs "
                                  + std::to_string(wantWidth) + "x" + std::to_string(wantHeight));
        if (level.channels != 1 && level.channels != 2 && level.channels != 4)
            return fail(code, where + " has " + std::to_string(level.channels)
                                  + " channels; an Image carries 1, 2 or 4");
        if (level.channels < needed)
            return fail(code, where + " has " + std::to_string(level.channels)
                                  + " channels and its format needs at least "
                                  + std::to_string(needed));
        if (level.pixels.size()
            != std::size_t{level.width} * level.height * level.channels)
            return fail(code, where + " holds " + std::to_string(level.pixels.size())
                                  + " bytes, which its dimensions and channels do not");
    }

    // INV-2's source clauses, refused on the WRITE side too -- SS 4.9. The
    // zero test comes first because the modulus below divides by it.
    if (sourceWidth == 0 || sourceHeight == 0)
        return fail(code, "compress: a source dimension is zero");
    if (width % sourceWidth != 0 || height % sourceHeight != 0)
        return fail(code, "compress: the base level is not an exact multiple of the source");
    const std::uint32_t factorX = width / sourceWidth;
    const std::uint32_t factorY = height / sourceHeight;
    if (factorX != factorY)
        return fail(code, "compress: the two axes disagree about the upscale factor");
    if (!std::has_single_bit(factorX) || factorX > ubundle::MAX_UPSCALE_FACTOR)
        return fail(code, "compress: upscale factor " + std::to_string(factorX)
                              + " is not a power of two in [1, "
                              + std::to_string(ubundle::MAX_UPSCALE_FACTOR) + "]");
    return {};
}

/// Both encoders fill global tables on init and only read them afterwards, so
/// one initialisation before the first encode is what makes the parallel
/// encode below safe. rgbcx::init's own header says it is not thread safe.
void initialiseEncoders() {
    static std::once_flag once;
    std::call_once(once, [] {
        rgbcx::init();
        bc7enc_compress_block_init();
    });
}

/// One 4x4 block of `level` as sixteen RGBA texels, the layout both encoders
/// read. A level smaller than a block repeats its last row and column, so the
/// texels past its edge are copies rather than zeros. Channels the level does
/// not carry are zero, and no format reads them.
[[nodiscard]] std::array<std::uint8_t, 64> gatherBlock(const Image& level,
                                                      std::uint32_t column,
                                                      std::size_t row) noexcept {
    std::array<std::uint8_t, 64> rgba{};
    for (std::uint32_t y = 0; y < 4; ++y) {
        const std::size_t sy = std::min<std::size_t>(row * 4 + y, level.height - 1);
        for (std::uint32_t x = 0; x < 4; ++x) {
            const std::size_t sx = std::min<std::size_t>(column * 4 + x, level.width - 1);
            const std::byte* texel =
                level.pixels.data() + (sy * level.width + sx) * level.channels;
            for (std::uint32_t c = 0; c < level.channels; ++c)
                rgba[(y * 4 + x) * 4 + c] = std::to_integer<std::uint8_t>(texel[c]);
        }
    }
    return rgba;
}

} // namespace

std::uint32_t upscaleFactor(std::uint32_t sourceWidth, std::uint32_t sourceHeight,
                            std::uint32_t requested) noexcept {
    // 1 is the floor rather than a result: a source edge already above
    // MAX_OUTPUT_EDGE gets 1 and is stored unreduced, because the edge limit
    // bounds UPSCALING and the budget is what refuses an over-size texture.
    std::uint32_t factor = 1;
    for (std::uint32_t next = 2; next <= ubundle::MAX_UPSCALE_FACTOR && next <= requested;
         next *= 2) {
        if (std::uint64_t{sourceWidth} * next > MAX_OUTPUT_EDGE
            || std::uint64_t{sourceHeight} * next > MAX_OUTPUT_EDGE)
            break;
        factor = next;
    }
    return factor;
}

std::uint64_t workingSet(std::span<const ubundle::CompressedTexture> textures) noexcept {
    // blocks is every stored level, so its length is exactly what the card
    // holds: BC data is uploaded as-is, with no decode and no second copy.
    std::uint64_t total = 0;
    for (const ubundle::CompressedTexture& texture : textures) total += texture.blocks.size();
    return total;
}

BudgetReport measure(std::span<const ubundle::CompressedTexture> textures,
                     std::uint64_t budgetBytes) {
    BudgetReport report;
    report.workingSetBytes = workingSet(textures);
    report.budgetBytes = budgetBytes;
    report.byTexture.reserve(textures.size());
    for (const ubundle::CompressedTexture& texture : textures)
        report.byTexture.push_back({texture.name, texture.blocks.size()});
    std::sort(report.byTexture.begin(), report.byTexture.end(),
              [](const TextureCost& a, const TextureCost& b) {
                  return a.bytes != b.bytes ? a.bytes > b.bytes : a.name < b.name;
              });
    return report;
}

Result<void> enforceBudget(const BudgetReport& report) {
    if (report.workingSetBytes > report.budgetBytes)
        return fail(ErrorCode::InvalidArgument,
                    "texture working set of " + std::to_string(report.workingSetBytes)
                        + " bytes exceeds the budget of " + std::to_string(report.budgetBytes)
                        + " bytes");
    return {};
}

Result<ubundle::CompressedTexture> compress(std::string name, std::span<const Image> levels,
                                            ubundle::BlockFormat format,
                                            std::uint16_t sourceWidth,
                                            std::uint16_t sourceHeight, JobSystem& jobs) {
    UTA_CHECK(validate(levels, format, sourceWidth, sourceHeight));
    initialiseEncoders();

    ubundle::CompressedTexture texture;
    texture.name = std::move(name);
    texture.format = format;
    texture.width = static_cast<std::uint16_t>(levels[0].width);
    texture.height = static_cast<std::uint16_t>(levels[0].height);
    texture.sourceWidth = sourceWidth;
    texture.sourceHeight = sourceHeight;
    texture.mipCount = static_cast<std::uint8_t>(levels.size());
    texture.blocks.resize(ubundle::expectedBlockBytes(texture));

    // SS 4.4: linear RGB error. The perceptual mode's colour-space transform
    // is not established to be free of platform maths.
    bc7enc_compress_block_params params;
    bc7enc_compress_block_params_init(&params);
    bc7enc_compress_block_params_init_linear_weights(&params);

    // One index per block row, each writing only its own range of `blocks`,
    // so the bytes are the same for any worker count (INV-7).
    const std::size_t blockBytes = ubundle::bytesPerBlock(format);
    std::size_t offset = 0;
    for (const Image& level : levels) {
        const std::uint32_t blocksX = (level.width + 3) / 4;
        const std::uint32_t blocksY = (level.height + 3) / 4;
        std::byte* const base = texture.blocks.data() + offset;
        const std::size_t threw = jobs.parallelFor(blocksY, [&](std::size_t row) {
            for (std::uint32_t column = 0; column < blocksX; ++column) {
                const std::array<std::uint8_t, 64> rgba = gatherBlock(level, column, row);
                void* const out = base + (row * blocksX + column) * blockBytes;
                switch (format) {
                case ubundle::BlockFormat::BC4: rgbcx::encode_bc4(out, rgba.data(), 4); break;
                case ubundle::BlockFormat::BC5: rgbcx::encode_bc5(out, rgba.data(), 0, 1, 4); break;
                case ubundle::BlockFormat::BC7: bc7enc_compress_block(out, rgba.data(), &params); break;
                }
            }
        });
        // A partially written texture is a wrong bundle presented as a good
        // one, so a body that threw fails the whole texture -- SS 6.
        if (threw != 0)
            return fail(ErrorCode::Unknown, "compress: " + std::to_string(threw)
                                                + " block rows of '" + texture.name
                                                + "' threw while encoding");
        offset += std::size_t{blocksX} * blocksY * blockBytes;
    }
    return texture;
}

} // namespace uta::umat
