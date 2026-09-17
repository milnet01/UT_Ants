// A still picture of a FireTexture -- UTA-0176.
//
// A FireTexture stores no pixels. UT99 draws each frame at run time from its
// sparks: each spark heats pixels of a heat field, directly or by releasing
// particles that drift and cool, and the field is then blurred and cooled,
// shifted up a row when the texture rises. The palette turns heat into colour.
// This runs that for FIRE_STILL_FRAMES frames from a cold field and returns the
// last one's heat, which is the texture's palette indices. Moving it is
// UTA-0105's; this is the still the bake gives it until then.
//
// ADAPTED FROM SurrealEngine's UFireTexture::UpdateFrame
// (SurrealEngine/Packages/Engine/Resources/Textures/UFireTexture.cpp,
// https://github.com/dpjudas/SurrealEngine), Copyright (c) 2021-2026 Magnus
// Norddahl, Lupert Everett and contributors, under the zlib licence, whose
// notice is at third_party/surrealengine/LICENSE. Altered from the original:
// - its random bytes come from the C library's rand(); these from a fixed
//   xorshift sequence, so one texture bakes to one picture on any machine
//   (docs/design.md, the numeric contract);
// - Wheel and SphereLightning, which turn angles with sine and cosine -- a
//   platform maths call the contract keeps out of the baker -- heat as the
//   types it does not model do, a scattered point per frame;
// - it is a function of its inputs rather than a class holding frame state.

#pragma once

#include "upkg/Texture.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace uta::ubake {

/// How many frames the still is run for. A torch's particles live at most 255
/// frames, so this is past the longest-lived one leaving its first spark.
inline constexpr int FIRE_STILL_FRAMES = 256;

/// The FireTexture properties the still reads. Each is 0 or false when the
/// export does not carry it, which is the class's own default in Fire.u.
struct FireSettings {
    std::uint8_t renderHeat = 0; ///< how slowly heat fades; 255 fades least
    bool rising = false;         ///< the field moves up a row a frame
    std::int32_t sparksLimit = 0; ///< sparks and live particles together
};

/// The heat of every pixel after FIRE_STILL_FRAMES frames, row by row, top
/// first: `width` times `height` palette indices. Empty when either is 0.
[[nodiscard]] std::vector<std::byte> fireStill(std::uint32_t width, std::uint32_t height,
                                               std::span<const upkg::Spark> sparks,
                                               const FireSettings& settings);

} // namespace uta::ubake
