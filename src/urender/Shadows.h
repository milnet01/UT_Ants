// Shadow maps: where each light's tiles go in the atlas, when they need
// drawing, and the projection each is drawn and sampled with --
// docs/specs/UTA-0014-vulkan-draw-path.md SS 4.8 and SS 6.
//
// ONE DEPTH ATLAS FOR EVERY SHADOWING LIGHT. A point light takes six tiles,
// one per cube face; a spotlight takes one. Tile size follows the light's
// projected size on screen, in powers of two.
//
// A LIGHT THAT DOES NOT MOVE HAS ITS TILES DRAWN ONCE AND KEPT (SS 4.8). A
// tile is drawn when it is placed, when its light's numbers change, and when a
// mover that moved is inside its light's radius -- and not otherwise, so a
// still camera over a still level draws no shadow tile at all.
//
// WHEN THE ATLAS CANNOT HOLD THE FRAME'S LIGHTS (SS 6), lights are admitted in
// descending projected size until it is full; the rest are lit unshadowed and
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

/// A square of the atlas, in texels.
struct AtlasTile {
    std::uint32_t x = 0, y = 0, size = 0;
    bool operator==(const AtlasTile&) const = default;
};

/// Squares of the atlas, handed out by halving: a tile of any power-of-two
/// size from SMALLEST_SHADOW_TILE to LARGEST_SHADOW_TILE, never two overlapping.
class ShadowAtlas {
public:
    ShadowAtlas() { clear(); }

    /// A free tile of `size`, or none when the atlas cannot hold one.
    [[nodiscard]] std::optional<AtlasTile> allocate(std::uint32_t size);

    /// Every tile free again.
    void clear();

private:
    /// Free tiles by level: level 0 is LARGEST_SHADOW_TILE, each level half the last.
    std::vector<std::vector<AtlasTile>> free_;
};

/// Whether `light` is shadowed as a spotlight -- one tile -- rather than as a
/// point light's six.
[[nodiscard]] bool isSpot(const ubundle::Light& light) noexcept;

/// How many tiles `light` takes: 6, 1, or 0 for a spotlight whose cone is 0,
/// which lights nothing.
[[nodiscard]] std::uint32_t shadowFacesOf(const ubundle::Light& light) noexcept;

/// The tile size `light` wants for `camera` on a `width` by `height` target: the
/// screen size of its sphere of influence rounded up to a power of two, within
/// the tile limits. 0 when the sphere is wholly outside the view.
[[nodiscard]] std::uint32_t shadowTileSize(const Camera& camera, std::uint32_t width, std::uint32_t height,
                                           const ubundle::Light& light) noexcept;

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
};

/// Keeps each light's tiles between frames, which is what makes a static
/// light's tiles drawn once.
class ShadowPlanner {
public:
    /// Plan a frame for `lights` -- the LITE lights the direct term draws, in
    /// the order the shader reads them. `movedMoverBounds` holds, for each mover
    /// whose transform changed since the last frame, its world box before and
    /// after as (min, max); a light whose radius reaches one redraws its tiles.
    [[nodiscard]] ShadowPlan plan(const std::vector<ubundle::Light>& lights, const Camera& camera,
                                  std::uint32_t width, std::uint32_t height,
                                  const std::vector<std::array<std::array<float, 3>, 2>>& movedMoverBounds);

    /// Forget every tile, so the next plan places and draws them all -- for a
    /// new bundle, whose geometry every tile depicted.
    void reset();

private:
    struct Held {
        std::uint32_t size = 0;
        std::vector<AtlasTile> tiles;
        ubundle::Light light; ///< the numbers the tiles were drawn from
    };
    ShadowAtlas atlas_;
    std::vector<Held> held_;
};

} // namespace uta::urender
