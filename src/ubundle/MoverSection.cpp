// The MOVR section: each mover's shape, in its pivot space --
// docs/specs/UTA-0119-mover-shapes.md SS 4.2, INV-1 and INV-2.
//
// A shape's geometry is GEOM's own encoding and GEOM's own rules, so this file
// reads, writes and validates it through GeometrySection.cpp rather than
// carrying a second copy of either.

#include "Sections.h"

#include <algorithm>
#include <string>
#include <utility>

namespace uta::ubundle::detail {
namespace {

/// SS 4.2: the four fixed fields, UTA-0439's still byte and three empty
/// vectors.
constexpr std::uint64_t MIN_MOVER_SHAPE = 53;

[[nodiscard]] Result<MoverShape> readShape(Cursor& cursor) {
    MoverShape shape;
    UTA_TRY(shape.exportIndex, cursor.readU32());
    for (float& part : shape.location) {
        UTA_TRY(part, cursor.readF32());
    }
    for (std::int32_t& part : shape.rotation) {
        UTA_TRY(part, cursor.readI32());
    }
    for (float& part : shape.postScale) {
        UTA_TRY(part, cursor.readF32());
    }
    UTA_TRY(shape.still, cursor.readU8());
    UTA_TRY(shape.geometry, readGeometry(cursor));
    return shape;
}

void putShape(Sink& sink, const MoverShape& shape) {
    sink.putU32(shape.exportIndex);
    for (const float part : shape.location) sink.putF32(part);
    for (const std::int32_t part : shape.rotation) sink.putI32(part);
    for (const float part : shape.postScale) sink.putF32(part);
    sink.putU8(shape.still);
    sink.putEncoded(encodeGeometry(shape.geometry));
}

} // namespace

Result<std::vector<MoverShape>> readMovers(Cursor& cursor) {
    return readVector<MoverShape>(cursor, MIN_MOVER_SHAPE, "mover shapes", readShape);
}

Result<void> validateMovers(const std::vector<MoverShape>& movers, ErrorCode code) {
    for (std::size_t i = 0; i < movers.size(); ++i) {
        // Strictly ascending, which also makes each slot unique.
        if (i > 0 && !(movers[i - 1].exportIndex < movers[i].exportIndex))
            return fail(code, "MOVR: shape " + std::to_string(i)
                                  + "'s exportIndex does not sort strictly after the one before it");
        if (movers[i].still > 1)
            return fail(code, "MOVR: shape " + std::to_string(i) + "'s still is "
                                  + std::to_string(movers[i].still) + ", above 1");
        const Result<void> geometry = validateGeometry(movers[i].geometry, code);
        if (!geometry.has_value())
            return fail(code, "MOVR: shape " + std::to_string(i) + "'s geometry: "
                                  + std::string(geometry.error().message()));
    }
    return {};
}

std::string chartedVertexOwners(const Bundle& bundle) {
    const bool still = bundle.movers && std::ranges::any_of(*bundle.movers, [](const MoverShape& mover) {
        return mover.still != 0 && !mover.geometry.vertices.empty();
    });
    return still ? "GEOM and its still movers " : "GEOM ";
}

Result<std::vector<std::byte>> encodeMovers(const std::vector<MoverShape>& movers) {
    Sink sink;
    sink.putVector(movers, putShape);
    return std::move(sink).finish("MOVR");
}

} // namespace uta::ubundle::detail

namespace uta::ubundle {

std::size_t chartedVertexCount(const Bundle& bundle) noexcept {
    std::size_t count = bundle.geometry ? bundle.geometry->vertices.size() : 0;
    if (bundle.movers)
        for (const MoverShape& mover : *bundle.movers)
            if (mover.still != 0) count += mover.geometry.vertices.size();
    return count;
}

} // namespace uta::ubundle
