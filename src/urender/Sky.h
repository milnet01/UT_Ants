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
// SCOPE. A still sky, but for UTA-0337's layers below: a panned sheet is
// replayed moving, and any other animated texture in the sky zone is drawn as
// it stood when the faces were drawn. The SkyZoneInfo's own Rotation is not
// applied -- none of the measured maps sets one, and which way it turns the
// sky is unverified. A ZoneInfo subclass that picks its own sky zone
// (MultiSkyZoneInfo and kin) gets the engine's default choice.
//
// Everything here needs no device; Frame.cpp draws the faces.

#pragma once

#include "ubundle/Bundle.h"
#include "ubundle/Sky.h"
#include "urender/Renderer.h"
#include "urender/ShaderTypes.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace uta::urender {

/// A face's side, in texels.
inline constexpr std::uint32_t SKY_FACE_SIZE = 512;
inline constexpr std::uint32_t SKY_COLUMNS = 3;
inline constexpr std::uint32_t SKY_ROWS = 2;

/// Where the sky is drawn from, and which one: ubundle/Sky.h, which the probe
/// bake reads too (UTA-0292).
using ubundle::SkyView;
using ubundle::skyViewOf;

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

/// UTA-0337: one flat sheet of a panned batch in the sky zone -- the moving
/// clouds of most stock skies. The faces are captured without it, and its
/// light once more with it painted white; the shader then slides its texture
/// over that light each frame, as UT99 pans it, so the clouds move although
/// the faces stay still.
struct SkyLayer {
    std::uint32_t firstIndex = 0;    ///< its batch's first index, which names it among the level's draws
    std::array<float, 4> plane{};    ///< unit normal, and the distance along it to the sheet
    std::array<float, 4> u{};        ///< u at point p is dot(u.xyz, p) + u.w
    std::array<float, 4> v{};
    std::uint32_t kind = gpu::SKY_LAYER_TRANSLUCENT;
    std::string material;            ///< its batch's MATS id
    std::array<float, 2> panRate{};  ///< its batch's, UTA-0269
};

struct SkyLayers {
    std::uint8_t zone = 0; ///< the sky zone, whose pan speeds the layers take
    std::vector<SkyLayer> layers;
};

/// The layers of `bundle`'s sky: the sheets of each translucent, masked or
/// opaque batch with a pan rate whose triangles lie wholly in the sky zone,
/// one per plane and texture mapping. A batch whose sheets would pass
/// gpu::SKY_LAYER_CAPACITY is left out whole, so it stays in the faces,
/// still. A modulated batch is left out too. Nothing without a sky view,
/// geometry, rooms or zones, or where the sky view finds no zone.
[[nodiscard]] SkyLayers skyLayersOf(const ubundle::Bundle& bundle);

} // namespace uta::urender
