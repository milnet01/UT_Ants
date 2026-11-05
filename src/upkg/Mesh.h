// A LodMesh export: a model's vertices, frame by frame, and its faces --
// docs/specs/UTA-0341-mesh-decorations.md SS 4.2.
//
// SCOPE: this reader transcribes, as Geometry.h's do. Which frame is drawn,
// where it stands and which picture each face wears are the baker's
// (src/ubake/Props.h). The LOD tables beyond Faces and Wedges are stepped
// over: only full detail is drawn.
//
// LIFETIME: as Package.h -- nothing here views the package's bytes.

#ifndef UTA_UPKG_MESH_H
#define UTA_UPKG_MESH_H

#include "core/Error.h"
#include "upkg/Geometry.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace uta::upkg {

/// One named run of frames.
struct MeshSequence {
    std::string name; ///< as the name table spells it
    std::int32_t startFrame = 0;
    std::int32_t frameCount = 0;
};

/// A corner of a face: a vertex of the frame and its texture coordinate, each
/// byte 0 to 255 across the picture.
struct MeshWedge {
    std::uint16_t vertex = 0;
    std::uint8_t u = 0;
    std::uint8_t v = 0;
};

struct MeshFace {
    std::array<std::uint16_t, 3> wedges{};
    std::uint16_t material = 0; ///< into LodMesh::materials
};

struct MeshMaterial {
    std::uint32_t polyFlags = 0;
    std::int32_t textureIndex = 0; ///< a skin slot: MultiSkins and Textures
};

struct LodMesh {
    Box bounds; ///< the mesh's own box, in its vertices' units
    std::int32_t frameVerts = 0;
    std::int32_t animFrames = 0;
    /// FrameVerts times AnimFrames points, frame by frame, unpacked from
    /// UT99's 11-11-10 bit fields.
    std::vector<std::array<std::int16_t, 3>> vertices;
    std::vector<MeshSequence> sequences;
    /// Unresolved, as Polygon::texture is: null where the mesh names none.
    std::vector<ObjectReference> textures;
    Vector3 scale;
    Vector3 origin;
    Rotator rotOrigin;
    std::vector<MeshFace> faces;
    std::vector<MeshWedge> wedges;
    std::vector<MeshMaterial> materials;
};

/// SS 4.2: a `LodMesh` export. Refuses another class, an export it does not
/// consume to the last byte, a Verts count that is not FrameVerts times
/// AnimFrames, and a face, wedge or material index outside its table.
[[nodiscard]] Result<LodMesh> readLodMesh(const Package& package, const ExportEntry& entry);

/// UT99's packed mesh vertex: X in bits 0 to 10, Y in 11 to 21, Z in 22 to 31,
/// each signed. Exposed for its test.
[[nodiscard]] std::array<std::int16_t, 3> unpackMeshVertex(std::uint32_t packed) noexcept;

} // namespace uta::upkg

#endif // UTA_UPKG_MESH_H
