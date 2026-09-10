// umat: the maps derived from a material's base colour -- height, normal,
// roughness and emissive -- and the mip chain every map carries.
//
// docs/specs/UTA-0009-material-from-texture.md SS 4.4 and SS 4.5.
//
// INTEGER ARITHMETIC THROUGHOUT, the normal's square root included, so every
// compiler produces the same bytes (INV-6) and no maths library is involved.
//
// Each function takes one level and returns one level, so a test can check a
// stage before compression. PRECONDITION, not checked: the input is a
// well-formed Image of the channel count each names. generate() validates its
// base before calling any of them.

#pragma once

#include "umat/Material.h"

#include <cstdint>
#include <vector>

namespace uta::umat {

namespace detail {

/// SS 4.4's normal strength `s`. A starting value -- SS 15.
inline constexpr std::int64_t NORMAL_STRENGTH = 1;

/// The integer square root of `n`, rounded down.
[[nodiscard]] std::uint64_t isqrt(std::uint64_t n) noexcept;

} // namespace detail

/// `level` and every level below it to 1x1 -- bit_width(max(width, height))
/// levels in all. Each is the 2x2 box average of the one above,
/// (a + b + c + d + 2) >> 2 per channel, floored at one texel per axis.
[[nodiscard]] std::vector<Image> mipChain(const Image& level);

/// One channel: the Rec. 709 luma of an RGBA level, in weights summing to 256.
[[nodiscard]] Image heightOf(const Image& rgba);

/// Two channels, X then Y, from a one-channel height level: the Sobel
/// gradient with wrap edges, scaled so one surface keeps one slope at every
/// upscale `factor` and mip `level`. X is +right; Y is +toward row 0.
[[nodiscard]] Image normalOf(const Image& height, std::uint32_t factor, std::uint32_t level);

/// One channel: baseRoughness + (128 - height) / 4, clamped to [0, 255].
/// A heuristic -- darker texels rougher -- that the curated library and the
/// recipe override.
[[nodiscard]] Image roughnessOf(const Image& height, std::uint8_t baseRoughness);

/// RGBA: the base colour where the height is at or above `threshold`, black
/// elsewhere, alpha 255.
[[nodiscard]] Image emissiveOf(const Image& rgba, const Image& height, std::uint8_t threshold);

} // namespace uta::umat
