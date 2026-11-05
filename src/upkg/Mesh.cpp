// A LodMesh export -- docs/specs/UTA-0341-mesh-decorations.md SS 4.2.
//
// The layout was measured, not recalled: a parser of exactly these fields
// consumed every LodMesh export of Botpack.u, UnrealShare.u and UnrealI.u
// (537) to its last byte (SS 4.2's measurement, 2026-11-05).

#include "upkg/Mesh.h"

#include "upkg/ByteReader.h"
#include "upkg/ReadSupport.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace uta::upkg {
namespace {

constexpr std::string_view OWNER = "a LodMesh";

// Each element's width, for the tables stepped over and the count checks.
constexpr std::size_t PACKED_VERTEX_BYTES = 4;
constexpr std::size_t TRI_BYTES = 20;       // FMeshTri: a Mesh's triangles; empty in a LodMesh
constexpr std::size_t CONNECT_BYTES = 8;    // FMeshVertConnect
constexpr std::size_t LINK_BYTES = 4;       // a VertLinks entry
constexpr std::size_t BOX_BYTES = 25;       // FBox: two vectors and a flag
constexpr std::size_t SPHERE_BYTES = 16;    // FSphere: a vector and a radius
constexpr std::size_t FLOAT_BYTES = 4;
constexpr std::size_t U16_BYTES = 2;
constexpr std::size_t FACE_BYTES = 8;       // three wedge indices and a material
constexpr std::size_t WEDGE_BYTES = 4;      // a vertex index and two UV bytes
constexpr std::size_t MATERIAL_BYTES = 8;   // PolyFlags and a texture index
constexpr std::size_t SEQUENCE_MIN_BYTES = 15; // two names, two int32, a count, a rate
constexpr std::size_t NOTIFY_MIN_BYTES = 5; // a float and a name
constexpr std::size_t REFERENCE_MIN_BYTES = 1;
/// SS 4.2 item 6: the nine 32-bit LOD settings after SpecialFaces.
constexpr std::size_t LOD_SETTINGS_BYTES = 9 * 4;

Result<Vector3> readVector(ByteReader& reader) {
    Vector3 value;
    UTA_TRY(value.x, reader.readFloat());
    UTA_TRY(value.y, reader.readFloat());
    UTA_TRY(value.z, reader.readFloat());
    return value;
}

Result<Box> readBox(ByteReader& reader) {
    Box box;
    UTA_TRY(box.min, readVector(reader));
    UTA_TRY(box.max, readVector(reader));
    UTA_TRY(const std::uint8_t valid, reader.readU8());
    box.valid = valid != 0;
    return box;
}

/// A table's count, checked against what is left before anything is read.
Result<std::int32_t> readCount(ByteReader& reader, std::size_t elementBytes, std::string_view what) {
    UTA_TRY(const std::int32_t count, reader.readIndex());
    UTA_CHECK(detail::checkCount(reader, count, elementBytes, OWNER, what));
    return count;
}

/// A table of fixed-width elements nothing here keeps.
Result<void> skipTable(ByteReader& reader, std::size_t elementBytes, std::string_view what) {
    UTA_TRY(const std::int32_t count, readCount(reader, elementBytes, what));
    return reader.skip(static_cast<std::size_t>(count) * elementBytes);
}

/// A lazy array: an int32 offset of its end in the file, which this reader
/// does not need, then the array.
Result<void> skipLazyOffset(ByteReader& reader) {
    UTA_TRY([[maybe_unused]] const std::int32_t end, reader.readI32());
    return {};
}

Result<std::string> nameAt(const Package& package, ByteReader& reader) {
    UTA_TRY(const std::int32_t raw, reader.readIndex());
    UTA_TRY(const std::uint32_t index, detail::checkNameIndex(raw, package.names().size(), "a LodMesh's names"));
    UTA_TRY(const std::string_view name, package.name(index));
    return std::string{name};
}

Result<void> readSequences(const Package& package, ByteReader& reader, LodMesh& mesh) {
    UTA_TRY(const std::int32_t count, readCount(reader, SEQUENCE_MIN_BYTES, "sequences"));
    mesh.sequences.reserve(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        MeshSequence sequence;
        UTA_TRY(sequence.name, nameAt(package, reader));
        UTA_TRY([[maybe_unused]] const std::string group, nameAt(package, reader));
        UTA_TRY(sequence.startFrame, reader.readI32());
        UTA_TRY(sequence.frameCount, reader.readI32());
        UTA_TRY(const std::int32_t notifies, readCount(reader, NOTIFY_MIN_BYTES, "notifies"));
        for (std::int32_t n = 0; n < notifies; ++n) {
            UTA_TRY([[maybe_unused]] const float time, reader.readFloat());
            UTA_TRY([[maybe_unused]] const std::string function, nameAt(package, reader));
        }
        UTA_TRY([[maybe_unused]] const float rate, reader.readFloat());
        mesh.sequences.push_back(std::move(sequence));
    }
    return {};
}

/// SS 4.2 item 3's Tris, as faces: each triangle's three corners become
/// wedges, one per distinct vertex and texture coordinate, and its PolyFlags
/// and texture slot a material, one per distinct pair, in first-seen order.
/// A LodMesh's Tris is empty; a Mesh draws from it (UTA-0450).
Result<void> readTris(ByteReader& reader, LodMesh& mesh) {
    UTA_TRY(const std::int32_t count, readCount(reader, TRI_BYTES, "triangles"));
    std::map<std::tuple<std::uint16_t, std::uint8_t, std::uint8_t>, std::uint16_t> wedgeOf;
    std::map<std::pair<std::uint32_t, std::int32_t>, std::uint16_t> materialOf;
    const auto index = [](auto& table, const auto& key, auto& into, auto&& value) -> Result<std::uint16_t> {
        if (const auto found = table.find(key); found != table.end()) return found->second;
        if (into.size() > 0xFFFFu) return std::unexpected(detail::malformed("a Mesh holds more corners than a face can name"));
        const auto made = static_cast<std::uint16_t>(into.size());
        into.push_back(value);
        table.emplace(key, made);
        return made;
    };
    for (std::int32_t t = 0; t < count; ++t) {
        std::array<std::uint16_t, 3> vertex{};
        std::array<std::array<std::uint8_t, 2>, 3> uv{};
        for (std::uint16_t& v : vertex) {
            UTA_TRY(v, reader.readU16());
        }
        for (auto& corner : uv)
            for (std::uint8_t& c : corner) {
                UTA_TRY(c, reader.readU8());
            }
        UTA_TRY(const std::uint32_t polyFlags, reader.readU32());
        UTA_TRY(const std::int32_t textureIndex, reader.readI32());
        MeshFace face;
        for (std::size_t k = 0; k < 3; ++k) {
            UTA_TRY(face.wedges[k], index(wedgeOf, std::tuple{vertex[k], uv[k][0], uv[k][1]}, mesh.wedges,
                                          MeshWedge{vertex[k], uv[k][0], uv[k][1]}));
        }
        UTA_TRY(face.material, index(materialOf, std::pair{polyFlags, textureIndex}, mesh.materials,
                                     MeshMaterial{polyFlags, textureIndex}));
        mesh.faces.push_back(face);
    }
    return {};
}

/// SS 4.2 items 1 to 4: UMesh's fields, all of a Mesh.
Result<void> readMeshPart(const Package& package, ByteReader& reader, LodMesh& mesh) {
    UTA_TRY(mesh.bounds, readBox(reader));
    UTA_CHECK(reader.skip(SPHERE_BYTES));

    UTA_CHECK(skipLazyOffset(reader));
    UTA_TRY(const std::int32_t vertexCount, readCount(reader, PACKED_VERTEX_BYTES, "vertices"));
    mesh.vertices.reserve(static_cast<std::size_t>(vertexCount));
    for (std::int32_t i = 0; i < vertexCount; ++i) {
        UTA_TRY(const std::uint32_t packed, reader.readU32());
        mesh.vertices.push_back(unpackMeshVertex(packed));
    }
    UTA_CHECK(skipLazyOffset(reader));
    UTA_CHECK(readTris(reader, mesh));
    UTA_CHECK(readSequences(package, reader, mesh));
    UTA_CHECK(skipLazyOffset(reader));
    UTA_CHECK(skipTable(reader, CONNECT_BYTES, "connects"));
    UTA_CHECK(reader.skip(BOX_BYTES + SPHERE_BYTES));
    UTA_CHECK(skipLazyOffset(reader));
    UTA_CHECK(skipTable(reader, LINK_BYTES, "vertex links"));

    UTA_TRY(const std::int32_t textureCount, readCount(reader, REFERENCE_MIN_BYTES, "textures"));
    for (std::int32_t i = 0; i < textureCount; ++i) {
        UTA_TRY(const std::int32_t raw, reader.readIndex());
        mesh.textures.emplace_back(raw);
    }
    UTA_CHECK(skipTable(reader, BOX_BYTES, "frame boxes"));
    UTA_CHECK(skipTable(reader, SPHERE_BYTES, "frame spheres"));

    UTA_TRY(mesh.frameVerts, reader.readI32());
    UTA_TRY(mesh.animFrames, reader.readI32());
    UTA_TRY([[maybe_unused]] const std::uint32_t andFlags, reader.readU32());
    UTA_TRY([[maybe_unused]] const std::uint32_t orFlags, reader.readU32());
    UTA_TRY(mesh.scale, readVector(reader));
    UTA_TRY(mesh.origin, readVector(reader));
    UTA_TRY(mesh.rotOrigin.pitch, reader.readI32());
    UTA_TRY(mesh.rotOrigin.yaw, reader.readI32());
    UTA_TRY(mesh.rotOrigin.roll, reader.readI32());
    UTA_TRY([[maybe_unused]] const std::int32_t curPoly, reader.readI32());
    UTA_TRY([[maybe_unused]] const std::int32_t curVertex, reader.readI32());
    return skipTable(reader, FLOAT_BYTES, "texture LODs");
}

/// SS 4.2 items 5 and 6: ULodMesh's fields.
Result<void> readLodPart(ByteReader& reader, LodMesh& mesh) {
    UTA_CHECK(skipTable(reader, U16_BYTES, "collapse points"));
    UTA_CHECK(skipTable(reader, U16_BYTES, "face levels"));

    UTA_TRY(const std::int32_t faceCount, readCount(reader, FACE_BYTES, "faces"));
    mesh.faces.reserve(static_cast<std::size_t>(faceCount));
    for (std::int32_t i = 0; i < faceCount; ++i) {
        MeshFace face;
        for (std::uint16_t& wedge : face.wedges) {
            UTA_TRY(wedge, reader.readU16());
        }
        UTA_TRY(face.material, reader.readU16());
        mesh.faces.push_back(face);
    }
    UTA_CHECK(skipTable(reader, U16_BYTES, "collapse wedges"));

    UTA_TRY(const std::int32_t wedgeCount, readCount(reader, WEDGE_BYTES, "wedges"));
    mesh.wedges.reserve(static_cast<std::size_t>(wedgeCount));
    for (std::int32_t i = 0; i < wedgeCount; ++i) {
        MeshWedge wedge;
        UTA_TRY(wedge.vertex, reader.readU16());
        UTA_TRY(wedge.u, reader.readU8());
        UTA_TRY(wedge.v, reader.readU8());
        mesh.wedges.push_back(wedge);
    }

    UTA_TRY(const std::int32_t materialCount, readCount(reader, MATERIAL_BYTES, "materials"));
    mesh.materials.reserve(static_cast<std::size_t>(materialCount));
    for (std::int32_t i = 0; i < materialCount; ++i) {
        MeshMaterial material;
        UTA_TRY(material.polyFlags, reader.readU32());
        UTA_TRY(material.textureIndex, reader.readI32());
        mesh.materials.push_back(material);
    }
    UTA_CHECK(skipTable(reader, FACE_BYTES, "special faces"));
    UTA_CHECK(reader.skip(LOD_SETTINGS_BYTES));
    return skipTable(reader, U16_BYTES, "remap anchors");
}

/// The tables index one another; a reader hands on no index it has not checked.
Result<void> checkIndices(const LodMesh& mesh) {
    if (mesh.frameVerts < 0 || mesh.animFrames < 0
        || static_cast<std::int64_t>(mesh.frameVerts) * mesh.animFrames != static_cast<std::int64_t>(mesh.vertices.size()))
        return std::unexpected(detail::malformed(
            "a LodMesh holds " + std::to_string(mesh.vertices.size()) + " vertices, not FrameVerts "
            + std::to_string(mesh.frameVerts) + " times AnimFrames " + std::to_string(mesh.animFrames)));
    for (const MeshFace& face : mesh.faces) {
        for (const std::uint16_t wedge : face.wedges)
            if (wedge >= mesh.wedges.size())
                return std::unexpected(detail::malformed("a LodMesh face names wedge " + std::to_string(wedge)
                                                         + " of " + std::to_string(mesh.wedges.size())));
        if (face.material >= mesh.materials.size())
            return std::unexpected(detail::malformed("a LodMesh face names material "
                                                     + std::to_string(face.material) + " of "
                                                     + std::to_string(mesh.materials.size())));
    }
    for (const MeshWedge& wedge : mesh.wedges)
        if (std::cmp_greater_equal(wedge.vertex, mesh.frameVerts))
            return std::unexpected(detail::malformed("a LodMesh wedge names vertex " + std::to_string(wedge.vertex)
                                                     + " of a frame's " + std::to_string(mesh.frameVerts)));
    return {};
}

} // namespace

