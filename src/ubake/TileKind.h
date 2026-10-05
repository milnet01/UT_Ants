// Whether a material's repeats may move -- docs/specs/UTA-0277-per-tile-
// variation.md SS 4.2 and SS 4.5.
//
// A FILE OF ITS OWN because it changes for its own reason: the judgement is
// tuned on pictures, and neither the material making nor the geometry in
// Bake.cpp moves when it is.
//
// SAME ON EVERY COMPILER (INV-1). The scores are sums, products and quotients
// of doubles in a fixed order, with no library function whose rounding a
// compiler chooses; so no FFT, whose sines would be one.

#pragma once

#include "ubake/Geometry.h"
#include "ubundle/Bundle.h"
#include "umat/Material.h"
#include "upkg/Geometry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace uta::ubake {

/// SS 4.2 step 4's four limits. Arguments rather than constants inside the
/// judgement so a test can force each outcome on a fixed picture. Fitted on
/// the reference install's 400 most-used textures (SS 4.2's record).
struct TileLimits {
    double linesShuffle = 5.0;
    double linesFixed = 12.0;
    double spotsShuffle = 0.5;
    double spotsFixed = 0.75;
};

/// SS 4.2 steps 3 and 4's measurements of one picture.
struct TileScores {
    bool flat = false; ///< step 3: luma spread under 2 at 64 x 64
    double lines = 0;  ///< whole-width bands along rows, columns or diagonals
    double spots = 0;  ///< the highest autocorrelation outside a radius of 4 texels at 128
};

/// SS 4.2 steps 3 and 4 over an RGBA base level, as umat::resolve gives it.
/// A picture with no texels, or not four channels, is flat.
[[nodiscard]] TileScores tileScores(const umat::Image& rgba);

/// SS 4.2 steps 3 and 4. Exclusions (step 1) and answers (step 2) are the
/// caller's: the first decides whether this is asked at all, and the second
/// is applied where a bundle is loaded (ubundle::applyTileAnswers).
[[nodiscard]] ubundle::TileKind judgeTile(const TileScores& scores, const TileLimits& limits = {});

/// SS 4.5's content hash: SHA-256 over the width and the height as
/// little-endian u32, then the RGBA bytes.
[[nodiscard]] std::array<std::byte, 32> pictureHash(const umat::Image& rgba);

/// What the level's surfaces wearing one material say about it -- SS 4.2
/// step 1's span and SS 4.5's view.
struct TileSurfaces {
    /// Some surface's texture coordinates cover two repeats or more in both u
    /// and v.
    bool spansTwo = false;
    std::uint32_t surfaces = 0;
    double largestArea = 0;
    /// The largest surface's largest polygon: its centre, which lies on it, the
    /// surface's unit normal, and the largest side of the polygon's box, in
    /// world units.
    std::array<double, 3> at{};
    std::array<double, 3> normal{};
    double extent = 0;
};

/// Per material id, over the level Model's drawn surfaces; a surface is every
/// node sharing its `iSurf`. Runs after buildGeometry, which has refused a
/// node whose index leaves its table, so such a node is skipped here.
[[nodiscard]] std::map<std::string, TileSurfaces, std::less<>> tileSurfaces(const upkg::Model& model,
                                                                            const MaterialLookup& materials);

} // namespace uta::ubake
