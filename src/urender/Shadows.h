// Shadow maps: where each light's tiles go in the atlas, when they need
// drawing, and the projection each is drawn and sampled with --
// docs/specs/UTA-0014-vulkan-draw-path.md SS 4.8 and SS 6.
//
// ONE DEPTH ATLAS FOR EVERY SHADOWING LIGHT. A point light takes six tiles,
// one per cube face; a spotlight takes one. Tile size follows the light's
// reach alone, in powers of two (UTA-0166), at the finest texel at which
// every light of the map fits the atlas (UTA-0303).
//
// A LIGHT THAT DOES NOT MOVE HAS ITS TILES DRAWN ONCE AND KEPT (SS 4.8). A
// tile is drawn when it is placed, when its light's numbers change, and when a
// mover that moved is inside its light's radius -- and not otherwise, so a
// still camera over a still level draws no shadow tile at all.
//
// WHEN THE ATLAS CANNOT HOLD THE FRAME'S LIGHTS (SS 6), lights are admitted in
// descending tile size until it is full; the rest are lit unshadowed and
// counted.
//
// INTERNAL, device-free: the allocation, the admission and the matrices are
// graded without a device.

#pragma once

#include "ubundle/Bundle.h"
#include "urender/Renderer.h"
#include "urender/ShaderTypes.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace uta::urender {

inline constexpr std::uint32_t SHADOW_ATLAS_SIZE = 4096;
inline constexpr std::uint32_t LARGEST_SHADOW_TILE = 1024;
inline constexpr std::uint32_t SMALLEST_SHADOW_TILE = 64;

/// World units one texel of a light's tile covers across a cube face --
/// UTA-0166. A face spans twice the light's reach, so the tile is
/// `2 * reach / SHADOW_UNITS_PER_TEXEL`. UTA-0303: this is the coarsest a
/// tier's shadows get; a map whose lights fit is shadowed finer.
inline constexpr double SHADOW_UNITS_PER_TEXEL = 64.0;

/// UTA-0303: how many times finer than its tier a map's shadows may be. The
/// planner halves the texel up to this many times over while every light of
/// the map still fits the atlas. Four times finer than Medium's texel is 8
/// world units. Past it few maps fit, and each halving makes a mover redraw
/// four times the texels per tile it touches.
inline constexpr std::uint32_t FINEST_SHADOW_REFINEMENT = 4;

/// UTA-0175: how fine a tier's shadows are -- the atlas's side and the world
/// units under one texel at the coarsest (UTA-0303). SHADOW_ATLAS_SIZE and
/// SHADOW_UNITS_PER_TEXEL are Low's.
struct ShadowDetail {
    std::uint32_t atlasSize = SHADOW_ATLAS_SIZE;
    double unitsPerTexel = SHADOW_UNITS_PER_TEXEL;
};

/// Medium and up: twice the atlas's side and half the texel, so a shadow edge
/// is twice as fine and a map's share of the atlas is never larger than at Low:
/// a tile at most doubles its side in an atlas four times the area, and one
/// held at LARGEST_SHADOW_TILE or SMALLEST_SHADOW_TILE takes less. At 64
/// units a thin occluder, such as the ledge over AS-Frigate's cabin doors,
/// fell between texels and lit the wall under it. Measured against a reference
/// eight times finer over 135 views of the three maps: the mean error fell
/// from 1.42 to 0.93 on AS-Frigate, 1.54 to 1.01 on DM-Deck16][ and 0.55 to
/// 0.40 on DM-Fetid. Less depth bias and a wider filter each measured worse.
inline constexpr ShadowDetail FINE_SHADOW_DETAIL{8192, 32.0};

/// A square of the atlas, in texels.
struct AtlasTile {
    std::uint32_t x = 0, y = 0, size = 0;
    bool operator==(const AtlasTile&) const = default;
};

/// Squares of the atlas, handed out by halving: a tile of any power-of-two
/// size from SMALLEST_SHADOW_TILE to LARGEST_SHADOW_TILE, never two overlapping.
class ShadowAtlas {
public:
    /// `atlasSize`, a multiple of LARGEST_SHADOW_TILE, is the side in texels.
    explicit ShadowAtlas(std::uint32_t atlasSize = SHADOW_ATLAS_SIZE) : size_(atlasSize) { clear(); }

    /// A free tile of `size`, or none when the atlas cannot hold one.
    [[nodiscard]] std::optional<AtlasTile> allocate(std::uint32_t size);

    /// `tile`, from `allocate`, free again. Not merged with its neighbours.
    void release(const AtlasTile& tile);

