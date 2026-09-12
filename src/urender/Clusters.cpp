// The cluster grid -- Clusters.h.

#include "urender/Clusters.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace uta::urender {

ClusterGrid clusterGrid(const Camera& camera, std::uint32_t width, std::uint32_t height) {
    const auto [tilesX, tilesY, slices] = gpu::CLUSTER_GRID;
    const double nearPlane = camera.nearPlane, farPlane = camera.farPlane;
    const double f = 1.0 / std::tan(camera.verticalFovDegrees * std::numbers::pi / 360.0);
    const double aspect = static_cast<double>(width) / static_cast<double>(height);
    const double logRatio = std::log(farPlane / nearPlane);

    ClusterGrid grid;
    grid.depthScale = static_cast<float>(slices / logRatio);
    grid.depthBias = static_cast<float>(slices * std::log(nearPlane) / logRatio);
    grid.bounds.resize(gpu::CLUSTER_COUNT);

    for (std::uint32_t z = 0; z < slices; ++z) {
        const double z0 = nearPlane * std::pow(farPlane / nearPlane, static_cast<double>(z) / slices);
        const double z1 = nearPlane * std::pow(farPlane / nearPlane, static_cast<double>(z + 1) / slices);
        for (std::uint32_t y = 0; y < tilesY; ++y) {
            // Pixel rows run down; view +Y runs up, and clip y = -f * y.
            const double ndcTop = -1.0 + 2.0 * y / tilesY;
            const double ndcBottom = -1.0 + 2.0 * (y + 1) / tilesY;
            for (std::uint32_t x = 0; x < tilesX; ++x) {
                const double ndcLeft = -1.0 + 2.0 * x / tilesX;
                const double ndcRight = -1.0 + 2.0 * (x + 1) / tilesX;
                double minX = INFINITY, maxX = -INFINITY, minY = INFINITY, maxY = -INFINITY;
                for (const double depth : {z0, z1}) {
                    for (const double ndc : {ndcLeft, ndcRight}) {
                        const double viewX = ndc * depth * aspect / f;
                        minX = std::min(minX, viewX);
                        maxX = std::max(maxX, viewX);
                    }
                    for (const double ndc : {ndcTop, ndcBottom}) {
                        const double viewY = -ndc * depth / f;
                        minY = std::min(minY, viewY);
                        maxY = std::max(maxY, viewY);
                    }
                }
                grid.bounds[x + y * tilesX + z * tilesX * tilesY] = {
                    {static_cast<float>(minX), static_cast<float>(minY), static_cast<float>(z0), 0},
                    {static_cast<float>(maxX), static_cast<float>(maxY), static_cast<float>(z1), 0}};
            }
        }
    }
    return grid;
}

std::uint32_t clusterOf(const ClusterGrid& grid, float pixelX, float pixelY, float viewZ, std::uint32_t width,
                        std::uint32_t height, float nearPlane) noexcept {
    const auto [tilesX, tilesY, slices] = gpu::CLUSTER_GRID;
    const std::uint32_t x = std::min(static_cast<std::uint32_t>(pixelX * tilesX / width), tilesX - 1);
    const std::uint32_t y = std::min(static_cast<std::uint32_t>(pixelY * tilesY / height), tilesY - 1);
    const float slice = std::floor(std::log(std::max(viewZ, nearPlane)) * grid.depthScale - grid.depthBias);
    const auto z = static_cast<std::uint32_t>(std::clamp(slice, 0.0f, static_cast<float>(slices - 1)));
    return x + y * tilesX + z * tilesX * tilesY;
}

} // namespace uta::urender
