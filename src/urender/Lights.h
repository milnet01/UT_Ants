// Which lights the direct term draws, and how each varies over time --
// docs/specs/UTA-0014-vulkan-draw-path.md SS 4.6 and SS 4.9.
//
// NOTHING OF UTA-0112 SS 4.3's MODEL IS HERE. A light reaches the shader as
// UT99's own numbers and shaders/light.glsl turns them into light (SS 3
// decision 5). What is here is the choice of lights and the time-varying
// scalar, which SS 4.9 says multiplies the direct term only.
//
// INTERNAL, device-free.

#pragma once

#include "ubundle/Bundle.h"
#include "urender/ShaderTypes.h"

#include <cstdint>
#include <vector>

namespace uta::urender {

/// UT99's ELightType values this file reads.
inline constexpr std::uint8_t LT_STEADY = 1;
inline constexpr std::uint8_t LT_PULSE = 2;
inline constexpr std::uint8_t LT_BLINK = 3;
inline constexpr std::uint8_t LT_FLICKER = 4;
inline constexpr std::uint8_t LT_STROBE = 5;
inline constexpr std::uint8_t LT_BACKDROP_LIGHT = 6;
inline constexpr std::uint8_t LT_SUBTLE_PULSE = 7;

/// SS 4.9's scalar for `light` at `seconds`: 1 for a steady light, and for
/// every type this file does not vary -- a byte past the last ELightType
/// included, which UTA-0110 SS 4.4 leaves to the renderer. Its shape per type
/// is deliberately unpinned by the spec; nothing binds to it.
[[nodiscard]] float flickerOf(const ubundle::Light& light, double seconds) noexcept;

/// The lights the direct term draws, as the shader reads them, in LITE order.
///
/// Every LITE light but two kinds. LT_BackdropLight lights only the sky, which
/// SS 4.5 draws unlit. A specialLit light lights only PF_SpecialLit surfaces,
/// which SS 4.5 does not implement.
[[nodiscard]] std::vector<gpu::Light> drawnLights(const ubundle::Bundle& bundle, double seconds);

} // namespace uta::urender