std::array<std::int16_t, 3> unpackMeshVertex(std::uint32_t packed) noexcept {
    // Shift each field to the top of a signed word, then back: the shift
    // carries its sign bit down.
    const auto field = [packed](unsigned low, unsigned bits) {
        const auto top = static_cast<std::int32_t>(packed << (32 - low - bits));
        return static_cast<std::int16_t>(top >> (32 - bits));
    };
    return {field(0, 11), field(11, 11), field(22, 10)};
}

Result<LodMesh> readLodMesh(const Package& package, const ExportEntry& entry) {
    UTA_TRY(const std::string_view className, package.objectName(entry.objectClass));
    const bool lod = className == "LodMesh";
    if (!lod && className != "Mesh")
        return std::unexpected(detail::malformed("a " + std::string{className} + " export is not a mesh"));
    UTA_TRY(const PropertyList list, readPropertyList(package, entry));
    UTA_TRY(const std::span<const std::byte> data, package.serialBytes(entry));
    ByteReader reader{data, list.nativeOffset};

    LodMesh mesh;
    UTA_CHECK(readMeshPart(package, reader, mesh));
    // UTA-0450: a Mesh ends where a LodMesh's own tables begin.
    if (lod) UTA_CHECK(readLodPart(reader, mesh));
    UTA_CHECK(detail::expectConsumed(reader, OWNER));
    UTA_CHECK(checkIndices(mesh));
    return mesh;
}

} // namespace uta::upkg
