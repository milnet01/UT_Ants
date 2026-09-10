#include "upkg/Geometry.h"

#include "upkg/ByteReader.h"

#include <cstddef>
#include <cstdint>
#include <span>
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
constexpr std::size_t LEAF_MIN_BYTES = 11;
constexpr std::size_t LIGHT_MIN_BYTES = 1;

/// The version from which a Model carries its BSP tables inline. Below it the
/// tables are separate exports the Model only references -- see readModel.
constexpr std::uint16_t VERSION_INLINE_BSP_TABLES = 62;

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

/// Name the part of a `Model` a failure happened in, for SS 4.6's file-order
/// derivation. `readTable` names its own; this is for everything between the
/// tables, which otherwise reports ByteReader's message and names nothing.
template <typename T>
Result<T> inModel(Result<T> result, std::string_view where) {
    if (!result.has_value()) {
        return std::unexpected(
            std::move(result).error().withContext("in a Model's " + std::string(where)));
    }
    return result;
}

/// Every table in SS 4.4 is prefixed by a compact index giving its element
/// count, so an empty table costs one zero byte.
template <typename Element, typename ReadElement>
Result<std::vector<Element>> readTable(ByteReader& reader, std::size_t elementBytes,
                                       std::string_view what, ReadElement readElement) {
    // The count prefix itself, named too: a misaligned cursor most often runs
    // out of bytes HERE rather than inside an element, and an unnamed failure
    // cannot be attributed to a table at all.
    UTA_TRY(const std::int32_t count, inModel(reader.readIndex(), what));
    UTA_CHECK(checkCount(reader, count, elementBytes, what));
    std::vector<Element> table;
    table.reserve(static_cast<std::size_t>(count));
    for (std::int32_t element = 0; element < count; ++element) {
        auto value = readElement(reader);
        if (!value.has_value()) {
            // SS 4.6 derives the residue in FILE ORDER, and that needs to know
            // which table the walk stopped in. A short read inside an element
            // otherwise reports ByteReader's own message, which names no
            // table -- so the failure cannot be attributed and the ordering
            // rule cannot be applied. The count-check path above already
            // names the table, so only this one needs the context.
            return std::unexpected(
                std::move(value).error().withContext("in a Model's " + std::string(what)));
        }
        table.push_back(*std::move(value));
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
    // iBack comes FIRST, and this is measured rather than read off the field
    // names. Swapping two adjacent compact indices changes no byte count, so
    // the parse-success walk UTA-0069 SS 4.5 settled this table with cannot
    // see the difference -- it read them the other way round and stayed green.
    // What sees it is UTA-0007 INV-2, which holds the descent against each
    // node's OWN zone record: 0 of 11451 probes agreed on one map with these
    // two the other way round, and 11406 agreed with them this way.
    // Corrected 2026-09-08 under UTA-0078.
    UTA_TRY(node.iBack, reader.readIndex());
    UTA_TRY(node.iFront, reader.readIndex());
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

/// Eleven bytes at its smallest: three compact indices then a sixty-four-bit
/// zone mask. Derived by sweeping candidate layouts against a signature that
/// requires `Lights` and the two trailing fields to land exactly on the
/// export's final byte -- this is the sole fit for all 842 exports in the
/// reference install that populate the table, and every permutation of the
/// same four fields fits none. Corroborated by `iZone` indexing the export's
/// own zone table on all 2784273 leaves.
Result<Leaf> readLeaf(ByteReader& reader) {
    Leaf leaf;
    UTA_TRY(leaf.iZone, reader.readIndex());
    UTA_TRY(leaf.iPermeating, reader.readIndex());
    UTA_TRY(leaf.iVolumetric, reader.readIndex());
    // Read signed and reinterpreted, as readBspNode does for ZoneMask:
    // ByteReader offers readI64 and no readU64.
    UTA_TRY(const std::int64_t visible, reader.readI64());
    leaf.visibleZones = static_cast<std::uint64_t>(visible);
    return leaf;
}

Result<ZoneProperties> readZoneProperties(ByteReader& reader) {
    ZoneProperties zone;
    UTA_TRY(zone.zoneActor, readReference(reader));
    UTA_TRY(zone.connectivity, reader.readI64());
    UTA_TRY(zone.visibility, reader.readI64());
    return zone;
}

/// Thirty bytes at its smallest, and SS 4.5's layout for it is wrong in every
/// part: it states two LEADING compact indices and four-byte clamps. The
/// offsets are raw `i32` and the CLAMPS are the compact ones, in the order
/// written here, which is the file's.
///
/// Reading `dataOffset` as a compact index is what stranded the cursor for
/// the whole second run of tables -- a first byte of 0x85 decodes to -5 and
/// consumes one byte where the field is four -- so the walk reached the later
/// tables misaligned and blamed whichever one it stopped in.
///
/// The clamps are a lightmap's texel dimensions, so they are usually 16 or 32
/// and fit a compact index's one byte; the element is thirty bytes whenever
/// they do. A clamp of 64 or more takes two, because 0x40 is the continue
/// bit, and that is the whole of the difference -- an element read at a fixed
/// thirty consumes 9560 exports and then shifts every later element of any
/// export holding one.
///
/// Derived by sweeping the element width against a signature that requires
/// the rest of the export to land exactly on its final byte. Corroborated
/// semantically rather than by that fit alone -- `dataOffset` lands inside
/// the export's own `lightBits` array and never decreases, `iLightActors` is
/// -1 or indexes `lights`, and both scales are finite and positive.
Result<LightMapIndex> readLightMapIndex(ByteReader& reader) {
    LightMapIndex entry;
    UTA_TRY(entry.dataOffset, reader.readI32());
    UTA_TRY(entry.pan, readVector(reader));
    UTA_TRY(entry.uClamp, reader.readIndex());
    UTA_TRY(entry.vClamp, reader.readIndex());
    UTA_TRY(entry.uScale, reader.readFloat());
    UTA_TRY(entry.vScale, reader.readFloat());
    UTA_TRY(entry.iLightActors, reader.readI32());
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
    // Below version 62 a Model holds no inline BSP tables at all: it holds six
    // object references, to separate Vectors, Points, BspNodes, BspSurfs,
    // Verts and Polys exports, behind a 37-byte prefix rather than 41. Walking
    // it as the layout below would blames the first table for a difference
    // that is the whole shape of the export, so it is refused by name.
    //
    // UTA-0072 is what lifts this, and UTA-0069's ROADMAP bullet carries the
    // derivation and its two proofs. The boundary is 62 rather than 63 because
    // that is the smallest claim the measurement supports: version 61 is the
    // only version below 62 the container accepts (Package.cpp MIN_VERSION),
    // and version 62 appears nowhere in the reference install, so nothing here
    // asserts which side of the change it falls on.
    if (package.header().packageVersion < VERSION_INLINE_BSP_TABLES) {
        return std::unexpected(
            Error(ErrorCode::UnsupportedVersion,
                  "a Model at package version " +
                      std::to_string(package.header().packageVersion) +
                      " keeps its BSP tables in separate exports rather than "
                      "inline; that layout is UTA-0072's, not this reader's"));
    }

    UTA_TRY(const PropertyList list,
            inModel(readPropertyList(package, entry), "property list"));
    UTA_TRY(const std::span<const std::byte> data,
            inModel(package.serialBytes(entry), "serialised bytes"));

    Model model;
    if (data.empty()) {
        return model; // a sizeless export is ordinary -- SS 6, UTA-0003 INV-7
    }

    ByteReader reader{data, list.nativeOffset};

    // The 41-byte prefix: FBox then FSphere. UTA-0004 SS 4.5 verified both.
    UTA_TRY(model.boundsMin, inModel(readVector(reader), "bounding prefix"));
    UTA_TRY(model.boundsMax, inModel(readVector(reader), "bounding prefix"));
    UTA_TRY(const std::uint8_t boundsValid, inModel(reader.readU8(), "bounding prefix"));
    model.boundsValid = boundsValid != 0;
    UTA_TRY(model.sphereCentre, inModel(readVector(reader), "bounding prefix"));
    UTA_TRY(model.sphereRadius, inModel(reader.readFloat(), "bounding prefix"));

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
    UTA_TRY(model.numSharedSides, inModel(reader.readI32(), "shared-side count"));
    UTA_TRY(const std::int32_t zones, inModel(reader.readI32(), "zone count"));
    UTA_CHECK(checkCount(reader, zones, ZONE_MIN_BYTES, "zones"));
    model.zones.reserve(static_cast<std::size_t>(zones));
    for (std::int32_t zone = 0; zone < zones; ++zone) {
        auto properties = readZoneProperties(reader);
        if (!properties.has_value()) {
            return std::unexpected(
                std::move(properties).error().withContext("in a Model's zones"));
        }
        model.zones.push_back(*properties);
    }

    // INV-5 is what checks this field's position: a byte count that merely
    // adds up cannot tell a correct assignment from a wrong one.
    UTA_TRY(model.polys, inModel(readReference(reader), "Polys reference"));

    // The second run. It holds SIX arrays against the first run's five --
    // UTA-0004 SS 4.5 called it "shorter", and SS 4.4 supersedes that.
    UTA_TRY(model.lightMap, readTable<LightMapIndex>(reader, LIGHT_MAP_MIN_BYTES,
                                                     "lightmap entries",
                                                     readLightMapIndex));
    // The lightmap bytes are one run, read in one call rather than one checked
    // byte at a time -- UTA-0096. The count and its bound are readTable's,
    // applied here unchanged; readBytes then applies its own bound as well.
    // src/ubundle/TextureSection.cpp reads block data the same way and says why.
    {
        UTA_TRY(const std::int32_t count, inModel(reader.readIndex(), "lightmap bytes"));
        UTA_CHECK(checkCount(reader, count, LIGHT_BITS_BYTES, "lightmap bytes"));
        UTA_TRY(const std::span<const std::byte> run,
                inModel(reader.readBytes(static_cast<std::size_t>(count)), "lightmap bytes"));
        const auto* first = reinterpret_cast<const std::uint8_t*>(run.data());
        model.lightBits.assign(first, first + run.size());
    }
    UTA_TRY(model.bounds, readTable<Box>(reader, BOX_BYTES, "bounds", readBox));
    UTA_TRY(model.leafHulls,
            readTable<std::int32_t>(reader, LEAF_HULL_BYTES, "leaf hulls",
                                    [](ByteReader& bytes) { return bytes.readI32(); }));

    // SS 4.1 refused a populated Leaves while SS 4.6 withheld the element's
    // layout. readLeaf carries that layout and what it was measured against,
    // so the table is read like any other from 2026-09-08.
    UTA_TRY(model.leaves, readTable<Leaf>(reader, LEAF_MIN_BYTES, "leaves", readLeaf));

    UTA_TRY(model.lights,
            readTable<ObjectReference>(reader, LIGHT_MIN_BYTES, "lights", readReference));

    auto rootOutside = reader.readI32();
    if (!rootOutside.has_value()) {
        return std::unexpected(
            std::move(rootOutside).error().withContext("in a Model's trailing fields"));
    }
    model.rootOutside = *rootOutside;
    auto linked = reader.readI32();
    if (!linked.has_value()) {
        return std::unexpected(
            std::move(linked).error().withContext("in a Model's trailing fields"));
    }
    model.linked = *linked;

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
