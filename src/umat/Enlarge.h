// umat: enlarge an RGBA image by an integer factor with a fixed Lanczos
// table -- docs/specs/UTA-0009-material-from-texture.md SS 4.3.
//
// NO FLOATING POINT. The weights are literals generated offline from the
// kernel, so the baker calls no `sin`, and every pass is integer arithmetic:
// the same bytes on every compiler (INV-6) and every worker count (INV-7).
//
// EDGES WRAP. World textures tile, and a clamped edge seams every repeat
// (INV-4).

#pragma once

#include "core/Error.h"
#include "core/Jobs.h"
#include "umat/Material.h"

#include <array>
#include <cstdint>

namespace uta::umat {

namespace detail {

inline constexpr int LANCZOS_A = 3;
inline constexpr int WEIGHT_BITS = 14;
inline constexpr std::int32_t WEIGHT_ONE = 1 << WEIGHT_BITS;
inline constexpr int TAPS = 2 * LANCZOS_A;

using Phase = std::array<std::int32_t, TAPS>;

// Output texel j at factor k samples source position (j + 1/2) / k - 1/2. Its
// phase is j mod k, and tap t reads source texel floor(that) - (A - 1) + t.
//
// Each phase holds the kernel L(x) = sinc(x) * sinc(x / A) at its taps,
// divided by their sum -- the raw kernel does not sum to 1 at a fractional
// offset -- scaled by WEIGHT_ONE and rounded, the largest tap absorbing the
// remainder. So every phase sums to exactly WEIGHT_ONE (INV-3), and
// tests/unit/MaterialGenerateTest.cpp recomputes each weight with std::sin.
inline constexpr std::array<Phase, 2> kWeights2{{
    {121, -1114, 4440, 14628, -2184, 493},
    {493, -2184, 14628, 4440, -1114, 121},
}};
inline constexpr std::array<Phase, 4> kWeights4{{
    {257, -1736, 7206, 12583, -2425, 499},
    {30, -501, 1977, 15936, -1393, 335},
    {335, -1393, 15936, 1977, -501, 30},
    {499, -2425, 12583, 7206, -1736, 257},
}};

} // namespace detail

/// `rgba` enlarged `factor` times in each axis. Factor 1 copies the input and
/// filters nothing. Separable: a horizontal pass into an 8-bit intermediate,
/// then a vertical pass, each clamped to [0, 255]. All four channels go
/// through the same filter, alpha included.
///
/// InvalidArgument for an image that is not well-formed RGBA, or a factor
/// other than 1, 2 or 4.
[[nodiscard]] Result<Image> enlarge(const Image& rgba, std::uint32_t factor, JobSystem& jobs);

} // namespace uta::umat
