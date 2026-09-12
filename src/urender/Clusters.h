// The cluster grid -- docs/specs/UTA-0014-vulkan-draw-path.md SS 4.6.
//
// The view frustum is 16 tiles across, 8 down, and 24 slices deep, the slices
// exponential in depth so a near cluster is as thin as a far one is wide.
// This file builds each cluster's view-space box, which the culling pass tests
// lights against, and says which cluster a fragment is in, which the shading
// pass computes the same way (shaders/scene.frag's clusterOf).
//
// INTERNAL, device-free: SS 4.12 grades cluster assignment without a device.

#pragma once

#include "urender/Renderer.h"
#include "urender/ShaderTypes.h"

#include <cstdint>
#include <vector>

namespace uta::urender {

struct ClusterGrid {
    std::vector<gpu::ClusterBounds> bounds; ///< CLUSTER_COUNT boxes, x + y * 16 + z * 16 * 8
    float depthScale = 0;                   ///< slice = floor(log(z) * depthScale - depthBias)
    float depthBias = 0;
};

[[nodiscard]] ClusterGrid clusterGrid(const Camera& camera, std::uint32_t width, std::uint32_t height);

/// The cluster a fragment at pixel (x, y), `viewZ` along the view, is in. A
/// depth outside [near, far] is clamped to the nearest slice.
[[nodiscard]] std::uint32_t clusterOf(const ClusterGrid& grid, float pixelX, float pixelY, float viewZ,
                                      std::uint32_t width, std::uint32_t height, float nearPlane) noexcept;

} // namespace uta::urender
