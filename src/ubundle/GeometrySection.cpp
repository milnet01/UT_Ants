// The GEOM section: the level's drawable surfaces as triangles --
// docs/specs/UTA-0109-map-geometry.md SS 4.2, INV-1 and INV-2.

#include "Sections.h"

#include <string>
#include <tuple>

namespace uta::ubundle::detail {
namespace {

/// SS 4.2: eight f32, fixed.
constexpr std::uint64_t MIN_GEOMETRY_VERTEX = 32;

/// SS 4.2: a u32 length for an empty material, then three u32.
constexpr std::uint64_t MIN_GEOMETRY_BATCH = 16;

[[nodiscard]] Result<GeometryVertex> readVertex(Cursor& cursor) {
    GeometryVertex vertex;
    for (float& part : vertex.position) {
        UTA_TRY(part, cursor.readF32());
    }
    for (float& part : vertex.normal) {
        UTA_TRY(part, cursor.readF32());
    }
    UTA_TRY(vertex.u, cursor.readF32());
    UTA_TRY(vertex.v, cursor.readF32());
    return vertex;
}

[[nodiscard]] Result<GeometryBatch> readBatch(Cursor& cursor) {
    GeometryBatch batch;
    UTA_TRY(batch.material, readString(cursor));
    UTA_TRY(batch.polyFlags, cursor.readU32());
    UTA_TRY(batch.firstIndex, cursor.readU32());
    UTA_TRY(batch.indexCount, cursor.readU32());
    return batch;
}

void putVertex(Sink& sink, const GeometryVertex& vertex) {
    for (const float part : vertex.position) sink.putF32(part);
    for (const float part : vertex.normal) sink.putF32(part);
    sink.putF32(vertex.u);
    sink.putF32(vertex.v);
}

void putBatch(Sink& sink, const GeometryBatch& batch) {
    sink.putString(batch.material);
    sink.putU32(batch.polyFlags);
    sink.putU32(batch.firstIndex);
    sink.putU32(batch.indexCount);
}

} // namespace

Result<Geometry> readGeometry(Cursor& cursor) {
    Geometry geometry;
    UTA_TRY(geometry.vertices,
            readVector<GeometryVertex>(cursor, MIN_GEOMETRY_VERTEX, "vertices", readVertex));
    UTA_TRY(geometry.indices,
            readVector<std::uint32_t>(cursor, MIN_U32, "indices", readU32Element));
    UTA_TRY(geometry.batches,
            readVector<GeometryBatch>(cursor, MIN_GEOMETRY_BATCH, "batches", readBatch));
    return geometry;
}

Result<void> validateGeometry(const Geometry& geometry, ErrorCode code) {
    const std::size_t vertexCount = geometry.vertices.size();
    for (std::size_t i = 0; i < geometry.indices.size(); ++i) {
        if (geometry.indices[i] >= vertexCount)
            return fail(code, "GEOM: index " + std::to_string(i) + " names vertex "
                                  + std::to_string(geometry.indices[i]) + " of "
                                  + std::to_string(vertexCount));
    }

    // The batches tile the indices. The running end is a u64, so no
    // firstIndex + indexCount can wrap onto a start that tiles by accident --
    // INV-2's {3, 4294967295} case is exactly that wrap.
    std::uint64_t end = 0;
    for (std::size_t i = 0; i < geometry.batches.size(); ++i) {
        const GeometryBatch& batch = geometry.batches[i];
        // Strictly ascending, which also makes each key unique. std::string's
        // `<` compares as unsigned char, so this is SS 4.2's bytewise order on
        // every compiler, as MATS's check is.
        if (i > 0) {
            const GeometryBatch& before = geometry.batches[i - 1];
            if (!(std::tie(before.material, before.polyFlags)
                  < std::tie(batch.material, batch.polyFlags)))
                return fail(code, "GEOM: batch " + std::to_string(i)
                                      + "'s key does not sort strictly after the one before it");
        }
        if (batch.indexCount == 0 || batch.indexCount % 3 != 0)
            return fail(code, "GEOM: batch " + std::to_string(i) + " holds "
                                  + std::to_string(batch.indexCount)
                                  + " indices, which is not a whole number of triangles");
        if (batch.firstIndex != end)
            return fail(code, "GEOM: batch " + std::to_string(i)
                                  + " does not start where the one before it ended");
        end += batch.indexCount;
    }
    if (end != geometry.indices.size())
        return fail(code, "GEOM: the batches do not end at the end of the index table");
    return {};
}

std::vector<std::byte> encodeGeometry(const Geometry& geometry) {
    Sink sink;
    sink.putVector(geometry.vertices, putVertex);
    sink.putVector(geometry.indices, [](Sink& out, std::uint32_t index) { out.putU32(index); });
    sink.putVector(geometry.batches, putBatch);
    return std::move(sink).take();
}

} // namespace uta::ubundle::detail
