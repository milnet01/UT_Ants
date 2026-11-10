// The light model the bake and the renderer share --
// docs/specs/UTA-0112-baked-light-probes.md SS 4.3.
//
// THIS IS THE REFERENCE. ubake stays out of the runtime targets (docs/design.md
// rule 2), so UTA-0014 writes these formulas again in its own code, and a test
// that links both holds its copy to this one (SS 4.9).
//
// WHERE THE MODEL COMES FROM. The colour and intensity are UT99's own FGetHSV,
// read from its 469 Engine.so (UTA-0165); the level's brightness is its
// Render.so's (UTA-0156 SS 4.5), and so is the falloff (UTA-0187); the two
// reshaping effects are SurrealEngine's (UTA-0156); the cone is SS 4.3's
// choice. A change to any of them re-bakes every map, which is what
// BAKER_REVISION records.
//
// NO PLATFORM MATHS LIBRARY IN THE SINE. sineOf and cosineOf are core/Angles.h's,
// named here so the bake's code reads them as before; that file says how they
// compute the same bits on every compiler, and tests/unit/BakeLightModelTest.cpp
// holds them to it (INV-4).

#pragma once

#include "core/Angles.h"
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

/// SS 4.3's colour, FGetHSV's hue: three sectors whose channels sum to 1 --
/// the last to just over 1, its blue dividing by 84 -- moved toward white by `saturation`, so saturation 255 is white at every hue.
[[nodiscard]] Rgb lightColour(std::uint8_t hue, std::uint8_t saturation) noexcept;

/// SS 4.3's intensity: FGetHSV's brightness curve over its value at 255, so
/// 255 is 1 and a dim light sits near sqrt(brightness / 255).
[[nodiscard]] double lightIntensity(std::uint8_t brightness) noexcept;

/// `AActor::WorldLightRadius`: 25 * (radius + 1).
[[nodiscard]] double lightRadius(std::uint8_t radius) noexcept;

/// UT99's own falloff (UTA-0187): with v = distance / radius, 1 + 2v^3 - 3v^2
/// below the radius; 1 at the light; 0 at the radius and beyond. Render.so's
/// spatial_None is incidence times this, with no /v and no cap.
///
/// TAKEN WITH THE DISPLAY-VALUE COMBINE, NOT ALONE. UTA-0156 SS 4.5 measured
/// it worse than SurrealEngine's min(1, (1 + 2v^3 - 3v^2) / v) while light met
/// texture linearly: UT99 adds this shape in its lightmap's units. UTA-0187
/// moved the combine onto display values (scene.frag), where this shape
/// matched the original game better than SurrealEngine's.
[[nodiscard]] double falloff(double distance, double radius) noexcept;

/// UTA-0253: the linear light a surface shows under light `light`, which is
/// `light` raised to light.glsl's DISPLAY_LIGHT_POWER, 1.6. UT99 combines
/// light and texture on display values (UTA-0187), so that power is what turns
/// a light value into light a surface sends on. Zero at and below zero.
///
/// NO PLATFORM MATHS LIBRARY. The fifth root is Newton's iteration from a
/// square-root guess, a fixed count of steps, and the eighth power of it is
/// three squarings; `sqrt` is correctly rounded, so every compiler computes
/// the same bits. Changed with DISPLAY_LIGHT_POWER, and BAKER_REVISION with it.
[[nodiscard]] double shownLight(double light) noexcept;

namespace detail {
using core::detail::cosPolynomial;
using core::detail::RADIANS_PER_UNIT;
using core::detail::sineOfUnits;
using core::detail::sinPolynomial;
} // namespace detail

using core::cosineOf;
using core::sineOf;

/// A light's pointing direction from its pitch, yaw and roll: UTA-0119 SS 4.5's
/// Y * P * R applied to +X, which `FRotator::Vector` is. Roll does not move it.
[[nodiscard]] constexpr Vec3 directionOf(const std::array<std::int32_t, 3>& rotation) noexcept {
    const double cosPitch = cosineOf(rotation[0]);
    return {cosPitch * cosineOf(rotation[1]), cosPitch * sineOf(rotation[1]), sineOf(rotation[0])};
}

/// UTA-0338 SS 4.3: the unit vector toward a SUN_EFFECT light -- against the
/// way its light travels, which its rotation points.
[[nodiscard]] constexpr Vec3 towardSun(const ubundle::Light& light) noexcept {
    // Component by component: Vec3's operator* is not constexpr, and MSVC refuses one that calls it.
    const Vec3 d = directionOf(light.rotation);
    return {-d.x, -d.y, -d.z};
}

/// The point `light` is lit from at `x` -- UTA-0162 SS 4.3: its location, or
/// for a strip leader the nearest point of the segment from stripFrom to stripTo.
[[nodiscard]] Vec3 litFrom(const ubundle::Light& light, const Vec3& x) noexcept;

/// The light `light` puts on a surface at `x` with unit normal `n`, with no
/// shadow test: colour, times lightIntensity, times the level's brightness
/// (UTA-0156 SS 4.5), times the falloff, times the incidence, times the spot
/// factor -- SS 4.3, all measured from litFrom.
/// LE_Cylinder and LE_NonIncidence replace the falloff and drop the other
/// factors (UTA-0156). An absorbed strip light puts nothing (UTA-0162). A
/// SUN_EFFECT light shines from infinitely far: colour, intensity and level
/// brightness times max(0, n . towardSun), with no falloff, spot or radius
/// (UTA-0338 SS 4.3).
[[nodiscard]] Rgb lightAt(const ubundle::Light& light, const Vec3& x, const Vec3& n) noexcept;

/// An 8-bit sRGB value, decoded to linear by a table of literals.
[[nodiscard]] double linearOf(std::uint8_t srgb) noexcept;

} // namespace uta::ubake
