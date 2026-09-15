// The fog's CPU half: its grid, which volumetric lights glow this frame, and
// the flashlight -- docs/specs/UTA-0015-volumetric-fog.md SS 4.3 to SS 4.5.
//
// NOTHING OF THE LIGHT MODEL IS HERE. A light reaches the shader as UT99's own
// numbers (UTA-0014 SS 3 decision 5), and so does the flashlight; shaders/fog.glsl
// holds how the fog looks.
//
// INTERNAL, device-free.

#pragma once

#include "ubundle/Bundle.h"
#include "urender/Renderer.h"
#include "urender/ShaderTypes.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace uta::urender {

/// SS 4.3's froxel grid, which shaders/fog.glsl states too.
inline constexpr std::array<std::uint32_t, 3> FOG_GRID{160, 90, 64};
inline constexpr float FOG_NEAR = 16.0f; ///< where slice 1 starts
inline constexpr float FOG_FAR = 8192.0f; ///< where the last slice ends
inline constexpr std::uint32_t VOLUME_LIGHT_CAPACITY = 64;

/// SS 4.5: the flashlight, in UT99's numbers. Hue, saturation and brightness
/// are FlashLightBeam's; radius 255 is the farthest a byte reaches; cone 18
/// spans UT99's 200-unit lit sphere at 500 units.
inline constexpr std::uint32_t LE_SPOTLIGHT = 12;
inline constexpr std::uint32_t FLASHLIGHT_HUE = 32;
inline constexpr std::uint32_t FLASHLIGHT_SATURATION = 142;
inline constexpr std::uint32_t FLASHLIGHT_BRIGHTNESS = 250;
inline constexpr std::uint32_t FLASHLIGHT_RADIUS = 255;
inline constexpr std::uint32_t FLASHLIGHT_CONE = 18;

struct VolumeLightChoice {
    std::vector<std::uint32_t> indices; ///< into `lights`
    std::uint32_t dropped = 0;          ///< qualifying lights past the capacity
};

/// SS 4.4: the drawn lights that glow this frame, nearest `eye` first, at most
/// VOLUME_LIGHT_CAPACITY. None unless `zones[cameraZone].fog`; then each light
/// whose volumeRadius is non-zero and whose zone is a fog zone. A zone index
/// not below zones.size(), and a light past lightZones, read as zone 0.
[[nodiscard]] VolumeLightChoice volumeLights(std::span<const gpu::Light> lights,
                                             std::span<const std::uint8_t> lightZones, std::uint8_t cameraZone,
                                             std::span<const ubundle::Zone> zones, const std::array<float, 3>& eye);

/// SS 4.5: a spotlight at the camera, looking where it looks.
[[nodiscard]] gpu::Light flashlightOf(const Camera& camera) noexcept;

} // namespace uta::urender
