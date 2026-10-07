// A bundle's geometry on the GPU -- docs/specs/UTA-0014-vulkan-draw-path.md
// SS 4.5.
//
// INTERNAL (includes Vulkan).
//
// One vertex buffer and one index buffer for the level and every mover
// together. Vertices are UT99's own coordinates and units, uploaded as
// ubundle::GeometryVertex lays them out; nothing is converted.

#pragma once

#include "core/Error.h"
#include "ubundle/Bundle.h"
#include "urender/Device.h"
#include "urender/Materials.h"
#include "urender/Resources.h"

#include <array>
#include <cstdint>
#include <vector>

namespace uta::urender {

/// Vertex binding 1: what a vertex carries beside its GeometryVertex --
/// UTA-0164 SS 4.5's occlusion uv and UTA-0326 SS 4.5's mask chart and texel.
struct SurfaceVertex {
    std::array<float, 2> occlusionUv{};
    std::uint32_t maskChart = ubundle::MASK_NO_CHART;
    std::array<float, 2> maskTexel{};
};

/// One batch, ready to draw.
struct DrawItem {
    std::uint32_t objectIndex = 0; ///< 0 is the level; mover i is i + 1
    std::uint32_t materialIndex = DEFAULT_MATERIAL;
    std::uint32_t polyFlags = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::int32_t firstVertex = 0;
    std::array<float, 2> panRate{}; ///< UTA-0269: repeats a second along u and v
};

struct SceneGeometry {
    Buffer vertices;
    /// One SurfaceVertex per vertex, in the same order. UTA-0164 SS 4.5: the
    /// level's occlusion uvs from AOCC, and the white block's centre for every
    /// mover. UTA-0326 SS 4.5: the level's charts and texels from SMSK, and
    /// MASK_NO_CHART for every mover and every vertex of a bundle without one.
    Buffer surfaceVertices;
    Buffer indices;
    /// Every batch that draws, in bundle order. PF_Portal and PF_Invisible
    /// batches are not here (SS 4.5).
    std::vector<DrawItem> draws;
    std::uint32_t objectCount = 1;

    /// InvalidArgument for a batch or an index that leaves its geometry --
    /// ubundle::read refuses both, but a Bundle built in memory has not been
    /// through it, and a stray index on a GPU is a crash rather than an error.
    [[nodiscard]] static Result<SceneGeometry> upload(Gpu& gpu, const ubundle::Bundle& bundle,
                                                      MaterialSet& materials);
};

} // namespace uta::urender
