// The level's shape, as the file stores it.
//
// docs/specs/UTA-0004-typed-level-content.md SS 4.4 and SS 4.5 for Polys,
// and docs/specs/UTA-0069-model-bsp-tables.md SS 4.4 and SS 4.5 for Model.
//
// SCOPE: this reader transcribes and does nothing else -- no BSP walk, no
// triangulation, no winding repair. SS 3.1 records that as the user's choice
// and why: turning tables into triangles is a design decision, it belongs
// where the bundle's shape is decided (`ubake`), and a reader that only
// transcribes can be checked against the file byte for byte, which SS 4.3
// turns into this item's whole acceptance test.
//
// LIFETIME: as Package.h -- these types hold no view into the package, but
// they are read FROM one, so the caller's bytes must outlive the read call.

#ifndef UTA_UPKG_GEOMETRY_H
#define UTA_UPKG_GEOMETRY_H

#include "core/Error.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"

#include <array>
#include <cstdint>
#include <vector>

namespace uta::upkg {

/// One brush polygon: its vertices, the texture it wears, and its flags.
///
/// `polyFlags` is what ROADMAP UTA-0004 calls "free information about glass,
/// water, sky and lava" -- it arrives here already paired with the texture it
/// applies to, which is why this is the surface most callers want.
struct Polygon {
    std::vector<Vector3> vertices;
    Vector3 base;
    Vector3 normal;
    Vector3 textureU;
    Vector3 textureV;
    std::uint32_t polyFlags = 0;
    /// Unresolved on purpose (SS 4.1): Package::objectName returns a view
    /// whose lifetime is the Package's, so resolving here would hand every
    /// caller a view it did not ask for and cannot outlive.
    ObjectReference actor;
    ObjectReference texture;
    std::uint32_t itemName = 0; // name index
    std::int32_t link = 0;
    std::int32_t brushPoly = 0;
    std::int16_t panU = 0;
    std::int16_t panV = 0;
};

struct Polys {
    std::vector<Polygon> polygons;
};

/// A `Polys` export: the brush polygons, with their textures and flags.
[[nodiscard]] Result<Polys> readPolys(const Package& package, const ExportEntry& entry);

/// A plane, as `FPlane` stores it: a normal and its distance from the origin.
///
/// Declared here rather than inferred: UTA-0069 SS 3.2 writes the fields out
/// because "the same shape" is not a spelling, and two builders would not
/// pick the same one.
struct Plane {
    Vector3 normal;
    float w = 0;
};

/// An axis-aligned box, as `FBox` stores it. `valid` is the file's own flag.
///
/// `Model`'s own bounding box keeps the flattened members UTA-0004 SS 4.1
/// declared; only the `bounds` array takes this struct (UTA-0069 SS 3.2).
struct Box {
    Vector3 min, max;
    bool valid = false;
};

/// One BSP node: the plane it splits on, and its indices into the model's
/// other tables. UTA-0069 SS 4.5.
///
/// Every `i`-prefixed field is the file's own index and is returned
/// unresolved (SS 3.2): resolving one would hand the caller a view whose
/// lifetime it did not ask for, and would make this reader's output
/// impossible to check byte for byte against the file.
struct BspNode {
    Plane plane;
    std::uint64_t zoneMask = 0;
    std::uint8_t nodeFlags = 0;
    std::int32_t iVertPool = 0;
    std::int32_t iSurf = 0;
    std::int32_t iFront = 0;
    std::int32_t iBack = 0;
    std::int32_t iPlane = 0;
    std::int32_t iCollisionBound = 0;
    std::int32_t iRenderBound = 0;
    /// The zone on each side of the plane.
    std::array<std::uint8_t, 2> iZone{};
    std::uint8_t numVertices = 0;
    /// Raw 32-bit, not compact indices -- UTA-0069 SS 4.5 settled this by
    /// measurement, and the two readings are not distinguishable by eye.
    std::array<std::int32_t, 2> iLeaf{};
};

/// One BSP surface: the texture it wears, its flags, and the vectors and
/// lightmap it points at. UTA-0069 SS 4.5.
struct BspSurf {
    ObjectReference texture;
    std::uint32_t polyFlags = 0;
    std::int32_t pBase = 0;
    std::int32_t vNormal = 0;
    std::int32_t vTextureU = 0;
    std::int32_t vTextureV = 0;
    std::int32_t iLightMap = 0;
    std::int32_t iBrushPoly = 0;
    std::int16_t panU = 0;
    std::int16_t panV = 0;
    ObjectReference actor;
};

/// One entry in a node's vertex pool: the point it uses, and the side it
/// shares. UTA-0069 SS 4.5.
struct Vert {
    std::int32_t pVertex = 0;
    std::int32_t iSide = 0;
};

/// One zone: the actor that describes it, and its two masks.
///
/// `connectivity` is what identified the field order (UTA-0069 SS 4.5): down
/// consecutive records its set bit tracks the record's own ordinal, which
/// fixes `zoneActor` as the field before it. UTA-0007 partitions a level on
/// these, so both the member and its field names are a contract (SS 11).
struct ZoneProperties {
    ObjectReference zoneActor;
    std::int64_t connectivity = 0;
    std::int64_t visibility = 0;
};

/// Where a surface's lightmap sits, and how it is projected.
///
/// This reader returns the table's indices and offsets; what the lightmap
/// DATA means is a renderer question and out of scope (UTA-0069 SS 9).
/// The members are in the order the file writes them, which is not the order
/// SS 4.5 states. That section has the compact fields the wrong way round:
/// `dataOffset` and `iLightActors` are raw `i32`, and the two clamps are the
/// compact ones. UTA-0069's ROADMAP bullet carries the derivation.
struct LightMapIndex {
    std::int32_t dataOffset = 0; // an offset INTO lightBits, not a compact index
    Vector3 pan;
    std::int32_t uClamp = 0; // compact, so an element is thirty bytes or more
    std::int32_t vClamp = 0;
    float uScale = 0;
    float vScale = 0;
    std::int32_t iLightActors = 0; // -1 where the surface is lit by nothing
};

/// One BSP leaf. Three compact indices and a sixty-four-bit zone mask.
///
/// SS 4.6 withheld this layout and SS 4.1 had the reader refuse a populated
/// table rather than state one nothing had verified. It is derived now: this
/// shape is the SOLE fit for all 842 exports in the reference install that
/// populate the table, where every permutation of the same four fields fits
/// none of them, and `iZone` indexes the export's own zone table on all
/// 2784273 leaves. UTA-0069's ROADMAP bullet carries the derivation.
struct Leaf {
    std::int32_t iZone = 0;       // indexes the Model's own zones
    std::int32_t iPermeating = 0;
    std::int32_t iVolumetric = 0;
    std::uint64_t visibleZones = 0;
};

/// A `Model` export: the BSP tables, in the file's own order and indexing.
///
/// The scalar fields are UTA-0004 SS 4.1's, unchanged. The tables are the
/// members that item said the implementation would add; UTA-0069 SS 4.4 and
/// SS 4.5 derive them, and UTA-0007 binds to these names (SS 3.2).
///
/// `leaves` IS a member from 2026-09-08. SS 4.1 kept it out while SS 4.6 had
/// not derived the layout, on the ground that returning it would mean either
/// stating a layout nothing had verified or shipping an empty element struct
/// that reads as finished. Neither applies now -- see `Leaf` -- and that
/// section reserved the name against this, so adding it is not a rename.
struct Model {
    Vector3 boundsMin, boundsMax;       // FBox
    bool boundsValid = false;
    Vector3 sphereCentre;
    float sphereRadius = 0;
    std::vector<Vector3> vectors;
    std::vector<Vector3> points;
    std::vector<BspNode> nodes;
    std::vector<BspSurf> surfs;
    std::vector<Vert> verts;
    std::int32_t numSharedSides = 0;
    std::vector<ZoneProperties> zones;
    ObjectReference polys;
    std::vector<LightMapIndex> lightMap;
    std::vector<std::uint8_t> lightBits;
    std::vector<Box> bounds;
    std::vector<std::int32_t> leafHulls;
    std::vector<Leaf> leaves;
    std::vector<ObjectReference> lights;
    std::int32_t rootOutside = 0;
    std::int32_t linked = 0;
};

/// A `Model` export: the BSP tables behind a level's geometry.
[[nodiscard]] Result<Model> readModel(const Package& package, const ExportEntry& entry);

} // namespace uta::upkg

#endif // UTA_UPKG_GEOMETRY_H
