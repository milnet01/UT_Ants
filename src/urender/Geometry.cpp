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
static_assert(sizeof(ubundle::GeometryVertex) == 36);
static_assert(offsetof(ubundle::GeometryVertex, position) == 0);
static_assert(offsetof(ubundle::GeometryVertex, normal) == 12);
static_assert(offsetof(ubundle::GeometryVertex, u) == 24);
static_assert(offsetof(ubundle::GeometryVertex, v) == 28);
static_assert(offsetof(ubundle::GeometryVertex, zone) == 32); // UTA-0156 SS 4.4
static_assert(offsetof(ubundle::GeometryVertex, sway) == 33); // UTA-0055 SS 4.5
static_assert(offsetof(ubundle::GeometryVertex, swayWeight) == 34);
static_assert(offsetof(ubundle::GeometryVertex, swayCell) == 35);
// Binding 1, likewise.
static_assert(sizeof(SurfaceVertex) == 20);
static_assert(offsetof(SurfaceVertex, occlusionUv) == 0);
static_assert(offsetof(SurfaceVertex, maskChart) == 8); // UTA-0326 SS 4.5
static_assert(offsetof(SurfaceVertex, maskTexel) == 12);

Result<SceneGeometry> SceneGeometry::upload(Gpu& gpu, const ubundle::Bundle& bundle, MaterialSet& materials) {
    SceneGeometry scene;
    std::vector<ubundle::GeometryVertex> vertices;
    std::vector<SurfaceVertex> surfaceVertices;
    std::vector<std::uint32_t> indices;
    // UTA-0164 SS 4.5: the white block's centre, for every vertex AOCC does not
    // place. With no AOCC the texture is NONE and the value is never read.
    const std::array<float, 2> white =
        bundle.occlusion ? std::array<float, 2>{2.0f / static_cast<float>(bundle.occlusion->width),
                                                2.0f / static_cast<float>(bundle.occlusion->height)}
                         : std::array<float, 2>{0.0f, 0.0f};

    // UTA-0439 SS 4.4: AOCC and SMSK hold GEOM's vertex entries, then each
    // still mover's, handed out in that order as `append` reaches them. A
    // section of any other count is read by nobody.
    const std::size_t charted = ubundle::chartedVertexCount(bundle);
    const bool occluded = bundle.occlusion && bundle.occlusion->uv.size() == charted;
    const ubundle::ShadowMask* const mask = bundle.shadowMask && bundle.shadowMask->vertexChart.size() == charted
                                                    && bundle.shadowMask->vertexTexel.size() == charted
                                                ? &*bundle.shadowMask
                                                : nullptr;
    std::size_t entry = 0;

    const auto append = [&](const ubundle::Geometry& geometry, std::uint32_t objectIndex, bool chartedHere,
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
            // SS 4.5: PF_Invisible cannot appear, and is not drawn if it does.
            //
            // UTA-0188 REMOVED PF_PORTAL FROM THIS TEST, and the removal is the
            // fix rather than a relaxation. In UT99 a zone portal is a surface
            // like any other: the polygon dividing an air zone from a water
            // zone IS the water, wearing the water texture. Discarding every
            // portal threw that away -- AS-Frigate's sea is one surface under
            // 56 nodes, flagged PF_PORTAL and nothing else that says "water",
            // and the band showed the hull behind it.
            //
            // What made the old test look safe is that it conflated two cases.
            // A portal meant to be unseen is flagged PF_INVISIBLE as well, and
            // ubake's Geometry.cpp already drops those before a batch is built
            // -- so the surfaces still arriving here are the ones UT99 draws,
            // and the PF_PORTAL half of this test could only ever remove those.
            if ((batch.polyFlags & gpu::PF_INVISIBLE) != 0) continue;
            if (batch.indexCount == 0) continue;
            scene.draws.push_back({objectIndex, materials.indexOf(batch.material), batch.polyFlags,
                                   firstIndex + batch.firstIndex, batch.indexCount, firstVertex, batch.panRate});
        }
        vertices.insert(vertices.end(), geometry.vertices.begin(), geometry.vertices.end());
        for (std::size_t i = 0; i < geometry.vertices.size(); ++i) {
            const std::size_t at = entry + i;
            SurfaceVertex surface{chartedHere && occluded ? bundle.occlusion->uv[at] : white};
            if (chartedHere && mask != nullptr) {
                // A chart past SMSK's would be read past the end of MASK_CHARTS.
                if (mask->vertexChart[at] != ubundle::MASK_NO_CHART && mask->vertexChart[at] >= mask->charts.size())
                    return fail(ErrorCode::InvalidArgument,
                                std::format("{} vertex {} names chart {} of SMSK's {}", what, i,
                                            mask->vertexChart[at], mask->charts.size()));
                surface.maskChart = mask->vertexChart[at];
                surface.maskTexel = mask->vertexTexel[at];
            }
            surfaceVertices.push_back(surface);
        }
        if (chartedHere) entry += geometry.vertices.size();
        indices.insert(indices.end(), geometry.indices.begin(), geometry.indices.end());
        return {};
    };

    if (bundle.geometry) UTA_CHECK(append(*bundle.geometry, 0, true, "GEOM"));
    if (bundle.movers) {
        for (std::size_t i = 0; i < bundle.movers->size(); ++i) {
            const ubundle::MoverShape& mover = (*bundle.movers)[i];
            UTA_CHECK(append(mover.geometry, static_cast<std::uint32_t>(i + 1), mover.still != 0,
                             std::format("mover {}", mover.exportIndex)));
        }
        scene.objectCount = static_cast<std::uint32_t>(bundle.movers->size() + 1);
    }

    UTA_TRY(scene.vertices, Buffer::upload(gpu, std::as_bytes(std::span(vertices)), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
    // A buffer of no bytes cannot be created; a level with no vertex draws nothing.
    if (surfaceVertices.empty()) surfaceVertices.push_back(SurfaceVertex{white});
    UTA_TRY(scene.surfaceVertices, Buffer::upload(gpu, std::as_bytes(std::span(surfaceVertices)),
                                                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
    UTA_TRY(scene.indices, Buffer::upload(gpu, std::as_bytes(std::span(indices)), VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
    return scene;
}

} // namespace uta::urender
