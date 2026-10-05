// A still picture of a FireTexture -- UTA-0176.
//
// A FireTexture stores no pixels; ufire/Fire.h runs UT99's animation, and
// this runs it for FIRE_STILL_FRAMES steps from a cold field under
// Turning::Scatter, returning the last step's heat as the texture's palette
// indices. Moving it is UTA-0286's, in the renderer; this is the still a
// material keeps.

#pragma once

#include "ufire/Fire.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"
#include "upkg/Texture.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace uta::ubake {

/// How many frames the still is run for: the primed picture.
inline constexpr int FIRE_STILL_FRAMES = ufire::PRIME_STEPS;

/// The FireTexture properties the still reads. Each is 0 or false when the
/// export does not carry it, which is the class's own default in Fire.u.
struct FireSettings {
    std::uint8_t renderHeat = 0; ///< how slowly heat fades; 255 fades least
    bool rising = false;         ///< the field moves up a row a frame
    std::int32_t sparksLimit = 0; ///< sparks and live particles together
    float maxFrameRate = 0;       ///< UTA-0286: the steps a second it asks for; 0 for once a frame
};

/// UTA-0263: the settings a FireTexture's property list gives, each at the
/// class default where the export does not store it. The bake and INV-3's
/// real-asset test both read a FireTexture through this, so both judge the
/// same still.
[[nodiscard]] FireSettings fireSettingsOf(const upkg::Package& holder,
                                          std::span<const upkg::Property> properties);

/// The heat of every pixel after FIRE_STILL_FRAMES frames, row by row, top
/// first: `width` times `height` palette indices. Empty when either is 0.
[[nodiscard]] std::vector<std::byte> fireStill(std::uint32_t width, std::uint32_t height,
                                               std::span<const upkg::Spark> sparks,
                                               const FireSettings& settings);

} // namespace uta::ubake
