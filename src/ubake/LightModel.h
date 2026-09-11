// The light model the bake and the renderer share --
// docs/specs/UTA-0112-baked-light-probes.md SS 4.3.
//
// THIS IS THE REFERENCE. ubake stays out of the runtime targets (docs/design.md
// rule 2), so UTA-0014 writes these formulas again in its own code, and a test
// that links both holds its copy to this one (SS 4.9).
//
// THE MODEL IS THIS PROJECT'S OWN. UT99's renderer is in no source this
// project draws on (SS 2 item 4), so the colour wheel, the falloff and the
// cone are SS 4.3's choices. A change to any of them re-bakes every map, which
// is what BAKER_REVISION records.
//
// NO PLATFORM MATHS LIBRARY IN THE SINE. sineOf reduces its angle with integer
// arithmetic and evaluates fixed polynomials by Horner's rule, using only
// addition and multiplication, so every compiler computes the same bits
// (docs/design.md SS What every part does the same way). The three angle
// functions are constexpr and defined here, and tests/unit/BakeLightModelTest.cpp
// evaluates them in a static_assert: Clang will not evaluate a library std::sin
// in a constant expression, so that is what refuses one (INV-4).

#pragma once

#include "ubake/CollisionQuery.h"
#include "ubundle/Bundle.h"

#include <array>
#include <cstdint>
#include <numbers>

namespace uta::ubake {

/// Linear light, or a linear reflectance, one double a channel.
struct Rgb {
    double r = 0, g = 0, b = 0;
};

/// SS 4.3's colour: the hue wheel's pure colour, moved toward white by
/// `saturation`, so saturation 255 is white at every hue.
[[nodiscard]] Rgb lightColour(std::uint8_t hue, std::uint8_t saturation) noexcept;

/// `AActor::WorldLightRadius`: 25 * (radius + 1).
[[nodiscard]] double lightRadius(std::uint8_t radius) noexcept;

/// (1 - (distance / radius)^2)^2 below the radius; 0 at it and beyond.
[[nodiscard]] double falloff(double distance, double radius) noexcept;

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

/// A light's pointing direction from its pitch, yaw and roll: UTA-0119 SS 4.5's
/// Y * P * R applied to +X, which `FRotator::Vector` is. Roll does not move it.
[[nodiscard]] constexpr Vec3 directionOf(const std::array<std::int32_t, 3>& rotation) noexcept {
    const double cosPitch = cosineOf(rotation[0]);
    return {cosPitch * cosineOf(rotation[1]), cosPitch * sineOf(rotation[1]), sineOf(rotation[0])};
}

/// The light `light` puts on a surface at `x` with unit normal `n`, with no
/// shadow test: colour, times brightness / 255, times the falloff, times the
/// incidence, times the spot factor -- SS 4.3.
[[nodiscard]] Rgb lightAt(const ubundle::Light& light, const Vec3& x, const Vec3& n) noexcept;

/// An 8-bit sRGB value, decoded to linear by a table of literals.
[[nodiscard]] double linearOf(std::uint8_t srgb) noexcept;

} // namespace uta::ubake
