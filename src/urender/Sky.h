// The level's sky -- UTA-0163.
//
// UT99 draws a PF_FakeBackdrop surface as a window onto the sky zone: a
// separate, sealed area of the map holding a SkyZoneInfo, rendered from that
// actor's location and turned with the player's view but never moved by it.
// urender draws the sky zone once per bundle, as six square faces looking out
// from the SkyZoneInfo along +X, -X, +Y, -Y, +Z and -Z -- the order a point
// light's shadow faces take -- into one texture three faces across and two
// down. A sky surface then shows the face texel its view direction meets.
//
// SCOPE. A still sky: a panning or animated texture in the sky zone is drawn
// as it stood when the faces were drawn. The SkyZoneInfo's own Rotation is not
// applied -- none of the measured maps sets one, and which way it turns the
// sky is unverified. A ZoneInfo subclass that picks its own sky zone
// (MultiSkyZoneInfo and kin) gets the engine's default choice.
//
// Everything here needs no device; Frame.cpp draws the faces.

#pragma once

#include "ubundle/Bundle.h"
#include "urender/Renderer.h"
#include "urender/ShaderTypes.h"

#include <array>
#include <cstdint>
#include <optional>

namespace uta::urender {

/// A face's side, in texels.
inline constexpr std::uint32_t SKY_FACE_SIZE = 512;
inline constexpr std::uint32_t SKY_COLUMNS = 3;
inline constexpr std::uint32_t SKY_ROWS = 2;

/// Where the sky is drawn from.
struct SkyView {
    std::array<float, 3> location{};
};

/// ZoneInfo.LinkToSkybox's choice, read from Engine.u's script: the last
/// SkyZoneInfo (or subclass) in the level's actor order, then the last whose
/// bHighDetail matches the detail mode, which urender always draws at. Nothing
/// when the level has none.
[[nodiscard]] std::optional<SkyView> skyViewOf(const ubundle::Bundle& bundle);

/// Face `face`'s rotation: pitch, yaw, roll.
[[nodiscard]] std::array<std::int32_t, 3> skyFaceRotation(std::uint32_t face) noexcept;

/// The camera face `face` is drawn with into a `width` by `height` target:
/// at the sky's location, its field chosen so the target's central square of
/// side min(width, height) spans exactly 90 degrees.
[[nodiscard]] Camera skyFaceCamera(const SkyView& sky, std::uint32_t face, std::uint32_t width,
                                   std::uint32_t height) noexcept;

/// What a shader samples face `face` with: its view from the origin and a
/// square 90-degree projection, so `viewProj * vec4(direction, 1)` lands in
/// the face's square, and the face's rectangle in the sky texture in UV units.
[[nodiscard]] gpu::ShadowFace skyFaceSample(std::uint32_t face) noexcept;

} // namespace uta::urender
