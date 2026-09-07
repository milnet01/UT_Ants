#include "upkg/Geometry.h"

#include "upkg/ByteReader.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace uta::upkg {
namespace {

Error malformed(std::string message) {
    return Error(ErrorCode::MalformedData, std::move(message));
}

// The SMALLEST encoding of each element, for the count checks. A compact
// index is one to five bytes wide, so a table holding one has a minimum
// rather than a fixed width. SS 4.5 of UTA-0069 states the layouts these are
// summed from.
constexpr std::size_t VECTOR_BYTES = 12; // FVector
constexpr std::size_t BSP_NODE_MIN_BYTES = 43;
constexpr std::size_t BSP_SURF_MIN_BYTES = 16;
constexpr std::size_t VERT_MIN_BYTES = 2;
constexpr std::size_t ZONE_MIN_BYTES = 17;
constexpr std::size_t LIGHT_MAP_MIN_BYTES = 30;
constexpr std::size_t LIGHT_BITS_BYTES = 1;
constexpr std::size_t BOX_BYTES = 25;
constexpr std::size_t LEAF_HULL_BYTES = 4;
constexpr std::size_t LIGHT_MIN_BYTES = 1;

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

/// A `Model` table's element count comes from the file, so it is checked
/// against what is left before anything is reserved -- INV-2, and the reason
/// is UTA-0004 INV-4's: the four-byte edit that asks for gigabytes.
///
/// `elementBytes` is the element's SMALLEST encoding, because a compact index
/// is one to five bytes wide. The check bounds the allocation; it does not
/// predict where the table ends.
Result<void> checkCount(const ByteReader& reader, std::int32_t count,
                        std::size_t elementBytes, std::string_view what) {
    if (count < 0) {
        return std::unexpected(malformed("a Model declares " + std::to_string(count) +
                                         " " + std::string(what)));
    }
    if (static_cast<std::size_t>(count) > reader.remaining() / elementBytes) {
        return std::unexpected(malformed(
            "a Model declares " + std::to_string(count) + " " + std::string(what) +
            ", more than the " + std::to_string(reader.remaining()) +
            " bytes remaining can hold"));
    }
    return {};
}

/// Every table in SS 4.4 is prefixed by a compact index giving its element
/// count, so an empty table costs one zero byte.
template <typename Element, typename ReadElement>
Result<std::vector<Element>> readTable(ByteReader& reader, std::size_t elementBytes,
                                       std::string_view what, ReadElement readElement) {
    UTA_TRY(const std::int32_t count, reader.readIndex());
    UTA_CHECK(checkCount(reader, count, elementBytes, what));
    std::vector<Element> table;
    table.reserve(static_cast<std::size_t>(count));
    for (std::int32_t element = 0; element < count; ++element) {
        UTA_TRY(Element value, readElement(reader));
        table.push_back(std::move(value));
    }
    return table;
}

/// An object reference is the file's own compact index, returned unresolved
/// for the reason Polygon::texture records.
Result<ObjectReference> readReference(ByteReader& reader) {
    UTA_TRY(const std::int32_t raw, reader.readIndex());
    return ObjectReference{raw};
}

Result<Plane> readPlane(ByteReader& reader) {
    Plane plane;
    UTA_TRY(plane.normal, readVector(reader));
    UTA_TRY(plane.w, reader.readFloat());
    return plane;
}

Result<Box> readBox(ByteReader& reader) {
    Box box;
    UTA_TRY(box.min, readVector(reader));
    UTA_TRY(box.max, readVector(reader));
    UTA_TRY(const std::uint8_t valid, reader.readU8());
    box.valid = valid != 0;
    return box;
}

Result<BspNode> readBspNode(ByteReader& reader) {
    BspNode node;
    UTA_TRY(node.plane, readPlane(reader));
    // ZoneMask is a 64-bit mask. ByteReader offers readI64 and no readU64, so
    // it is read signed and reinterpreted, as readPolys does for PanU.
    UTA_TRY(const std::int64_t zoneMask, reader.readI64());
    node.zoneMask = static_cast<std::uint64_t>(zoneMask);
    UTA_TRY(node.nodeFlags, reader.readU8());
    UTA_TRY(node.iVertPool, reader.readIndex());
    UTA_TRY(node.iSurf, reader.readIndex());
    UTA_TRY(node.iFront, reader.readIndex());
    UTA_TRY(node.iBack, reader.readIndex());
    UTA_TRY(node.iPlane, reader.readIndex());
    UTA_TRY(node.iCollisionBound, reader.readIndex());
    UTA_TRY(node.iRenderBound, reader.readIndex());
    UTA_TRY(node.iZone[0], reader.readU8());
    UTA_TRY(node.iZone[1], reader.readU8());
    UTA_TRY(node.numVertices, reader.readU8());
    // SS 4.5: iLeaf is two raw i32. Read as compact indices the walk failed
    // thousands of exports; as i32 it failed three, and nothing else moved.
    UTA_TRY(node.iLeaf[0], reader.readI32());
    UTA_TRY(node.iLeaf[1], reader.readI32());
    return node;
}

Result<BspSurf> readBspSurf(ByteReader& reader) {
    BspSurf surf;
    UTA_TRY(surf.texture, readReference(reader));
    UTA_TRY(surf.polyFlags, reader.readU32());
    UTA_TRY(surf.pBase, reader.readIndex());
    UTA_TRY(surf.vNormal, reader.readIndex());
    UTA_TRY(surf.vTextureU, reader.readIndex());
    UTA_TRY(surf.vTextureV, reader.readIndex());
    UTA_TRY(surf.iLightMap, reader.readIndex());
    UTA_TRY(surf.iBrushPoly, reader.readIndex());
    // Signed 16-bit, so read as u16 and reinterpreted rather than
    // sign-extended from a wider read -- readPolys does the same.
    UTA_TRY(const std::uint16_t panU, reader.readU16());
    UTA_TRY(const std::uint16_t panV, reader.readU16());
    surf.panU = static_cast<std::int16_t>(panU);
    surf.panV = static_cast<std::int16_t>(panV);
    UTA_TRY(surf.actor, readReference(reader));
    return surf;
}

Result<Vert> readVert(ByteReader& reader) {
    Vert vert;
    UTA_TRY(vert.pVertex, reader.readIndex());
    UTA_TRY(vert.iSide, reader.readIndex());
    return vert;
}

Result<ZoneProperties> readZoneProperties(ByteReader& reader) {
    ZoneProperties zone;
    UTA_TRY(zone.zoneActor, readReference(reader));
    UTA_TRY(zone.connectivity, reader.readI64());
    UTA_TRY(zone.visibility, reader.readI64());
    return zone;
}

Result<LightMapIndex> readLightMapIndex(ByteReader& reader) {
    LightMapIndex entry;
    // Both indices are compact, not raw i32 -- SS 4.5 measured each reading.
    UTA_TRY(entry.dataOffset, reader.readIndex());
    UTA_TRY(entry.iLightActors, reader.readIndex());
    UTA_TRY(entry.pan, readVector(reader));
    UTA_TRY(entry.uScale, reader.readFloat());
    UTA_TRY(entry.vScale, reader.readFloat());
    UTA_TRY(entry.uClamp, reader.readI32());
    UTA_TRY(entry.vClamp, reader.readI32());
    return entry;
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

Result<Model> readModel(const Package& package, const ExportEntry& entry) {
    UTA_TRY(const PropertyList list, readPropertyList(package, entry));
    UTA_TRY(const std::span<const std::byte> data, package.serialBytes(entry));

    Model model;
    if (data.empty()) {
        return model; // a sizeless export is ordinary -- SS 6, UTA-0003 INV-7
    }

    ByteReader reader{data, list.nativeOffset};

    // The 41-byte prefix: FBox then FSphere. UTA-0004 SS 4.5 verified both.
    UTA_TRY(model.boundsMin, readVector(reader));
    UTA_TRY(model.boundsMax, readVector(reader));
    UTA_TRY(const std::uint8_t boundsValid, reader.readU8());
    model.boundsValid = boundsValid != 0;
    UTA_TRY(model.sphereCentre, readVector(reader));
    UTA_TRY(model.sphereRadius, reader.readFloat());

    // The first run of arrays. SS 4.4 derived which is which; SS 10 records
    // that nothing can separate Vectors from Points, both being FVector.
    UTA_TRY(model.vectors,
            readTable<Vector3>(reader, VECTOR_BYTES, "vectors", readVector));
    UTA_TRY(model.points, readTable<Vector3>(reader, VECTOR_BYTES, "points", readVector));
    UTA_TRY(model.nodes,
            readTable<BspNode>(reader, BSP_NODE_MIN_BYTES, "nodes", readBspNode));
    UTA_TRY(model.surfs,
            readTable<BspSurf>(reader, BSP_SURF_MIN_BYTES, "surfs", readBspSurf));
    UTA_TRY(model.verts, readTable<Vert>(reader, VERT_MIN_BYTES, "verts", readVert));

    // Two raw i32 between the runs, not index-prefixed arrays. This is the
    // part the community order gets wrong (SS 4.4), and NumZones counts the
    // records that follow it rather than prefixing an array of its own.
    UTA_TRY(model.numSharedSides, reader.readI32());
    UTA_TRY(const std::int32_t zones, reader.readI32());
    UTA_CHECK(checkCount(reader, zones, ZONE_MIN_BYTES, "zones"));
    model.zones.reserve(static_cast<std::size_t>(zones));
    for (std::int32_t zone = 0; zone < zones; ++zone) {
        UTA_TRY(const ZoneProperties properties, readZoneProperties(reader));
        model.zones.push_back(properties);
    }

    // INV-5 is what checks this field's position: a byte count that merely
    // adds up cannot tell a correct assignment from a wrong one.
    UTA_TRY(model.polys, readReference(reader));

    // The second run. It holds SIX arrays against the first run's five --
    // UTA-0004 SS 4.5 called it "shorter", and SS 4.4 supersedes that.
    UTA_TRY(model.lightMap, readTable<LightMapIndex>(reader, LIGHT_MAP_MIN_BYTES,
                                                     "lightmap entries",
                                                     readLightMapIndex));
    UTA_TRY(model.lightBits,
            readTable<std::uint8_t>(reader, LIGHT_BITS_BYTES, "lightmap bytes",
                                    [](ByteReader& bytes) { return bytes.readU8(); }));
    UTA_TRY(model.bounds, readTable<Box>(reader, BOX_BYTES, "bounds", readBox));
    UTA_TRY(model.leafHulls,
            readTable<std::int32_t>(reader, LEAF_HULL_BYTES, "leaf hulls",
                                    [](ByteReader& bytes) { return bytes.readI32(); }));

    // SS 4.1: Leaves is neither returned nor stepped over. An empty table
    // costs one zero byte and is consumed like any other; a populated one is
    // a table this reader does not yet describe, and stepping over it would
    // need the very element width SS 4.6 withholds.
    UTA_TRY(const std::int32_t leaves, reader.readIndex());
    if (leaves != 0) {
        return std::unexpected(malformed(
            "a Model declares " + std::to_string(leaves) +
            " leaves, whose layout UTA-0069 SS 4.6 has not derived"));
    }

    UTA_TRY(model.lights,
            readTable<ObjectReference>(reader, LIGHT_MIN_BYTES, "lights", readReference));

    UTA_TRY(model.rootOutside, reader.readI32());
    UTA_TRY(model.linked, reader.readI32());

    // SS 4.3: a reader that models the layout correctly ends exactly here.
    // Anywhere else means a field's width is wrong, and a partial result is
    // never returned (INV-1).
    if (reader.remaining() != 0) {
        return std::unexpected(malformed(
            "a Model left " + std::to_string(reader.remaining()) +
            " bytes unread; the layout does not match the export"));
    }
    return model;
}

} // namespace uta::upkg
