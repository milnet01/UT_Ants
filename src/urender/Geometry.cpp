// A bundle's geometry on the GPU -- Geometry.h.

#include "urender/Geometry.h"

#include "urender/ShaderTypes.h"

#include <cstddef>
#include <format>
#include <limits>
#include <span>

namespace uta::urender {

// The vertex buffer is ubundle::GeometryVertex's own bytes, so its layout is
// the pipeline's vertex input (Pipelines.cpp). Asserted, as INV-9 asserts
// every other struct a shader reads.
static_assert(sizeof(ubundle::GeometryVertex) == 32);
static_assert(offsetof(ubundle::GeometryVertex, position) == 0);
static_assert(offsetof(ubundle::GeometryVertex, normal) == 12);
static_assert(offsetof(ubundle::GeometryVertex, u) == 24);
static_assert(offsetof(ubundle::GeometryVertex, v) == 28);

Result<SceneGeometry> SceneGeometry::upload(Gpu& gpu, const ubundle::Bundle& bundle, MaterialSet& materials) {
    SceneGeometry scene;
    std::vector<ubundle::GeometryVertex> vertices;
    std::vector<std::uint32_t> indices;

    const auto append = [&](const ubundle::Geometry& geometry, std::uint32_t objectIndex,
                            std::string_view what) -> Result<void> {
        if (vertices.size() + geometry.vertices.size() > std::numeric_limits<std::int32_t>::max()
            || indices.size() + geometry.indices.size() > std::numeric_limits<std::uint32_t>::max())
            return fail(ErrorCode::InvalidArgument, std::format("{} has too many vertices or indices to draw", what));
        for (const std::uint32_t index : geometry.indices)
            if (index >= geometry.vertices.size())
                return fail(ErrorCode::InvalidArgument,
                            std::format("{} has an index {} past its {} vertices", what, index,
                                        geometry.vertices.size()));

        const auto firstVertex = static_cast<std::int32_t>(vertices.size());
        const auto firstIndex = static_cast<std::uint32_t>(indices.size());
        for (const ubundle::GeometryBatch& batch : geometry.batches) {
            if (static_cast<std::uint64_t>(batch.firstIndex) + batch.indexCount > geometry.indices.size())
                return fail(ErrorCode::InvalidArgument,
                            std::format("{} has a batch reaching index {} of {}", what,
                                        static_cast<std::uint64_t>(batch.firstIndex) + batch.indexCount,
                                        geometry.indices.size()));
            // SS 4.5: PF_Portal is a visibility marker and is not drawn;
            // PF_Invisible cannot appear, and is not drawn if it does.
            if ((batch.polyFlags & (gpu::PF_PORTAL | gpu::PF_INVISIBLE)) != 0) continue;
            if (batch.indexCount == 0) continue;
            scene.draws.push_back({objectIndex, materials.indexOf(batch.material), batch.polyFlags,
                                   firstIndex + batch.firstIndex, batch.indexCount, firstVertex});
        }
        vertices.insert(vertices.end(), geometry.vertices.begin(), geometry.vertices.end());
        indices.insert(indices.end(), geometry.indices.begin(), geometry.indices.end());
        return {};
    };

    if (bundle.geometry) UTA_CHECK(append(*bundle.geometry, 0, "GEOM"));
    if (bundle.movers) {
        for (std::size_t i = 0; i < bundle.movers->size(); ++i) {
            const ubundle::MoverShape& mover = (*bundle.movers)[i];
            UTA_CHECK(append(mover.geometry, static_cast<std::uint32_t>(i + 1),
                             std::format("mover {}", mover.exportIndex)));
        }
        scene.objectCount = static_cast<std::uint32_t>(bundle.movers->size() + 1);
    }

    UTA_TRY(scene.vertices, Buffer::upload(gpu, std::as_bytes(std::span(vertices)), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
    UTA_TRY(scene.indices, Buffer::upload(gpu, std::as_bytes(std::span(indices)), VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
    return scene;
}

} // namespace uta::urender
