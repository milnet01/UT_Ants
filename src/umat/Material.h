// umat: block-compress a texture, cap its upscale, and measure a map's
// texture working set against a budget.
//
// docs/specs/UTA-0052-texture-memory-budget.md.
//
// BUILD-TIME ONLY. docs/design.md rule 2 keeps this library out of every
// runtime target. It links uta_core, uta_ubundle and, since UTA-0009,
// uta_upkg, and NOTHING else -- INV-12, asserted at configure time in
// src/umat/CMakeLists.txt. Generating a material is Generate.h's.
//
// DETERMINISTIC. One chain and one format give one byte sequence on every
// compiler (INV-6) and every worker count (INV-7). The vendored encoders are
// compiled under the project's numeric contract, and nothing here adds a flag
// that undoes it (INV-8).
//
// NEVER DEGRADES. Over budget, a bake is refused and the report says why.
// Nothing here drops, resizes or re-compresses a texture to make it fit --
// SS 3 decision 3, and enforceBudget's signature is what holds it.

#pragma once

#include "core/Error.h"
#include "core/Jobs.h"
#include "ubundle/Bundle.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace uta::umat {

/// The largest edge umat will UPSCALE TO. Not a ceiling on the SOURCE: a
/// source already larger is passed through at factor 1 and never downscaled
/// -- SS 4.5.
inline constexpr std::uint32_t MAX_OUTPUT_EDGE = 1024;

// MAX_UPSCALE_FACTOR is NOT declared here. It lives in src/ubundle/Bundle.h
// beside CompressedTexture, because the reader enforces it (SS 4.8).

/// The factor actually applied to a source of these dimensions, given the
/// factor the caller asks for. Pure, and the same on every machine.
///
/// The largest power of two that is at most `requested`, at most
/// ubundle::MAX_UPSCALE_FACTOR, and leaves both output edges at or below
/// MAX_OUTPUT_EDGE. Where no factor qualifies it returns 1, and it never
/// returns 0 (INV-11).
[[nodiscard]] std::uint32_t upscaleFactor(std::uint32_t sourceWidth,
                                         std::uint32_t sourceHeight,
                                         std::uint32_t requested) noexcept;

/// Bytes of texture working set one baked map may occupy -- SS 4.6.
inline constexpr std::uint64_t TEXTURE_BUDGET_BYTES = 1024ull * 1024ull * 1024ull;

/// What one texture contributes.
struct TextureCost {
    std::string name;
    std::uint64_t bytes = 0;
};

struct BudgetReport {
    std::uint64_t workingSetBytes = 0;
    std::uint64_t budgetBytes = 0;
    /// Every texture, largest first, and never a top-N: a truncated list
    /// cannot say where an overspend came from. Ties go by name ascending, so
    /// the order does not depend on the input's (INV-14).
    std::vector<TextureCost> byTexture;
};

/// The sum of every stored level of every texture. Mips included (INV-9).
[[nodiscard]] std::uint64_t workingSet(
    std::span<const ubundle::CompressedTexture> textures) noexcept;

[[nodiscard]] BudgetReport measure(
    std::span<const ubundle::CompressedTexture> textures,
    std::uint64_t budgetBytes = TEXTURE_BUDGET_BYTES);

/// InvalidArgument when the working set exceeds the budget, its message
/// naming both figures (INV-10). Nothing is dropped, resized or
/// re-compressed: this takes the report and never sees a texture.
[[nodiscard]] Result<void> enforceBudget(const BudgetReport& report);

/// One level of a source image, 8-bit, row-major, no row padding. `channels`
/// is 1, 2 or 4; `pixels` holds width * height * channels bytes.
struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t channels = 0;
    std::vector<std::byte> pixels;
};

/// Compress one texture's mip chain. `levels[0]` is the base level and each
/// one after it is the previous halved in both axes, floored at 1 -- the
/// same relation SS 4.3 encodes, checked here so a bad chain is refused
/// before it reaches the container.
///
/// `sourceWidth`/`sourceHeight` are recorded verbatim into the result so the
/// applied upscale factor stays derivable (SS 4.3). Generating the levels and
/// deriving the five maps are UTA-0009's.
///
/// InvalidArgument for every input SS 4.9 lists, including a source pair
/// ubundle's reader would refuse: a bad texture cannot be produced by the
/// only writer there is and then blamed on the reader.
[[nodiscard]] Result<ubundle::CompressedTexture> compress(
    std::string name,
    std::span<const Image> levels,
    ubundle::BlockFormat format,
    std::uint16_t sourceWidth,
    std::uint16_t sourceHeight,
    JobSystem& jobs);

} // namespace uta::umat
