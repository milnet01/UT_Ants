// The sine and cosine of UT99's angle units, 65536 to a turn, the same bits on
// every compiler. Moved here from ubake/LightModel.h (UTA-0112) so the
// simulation can turn by them too (UTA-0017 SS 4.6).
//
// NO PLATFORM MATHS LIBRARY. sineOf reduces its angle with integer arithmetic
// and evaluates fixed polynomials by Horner's rule, using only addition and
// multiplication, so every compiler computes the same bits (docs/design.md
// SS Determinism). They are constexpr, and tests/unit/BakeLightModelTest.cpp
// evaluates them in a static_assert: Clang will not evaluate a library std::sin
// in a constant expression, so that is what refuses one.

#pragma once

#include <cstdint>
#include <numbers>

namespace uta::core {

namespace detail {

/// One UT angle unit in radians. Dividing by a power of two is exact, so this
/// is pi correctly rounded, scaled.
inline constexpr double RADIANS_PER_UNIT = std::numbers::pi / 32768.0;

/// sin x for x in [0, pi/4]: its Taylor series to x^13, whose first omitted
/// term is below 3e-14 there.
[[nodiscard]] constexpr double sinPolynomial(double x) noexcept {
    const double x2 = x * x;
    return x * (1.0
                + x2 * (-1.0 / 6.0
                        + x2 * (1.0 / 120.0
                                + x2 * (-1.0 / 5040.0
                                        + x2 * (1.0 / 362880.0
                                                + x2 * (-1.0 / 39916800.0
                                                        + x2 * (1.0 / 6227020800.0)))))));
}

/// cos x for x in [0, pi/4]: its Taylor series to x^14, whose first omitted
/// term is below 2e-15 there.
[[nodiscard]] constexpr double cosPolynomial(double x) noexcept {
    const double x2 = x * x;
    return 1.0
           + x2 * (-1.0 / 2.0
                   + x2 * (1.0 / 24.0
                           + x2 * (-1.0 / 720.0
                                   + x2 * (1.0 / 40320.0
                                           + x2 * (-1.0 / 3628800.0
                                                   + x2 * (1.0 / 479001600.0
                                                           + x2 * (-1.0 / 87178291200.0)))))));
}

/// The sine of `units` UT angle units, 65536 to a turn. The quadrant is the
/// top two bits of the low sixteen; within it, the first half is a sine and
/// the second the cosine of what remains, so each polynomial sees [0, pi/4].
[[nodiscard]] constexpr double sineOfUnits(std::uint32_t units) noexcept {
    const std::uint32_t turn = units & 0xFFFFu;
    const std::uint32_t quadrant = turn >> 14;
    const std::uint32_t within = turn & 0x3FFFu;
    const bool firstHalf = within <= 8192u;
    const double near = static_cast<double>(firstHalf ? within : 16384u - within) * RADIANS_PER_UNIT;
    const double sine = firstHalf ? sinPolynomial(near) : cosPolynomial(near);
    const double cosine = firstHalf ? cosPolynomial(near) : sinPolynomial(near);
    switch (quadrant) {
    case 0: return sine;
    case 1: return cosine;
    case 2: return -sine;
    default: return -cosine;
    }
}

} // namespace detail

/// The sine of `angle` UT units, 65536 to a turn; any int32 is taken modulo a
/// turn.
[[nodiscard]] constexpr double sineOf(std::int32_t angle) noexcept {
    return detail::sineOfUnits(static_cast<std::uint32_t>(angle));
}

/// The cosine of `angle` UT units: the sine a quarter turn on, computed in
/// unsigned arithmetic so no angle overflows.
[[nodiscard]] constexpr double cosineOf(std::int32_t angle) noexcept {
    return detail::sineOfUnits(static_cast<std::uint32_t>(angle) + 16384u);
}

/// The angle of (x, y), in whole UT units from -32767 to 32768, rounded to
/// the nearest unit: UT99's Rotator() of a vector (UTA-0465). Found by
/// halving within the quadrant on the sign of x sin a - y cos a, which rises
/// through the answer there, so no platform maths runs. (0, 0) is 0.
[[nodiscard]] constexpr std::int32_t angleOf(double x, double y) noexcept {
    if (x == 0 && y == 0) return 0;
    const auto past = [x, y](std::int32_t a) { return x * sineOf(a) - y * cosineOf(a); };
    // The quadrant whose closed range holds the answer.
    std::int32_t low = y >= 0 ? (x >= 0 ? 0 : 16384) : (x >= 0 ? -16384 : -32768);
    std::int32_t high = low + 16384;
    while (high - low > 1) {
        const std::int32_t middle = low + (high - low) / 2;
        (past(middle) <= 0 ? low : high) = middle;
    }
    const double below = -past(low), above = past(high);
    const std::int32_t nearest = below <= above ? low : high;
    return nearest == -32768 ? 32768 : nearest;
}

} // namespace uta::core
