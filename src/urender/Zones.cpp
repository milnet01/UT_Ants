#include "urender/Zones.h"

#include "urender/ShaderTypes.h"

namespace uta::urender {

std::vector<std::uint8_t> zonesSeeingSky(const ubundle::Geometry& geometry, std::size_t zoneCount) {
    std::vector<std::uint8_t> sky(zoneCount, 0);
    for (const ubundle::GeometryBatch& batch : geometry.batches) {
        // Bit by bit, never by equality: a two-sided window is still a window.
        if ((batch.polyFlags & gpu::PF_FAKE_BACKDROP) == 0) continue;
        const std::size_t end = std::size_t{batch.firstIndex} + batch.indexCount;
        for (std::size_t i = batch.firstIndex; i < end && i < geometry.indices.size(); ++i) {
            const std::uint32_t vertex = geometry.indices[i];
            if (vertex >= geometry.vertices.size()) continue;
            const std::uint8_t zone = geometry.vertices[vertex].zone;
            if (zone < zoneCount) sky[zone] = 1;
        }
    }
    return sky;
}

} // namespace uta::urender