    /// Every tile free again.
    void clear();

private:
    /// Free tiles by level: level 0 is LARGEST_SHADOW_TILE, each level half the last.
    std::uint32_t size_;
    std::vector<std::vector<AtlasTile>> free_;
};

/// Whether `light` is shadowed as a spotlight -- one tile -- rather than as a
/// point light's six. A spotlight whose cone is wider than one tile's frustum
/// opens is shadowed as a point light.
[[nodiscard]] bool isSpot(const ubundle::Light& light) noexcept;

/// How many tiles `light` takes: 6, 1, or 0 for a light that lights no
/// surface -- one ubundle::litDirectly refuses, such as brightness 0 or a
/// spotlight whose cone is 0.
[[nodiscard]] std::uint32_t shadowFacesOf(const ubundle::Light& light) noexcept;

/// The tile size `light` wants: its sphere of influence at
/// SHADOW_UNITS_PER_TEXEL, rounded up to a power of two, within the tile
/// limits. 0 only when the light shadows nothing.
///
/// UTA-0166: this reads the light and nothing else. Sized from the light's
/// screen size it changed whenever the camera did, which re-admitted every
/// light and left a different few holding tiles each frame -- and a light with
/// no tile scatters no fog at all, so shafts and haze switched on and off as
/// the camera turned.
[[nodiscard]] std::uint32_t shadowTileSize(const ubundle::Light& light,
                                           double unitsPerTexel = SHADOW_UNITS_PER_TEXEL) noexcept;

/// The view-projection face `face` of `light` is drawn and sampled with. A point
/// light's faces look along +X, -X, +Y, -Y, +Z, -Z; a spotlight's one face looks
/// along its direction. Depth runs 0 near to 1 at the light's radius.
[[nodiscard]] gpu::Mat4 shadowViewProj(const ubundle::Light& light, std::uint32_t face) noexcept;

/// One tile a frame must draw before it shades.
struct ShadowDraw {
    std::uint32_t face = 0; ///< into Plan::faces
    AtlasTile tile;
};

/// Where every drawn light's shadow is this frame.
struct ShadowPlan {
    std::vector<gpu::ShadowFace> faces;
    std::vector<std::int32_t> firstFace;   ///< per light: into faces, or -1 when unshadowed
    std::vector<std::uint32_t> faceCount;  ///< per light
    std::vector<ShadowDraw> draws;         ///< the tiles to draw this frame
    std::uint32_t unshadowed = 0;          ///< lights wanting shadows the atlas could not hold
    double unitsPerTexel = 0;              ///< UTA-0303: the texel every tile was sized at
};

/// Keeps each light's tiles between frames, which is what makes a static
/// light's tiles drawn once.
class ShadowPlanner {
public:
    /// UTA-0175: the tier's atlas size and texel, which every plan keeps to.
    explicit ShadowPlanner(ShadowDetail detail = {}) : detail_(detail), atlas_(detail.atlasSize) {}

    /// Plan a frame for `lights` -- the LITE lights the direct term draws, in
    /// the order the shader reads them. `movedMoverBounds` holds, for each mover
    /// whose transform changed since the last frame, its world box before and
    /// after as (min, max); a light whose radius reaches one redraws its tiles.
    ///
    /// UTA-0166: the camera is not an argument. The plan depends on the lights
    /// alone, so a static level is placed and drawn once however the camera
    /// moves.
    ///
    /// UTA-0303: every tile is sized at the finest of the tier's texel and its
    /// halvings down to FINEST_SHADOW_REFINEMENT times finer at which all the
    /// lights' tiles fit the atlas, and at the tier's texel when none does.
    /// Tiles are powers of two placed largest first, so they fit exactly when
    /// their areas sum to no more than the atlas's. That too reads the lights
    /// alone.
    [[nodiscard]] ShadowPlan plan(const std::vector<ubundle::Light>& lights,
                                  const std::vector<std::array<std::array<float, 3>, 2>>& movedMoverBounds);

    /// Forget every tile, so the next plan places and draws them all -- for a
    /// new bundle, whose geometry every tile depicted.
    void reset();

    [[nodiscard]] const ShadowDetail& detail() const noexcept { return detail_; }

private:
    struct Held {
        std::uint32_t size = 0;
        std::vector<AtlasTile> tiles;
        ubundle::Light light; ///< the numbers the tiles were drawn from
    };
    ShadowDetail detail_;
    ShadowAtlas atlas_;
    std::vector<Held> held_;
};

} // namespace uta::urender
