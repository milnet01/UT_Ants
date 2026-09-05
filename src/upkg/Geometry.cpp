#include "upkg/Geometry.h"

#include "upkg/ByteReader.h"

#include <string>
#include <utility>

namespace uta::upkg {
namespace {

Error malformed(std::string message) {
    return Error(ErrorCode::MalformedData, std::move(message));
}

Result<Vector3> readVector(ByteReader& reader) {
    Vector3 value;
    UTA_TRY(value.x, reader.readFloat());
    UTA_TRY(value.y, reader.readFloat());
    UTA_TRY(value.z, reader.readFloat());
    return value;
}

/// Twelve bytes a vertex, and the count comes from the file. Checked against
/// what is left before the vector is grown, which is INV-4: reserving from an
/// unchecked count is how a four-byte edit asks for gigabytes.
Result<void> reserveVertices(const ByteReader& reader, std::vector<Vector3>& into,
                             std::int32_t count) {
    constexpr std::size_t VECTOR_BYTES = 12;
    if (count < 0) {
        return std::unexpected(
            malformed("a polygon declares " + std::to_string(count) + " vertices"));
    }
    const auto wanted = static_cast<std::size_t>(count);
    if (wanted > reader.remaining() / VECTOR_BYTES) {
        return std::unexpected(malformed(
            "a polygon declares " + std::to_string(count) +
            " vertices, more than the " + std::to_string(reader.remaining()) +
            " bytes remaining can hold"));
    }
    into.reserve(wanted);
    return {};
}

} // namespace

Result<Polys> readPolys(const Package& package, const ExportEntry& entry) {
    UTA_TRY(const PropertyList list, readPropertyList(package, entry));
    UTA_TRY(const std::span<const std::byte> data, package.serialBytes(entry));

    Polys polys;
    if (data.empty()) {
        return polys; // a sizeless export is ordinary -- SS 6, UTA-0003 INV-7
    }

    ByteReader reader{data, list.nativeOffset};

    // Num, then Max. Max is the array's allocated size and is not used: it is
    // read so the cursor lands on the first polygon, which SS 4.3 requires.
    UTA_TRY(const std::int32_t count, reader.readI32());
    UTA_TRY([[maybe_unused]] const std::int32_t capacity, reader.readI32());
    if (count < 0) {
        return std::unexpected(
            malformed("a Polys declares " + std::to_string(count) + " polygons"));
    }

    // One byte per polygon is the floor: a polygon cannot serialise smaller
    // than its own vertex count. Checked before reserving -- INV-4.
    if (static_cast<std::size_t>(count) > reader.remaining()) {
        return std::unexpected(malformed(
            "a Polys declares " + std::to_string(count) + " polygons, more than the " +
            std::to_string(reader.remaining()) + " bytes remaining can hold"));
    }
    polys.polygons.reserve(static_cast<std::size_t>(count));

    for (std::int32_t index = 0; index < count; ++index) {
        Polygon polygon;
        UTA_TRY(const std::int32_t vertexCount, reader.readIndex());
        UTA_CHECK(reserveVertices(reader, polygon.vertices, vertexCount));

        UTA_TRY(polygon.base, readVector(reader));
        UTA_TRY(polygon.normal, readVector(reader));
        UTA_TRY(polygon.textureU, readVector(reader));
        UTA_TRY(polygon.textureV, readVector(reader));
        for (std::int32_t vertex = 0; vertex < vertexCount; ++vertex) {
            UTA_TRY(const Vector3 point, readVector(reader));
            polygon.vertices.push_back(point);
        }

        UTA_TRY(polygon.polyFlags, reader.readU32());
        UTA_TRY(const std::int32_t actor, reader.readIndex());
        polygon.actor = ObjectReference{actor};
        UTA_TRY(const std::int32_t texture, reader.readIndex());
        polygon.texture = ObjectReference{texture};

        UTA_TRY(const std::int32_t itemName, reader.readIndex());
        if (itemName < 0 || static_cast<std::size_t>(itemName) >= package.names().size()) {
            return std::unexpected(malformed(
                "a polygon names item index " + std::to_string(itemName) +
                ", which is outside the name table"));
        }
        polygon.itemName = static_cast<std::uint32_t>(itemName);

        UTA_TRY(polygon.link, reader.readIndex());
        UTA_TRY(polygon.brushPoly, reader.readIndex());

        // PanU and PanV are signed 16-bit, so they are read as u16 and
        // reinterpreted rather than sign-extended from a wider read.
        UTA_TRY(const std::uint16_t panU, reader.readU16());
        UTA_TRY(const std::uint16_t panV, reader.readU16());
        polygon.panU = static_cast<std::int16_t>(panU);
        polygon.panV = static_cast<std::int16_t>(panV);

        polys.polygons.push_back(std::move(polygon));
    }

    // SS 4.3: a reader that models the layout correctly ends exactly here.
    // Anywhere else means a field's width is wrong or a branch was missed, and
    // a partial result is never returned (INV-1).
    if (reader.remaining() != 0) {
        return std::unexpected(malformed(
            "a Polys left " + std::to_string(reader.remaining()) +
            " bytes unread; the layout does not match the export"));
    }
    return polys;
}

} // namespace uta::upkg
