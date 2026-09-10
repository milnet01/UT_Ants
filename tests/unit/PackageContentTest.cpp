// The typed level-content readers, against fixtures.
//
// Locks INV-1 (a reader consumes its export exactly or returns an Error),
// INV-5 (the redundant offset fields are checked, not discarded), INV-6 (the
// second mip chain is read if and only if bHasComp says so), INV-7 (bulk
// payload is a view, never a copy) and INV-8 (an unmodelled class is refused
// by name).
//
// docs/specs/UTA-0004-typed-level-content.md SS 7 tier 1.
//
// `readModel`'s tier-1 cases are here too, and in PackageMalformedTest.cpp:
// docs/specs/UTA-0069-model-bsp-tables.md SS 4.7 names four fixtures for
// INV-1, INV-2 (oversized and negative) and INV-3. SS 4.6's `Leaves` table
// is still underived -- SS 4.7 says a fixture for it waits on that -- so these
// cases populate every table SS 4.4 and SS 4.5 settle and leave `Leaves` at
// its empty, always-legal count of zero.
//
// Two versions run throughout. SS 4.6's WidthOffset and SS 4.8's NextOffset
// exist only from package version 63, and SS 2.1 measured that stock content
// sits on both sides of that line -- so a fixture set at one version leaves a
// branch real packages take exercised by nothing.

#include "support/UnrealPackageBuilder.h"
#include "upkg/Geometry.h"
#include "upkg/Level.h"
#include "upkg/Package.h"
#include "upkg/Sound.h"
#include "upkg/Texture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

using uta::ErrorCode;
using uta::test::asBytes;
using uta::test::encodeCompactIndex;
using uta::test::ExportEntry;
using uta::test::ImportEntry;
using uta::test::TaggedPropertyWriter;
using uta::test::UnrealPackageBuilder;
using uta::upkg::Package;

namespace {

constexpr std::int32_t NAME_NONE = 0;
constexpr std::int32_t NAME_OBJECT = 1;
constexpr std::int32_t NAME_CLASS_WORD = 2;
constexpr std::int32_t NAME_ITEM = 3;
constexpr std::int32_t NAME_WAV = 4;
constexpr std::int32_t NAME_HASCOMP = 5;
/// The class name is what the reader dispatches on (SS 3.3 item 4), so it is
/// a fixture parameter rather than a constant.
constexpr std::int32_t NAME_CLASS_NAME = 6;

void appendU8(std::vector<std::uint8_t>& into, std::uint8_t value) {
    into.push_back(value);
}

void appendU16(std::vector<std::uint8_t>& into, std::uint16_t value) {
    into.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    into.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void appendU32(std::vector<std::uint8_t>& into, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        into.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
}

void appendFloat(std::vector<std::uint8_t>& into, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32(into, bits);
}

void appendIndex(std::vector<std::uint8_t>& into, std::int32_t value) {
    const std::vector<std::uint8_t> encoded = encodeCompactIndex(value);
    into.insert(into.end(), encoded.begin(), encoded.end());
}

void appendVector(std::vector<std::uint8_t>& into, float x, float y, float z) {
    appendFloat(into, x);
    appendFloat(into, y);
    appendFloat(into, z);
}

UnrealPackageBuilder packageWithNames(std::uint16_t version, std::string_view className) {
    UnrealPackageBuilder builder;
    builder.setPackageVersion(version)
        .addName("None")
        .addName("TheObject")
        .addName("Class")
        .addName("ItemName")
        .addName("WAV")
        .addName("bHasComp")
        .addName(className);
    return builder;
}

/// A package holding one export of class `className` whose serialised data is
/// `data`. The class arrives as an IMPORT, which is how a real package names a
/// class it does not itself define.
std::vector<std::uint8_t> packageWithObject(std::uint16_t version,
                                            std::string_view className,
                                            const std::vector<std::uint8_t>& data) {
    UnrealPackageBuilder builder = packageWithNames(version, className);

    ImportEntry import;
    import.classPackage = NAME_NONE;
    import.className = NAME_CLASS_WORD;
    import.objectName = NAME_CLASS_NAME;
    builder.addImport(import);

    ExportEntry entry;
    entry.objectName = NAME_OBJECT;
    entry.objectClass = -1; // import 0
    entry.serialData = data;
    builder.addExport(entry);
    return builder.build();
}

/// An empty property list -- just the `None` terminator.
std::vector<std::uint8_t> emptyProperties() {
    return TaggedPropertyWriter{}.build(NAME_NONE);
}

/// One polygon with `vertexCount` vertices, in SS 4.4's layout.
std::vector<std::uint8_t> onePolygon(std::int32_t vertexCount) {
    std::vector<std::uint8_t> body;
    appendIndex(body, vertexCount);
    appendVector(body, 1.0F, 2.0F, 3.0F);  // base
    appendVector(body, 0.0F, 0.0F, 1.0F);  // normal
    appendVector(body, 1.0F, 0.0F, 0.0F);  // textureU
    appendVector(body, 0.0F, 1.0F, 0.0F);  // textureV
    for (std::int32_t index = 0; index < vertexCount; ++index) {
        appendVector(body, static_cast<float>(index), 0.0F, 0.0F);
    }
    appendU32(body, 0x20u); // polyFlags
    appendIndex(body, 0);   // actor: null
    appendIndex(body, 0);   // texture: null
    appendIndex(body, NAME_ITEM);
    appendIndex(body, 7);  // link
    appendIndex(body, 11); // brushPoly
    appendU16(body, static_cast<std::uint16_t>(-3));
    appendU16(body, 5);
    return body;
}

std::vector<std::uint8_t> polysData(std::int32_t polygonCount, std::int32_t vertexCount) {
    std::vector<std::uint8_t> data = emptyProperties();
    appendU32(data, static_cast<std::uint32_t>(polygonCount)); // Num
    appendU32(data, static_cast<std::uint32_t>(polygonCount)); // Max
    for (std::int32_t index = 0; index < polygonCount; ++index) {
        const std::vector<std::uint8_t> polygon = onePolygon(vertexCount);
        data.insert(data.end(), polygon.begin(), polygon.end());
    }
    return data;
}

void appendI64(std::vector<std::uint8_t>& into, std::int64_t value) {
    const auto bits = static_cast<std::uint64_t>(value);
    appendU32(into, static_cast<std::uint32_t>(bits & 0xFFFFFFFFu));
    appendU32(into, static_cast<std::uint32_t>(bits >> 32));
}

/// One `BspNode` in UTA-0069 SS 4.5's layout. `nodeFlags` carries a
/// caller-chosen label -- nothing else in the layout uses that byte -- so
/// INV-3 can tell which node landed at which returned position. Every other
/// field is zero.
std::vector<std::uint8_t> oneBspNode(std::uint8_t nodeFlagsLabel) {
    std::vector<std::uint8_t> body;
    appendVector(body, 0.0F, 0.0F, 1.0F); // plane.normal
    appendFloat(body, 0.0F);              // plane.w
    appendI64(body, 0);                   // zoneMask
    appendU8(body, nodeFlagsLabel);       // nodeFlags -- the label
    appendIndex(body, 0);                 // iVertPool
    appendIndex(body, 0);                 // iSurf
    appendIndex(body, 0);                 // iFront
    appendIndex(body, 0);                 // iBack
    appendIndex(body, 0);                 // iPlane
    appendIndex(body, 0);                 // iCollisionBound
    appendIndex(body, 0);                 // iRenderBound
    appendU8(body, 0);                    // iZone[0]
    appendU8(body, 0);                    // iZone[1]
    appendU8(body, 0);                    // numVertices
    appendU32(body, 0);                   // iLeaf[0] -- raw i32, SS 4.5
    appendU32(body, 0);                   // iLeaf[1]
    return body;
}

/// `count` nodes, labelled 0, 1, 2, ... in file order -- so a reader that
/// drops, reorders or compacts the table disagrees at the first position it
/// touches.
std::vector<std::uint8_t> bspNodes(std::int32_t count) {
    std::vector<std::uint8_t> body;
    for (std::int32_t index = 0; index < count; ++index) {
        const std::vector<std::uint8_t> node = oneBspNode(static_cast<std::uint8_t>(index));
        body.insert(body.end(), node.begin(), node.end());
    }
    return body;
}

/// One `BspSurf` in UTA-0069 SS 4.5's layout. `polyFlagsLabel` is the marker.
std::vector<std::uint8_t> oneBspSurf(std::uint32_t polyFlagsLabel) {
    std::vector<std::uint8_t> body;
    appendIndex(body, 0);          // texture: null
    appendU32(body, polyFlagsLabel); // polyFlags -- the label
    appendIndex(body, 0);          // pBase
    appendIndex(body, 0);          // vNormal
    appendIndex(body, 0);          // vTextureU
    appendIndex(body, 0);          // vTextureV
    appendIndex(body, 0);          // iLightMap
    appendIndex(body, 0);          // iBrushPoly
    appendU16(body, 0);            // panU
    appendU16(body, 0);            // panV
    appendIndex(body, 0);          // actor: null
    return body;
}

/// `count` surfs, labelled 100, 200, 300, ... in file order -- a different
/// label range from `bspNodes` so a test mixing the two up would show it.
std::vector<std::uint8_t> bspSurfs(std::int32_t count) {
    std::vector<std::uint8_t> body;
    for (std::int32_t index = 0; index < count; ++index) {
        const std::vector<std::uint8_t> surf =
            oneBspSurf(static_cast<std::uint32_t>(100 * (index + 1)));
        body.insert(body.end(), surf.begin(), surf.end());
    }
    return body;
}

/// One `ZoneProperties` in UTA-0069 SS 4.5's layout. `connectivity` carries
/// the label -- SS 4.5 identified this very field by its content down
/// consecutive records, so labelling it here exercises the thing that found
/// it, not just its width.
std::vector<std::uint8_t> oneZoneProperties(std::int64_t connectivityLabel) {
    std::vector<std::uint8_t> body;
    appendIndex(body, 0);               // zoneActor: null
    appendI64(body, connectivityLabel); // connectivity -- the label
    appendI64(body, 0);                 // visibility
    return body;
}

/// `count` zone records, labelled 1000, 1001, 1002, ... in file order.
std::vector<std::uint8_t> zoneRecords(std::int32_t count) {
    std::vector<std::uint8_t> body;
    for (std::int32_t index = 0; index < count; ++index) {
        const std::vector<std::uint8_t> zone = oneZoneProperties(1000 + index);
        body.insert(body.end(), zone.begin(), zone.end());
    }
    return body;
}

/// One index-prefixed table: a compact-index count, then the caller's
/// element bytes verbatim.
void appendTable(std::vector<std::uint8_t>& into, std::int32_t count,
                 const std::vector<std::uint8_t>& elements) {
    appendIndex(into, count);
    into.insert(into.end(), elements.begin(), elements.end());
}

/// One `LightMapIndex`, in the order the file writes them. `dataOffset`
/// carries the label; every other field carries a value distinct from its
/// neighbours' so a test can tell a correct placement from a shifted one.
///
/// Two of those values are chosen against a specific wrong reading.
/// `dataOffset` is larger than a one-byte compact index can hold, so reading
/// it as an index misaligns the cursor rather than happening to agree. And
/// `vClamp` is over 63, so it takes two bytes -- the clamps ARE compact, and
/// a lightmap's texel dimensions are usually small enough that a fixed-width
/// read of them agrees by accident.
std::vector<std::uint8_t> oneLightMapIndex(std::int32_t dataOffsetLabel) {
    std::vector<std::uint8_t> body;
    appendU32(body, static_cast<std::uint32_t>(dataOffsetLabel)); // raw i32
    appendVector(body, 1.5F, 2.5F, 3.5F);                        // pan
    appendIndex(body, 7);                                        // uClamp, compact
    appendIndex(body, 100);                                      // vClamp, two bytes
    appendFloat(body, 32.0F);                                    // uScale
    appendFloat(body, 64.0F);                                    // vScale
    appendU32(body, 0xFFFFFFFFu);                                // iLightActors, -1
    return body;
}

/// `count` BSP leaves, labelled 1, 2, 3, ... in `iZone`. `iPermeating` is
/// over 63 so it takes two compact bytes: the three leading fields are
/// compact and the mask is not, and a fixed-width read of the element would
/// otherwise agree with this fixture by accident.
std::vector<std::uint8_t> leafEntries(std::int32_t count) {
    std::vector<std::uint8_t> body;
    for (std::int32_t index = 0; index < count; ++index) {
        appendIndex(body, index + 1);   // iZone -- the label
        appendIndex(body, 200);         // iPermeating, two bytes
        appendIndex(body, -1);          // iVolumetric
        appendU32(body, 0xEFBEADDEu);   // visibleZones, low word
        appendU32(body, 0x0DF0FECAu);   // visibleZones, high word
    }
    return body;
}

/// `count` lightmap entries, labelled 10, 20, 30, ... in file order.
std::vector<std::uint8_t> lightMapEntries(std::int32_t count) {
    std::vector<std::uint8_t> body;
    for (std::int32_t index = 0; index < count; ++index) {
        const std::vector<std::uint8_t> entry = oneLightMapIndex(1000 * (index + 1));
        body.insert(body.end(), entry.begin(), entry.end());
    }
    return body;
}

/// `count` `LightBits` bytes, labelled 0x10, 0x11, 0x12, ... in file order.
/// One byte per element (SS 4.5), so the label is the whole element.
std::vector<std::uint8_t> lightBitsBytes(std::int32_t count) {
    std::vector<std::uint8_t> body;
    for (std::int32_t index = 0; index < count; ++index) {
        appendU8(body, static_cast<std::uint8_t>(0x10 + index));
    }
    return body;
}

/// One `Box` (`FBox`, SS 4.5's shape for `Bounds`). `min.x` carries the
/// label; `max` and `valid` are fixed.
std::vector<std::uint8_t> oneBox(float minXLabel) {
    std::vector<std::uint8_t> body;
    appendVector(body, minXLabel, 0.0F, 0.0F); // min -- the label
    appendVector(body, 1.0F, 1.0F, 1.0F);      // max
    appendU8(body, 1);                          // valid
    return body;
}

/// `count` bounds boxes, labelled 1.0, 2.0, 3.0, ... in file order.
std::vector<std::uint8_t> boundsBoxes(std::int32_t count) {
    std::vector<std::uint8_t> body;
    for (std::int32_t index = 0; index < count; ++index) {
        const std::vector<std::uint8_t> box = oneBox(static_cast<float>(index + 1));
        body.insert(body.end(), box.begin(), box.end());
    }
    return body;
}

/// `count` `LeafHulls` entries, labelled 500, 501, 502, ... in file order.
/// A raw `i32` per element (SS 4.5), so the label is the whole element.
std::vector<std::uint8_t> leafHullEntries(std::int32_t count) {
    std::vector<std::uint8_t> body;
    for (std::int32_t index = 0; index < count; ++index) {
        appendU32(body, static_cast<std::uint32_t>(500 + index));
    }
    return body;
}

/// `count` `Lights` entries -- an `ObjectReference` compact index per
/// element (SS 4.5) -- labelled 7, 8, 9, ... in file order. `readModel`
/// never validates a light reference against the package's own tables, so
/// an arbitrary non-zero raw value is a legitimate label.
std::vector<std::uint8_t> lightsEntries(std::int32_t count) {
    std::vector<std::uint8_t> body;
    for (std::int32_t index = 0; index < count; ++index) {
        appendIndex(body, 7 + index);
    }
    return body;
}

/// A `Model` body in UTA-0069 SS 4.4's order: the 41-byte prefix, `Nodes`
/// and `Surfs` encoded from the caller's bytes, and the zone run and the six
/// trailing tables populated at their own DISTINCT counts. `leavesCount`
/// trails the others because it was added after them: SS 4.1 kept `Leaves`
/// empty in every fixture while SS 4.6 had not derived the element, and it
/// defaults to that so the call sites written under that rule still read as
/// they did.
///
/// `nodeCount` and `surfCount` are the DECLARED counts written into each
/// table's index prefix; `nodes` and `surfs` are the elements' own encoded
/// bytes. Passing a count that disagrees with what `nodes`/`surfs` actually
/// hold is how the malformed cases are built -- SS 4.7's oversized and
/// negative fixtures declare a count and supply no matching bytes at all.
/// The other trailing tables declare their own true count, except where
/// `declaredLightBits` is set: then the lightmap byte table declares that
/// count and holds no bytes, which is how UTA-0096's refusal cases reach its
/// one-run read.
std::vector<std::uint8_t> modelData(std::int32_t nodeCount, const std::vector<std::uint8_t>& nodes,
                                    std::int32_t surfCount, const std::vector<std::uint8_t>& surfs,
                                    std::int32_t zoneCount, std::int32_t lightMapCount,
                                    std::int32_t lightBitsCount, std::int32_t boundsCount,
                                    std::int32_t leafHullsCount, std::int32_t lightsCount,
                                    std::int32_t leavesCount = 0,
                                    std::optional<std::int32_t> declaredLightBits = std::nullopt) {
    std::vector<std::uint8_t> data = emptyProperties();
    appendVector(data, -1.0F, -2.0F, -3.0F); // BoundingBox.min
    appendVector(data, 1.0F, 2.0F, 3.0F);    // BoundingBox.max
    appendU8(data, 1);                       // BoundingBox.valid
    appendVector(data, 0.0F, 0.0F, 0.0F);    // BoundingSphere centre
    appendFloat(data, 5.0F);                 // BoundingSphere radius
    appendIndex(data, 0);                    // Vectors: empty
    appendIndex(data, 0);                    // Points: empty
    appendIndex(data, nodeCount);
    data.insert(data.end(), nodes.begin(), nodes.end());
    appendIndex(data, surfCount);
    data.insert(data.end(), surfs.begin(), surfs.end());
    appendIndex(data, 0); // Verts: empty
    appendU32(data, 3u);  // NumSharedSides -- a raw i32, not a table
    // NumZones is a raw i32 too (SS 4.4), not a compact-index table count --
    // the field the community order gets wrong.
    appendU32(data, static_cast<std::uint32_t>(zoneCount));
    const std::vector<std::uint8_t> zones = zoneRecords(zoneCount);
    data.insert(data.end(), zones.begin(), zones.end());
    appendIndex(data, 0); // Polys: null
    appendTable(data, lightMapCount, lightMapEntries(lightMapCount));
    if (declaredLightBits.has_value()) {
        appendIndex(data, *declaredLightBits); // the count alone, no bytes
    } else {
        appendTable(data, lightBitsCount, lightBitsBytes(lightBitsCount));
    }
    appendTable(data, boundsCount, boundsBoxes(boundsCount));
    appendTable(data, leafHullsCount, leafHullEntries(leafHullsCount));
    appendTable(data, leavesCount, leafEntries(leavesCount));
    appendTable(data, lightsCount, lightsEntries(lightsCount));
    appendU32(data, 1u); // RootOutside
    appendU32(data, 0u); // Linked
    return data;
}

/// One mip. `widthOffset` is written verbatim so a test can make it disagree;
/// a value of 0 with version >= 63 means "fill in the correct one later".
void appendMip(std::vector<std::uint8_t>& into, std::uint16_t version,
               std::uint32_t widthOffset, const std::vector<std::uint8_t>& pixels,
               std::uint32_t width, std::uint32_t height) {
    if (version >= 63) {
        appendU32(into, widthOffset);
    }
    appendIndex(into, static_cast<std::int32_t>(pixels.size()));
    into.insert(into.end(), pixels.begin(), pixels.end());
    appendU32(into, width);
    appendU32(into, height);
    appendU8(into, 0);
    appendU8(into, 0);
}

/// The offset at which the first export's serialised bytes begin. The builder
/// lays a package out as header, then each export's bytes, then the tables --
/// so this is recovered by opening the package rather than assumed.
std::size_t firstExportOffset(const std::vector<std::uint8_t>& bytes) {
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());
    REQUIRE(package->exports().size() == 1);
    return package->exports()[0].serialOffset;
}

} // namespace

// --- Polys ------------------------------------------------------------------

TEST_CASE("a Polys export reads back the polygons that were written", "[upkg]") {
    const std::vector<std::uint8_t> bytes =
        packageWithObject(68, "Polys", polysData(2, 4));
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto polys = uta::upkg::readPolys(*package, package->exports()[0]);
    REQUIRE(polys.has_value());
    REQUIRE(polys->polygons.size() == 2);

    const uta::upkg::Polygon& first = polys->polygons[0];
    CHECK(first.vertices.size() == 4);
    CHECK(first.polyFlags == 0x20u);
    CHECK(first.itemName == static_cast<std::uint32_t>(NAME_ITEM));
    CHECK(first.link == 7);
    CHECK(first.brushPoly == 11);
    // Signed, and narrower than the compact index everything around it uses --
    // a reader that sign-extended from a wider read gets this wrong.
    CHECK(first.panU == -3);
    CHECK(first.panV == 5);
    CHECK(first.normal.z == 3.0F / 3.0F);
}

TEST_CASE("a Polys export with bytes left over is refused", "[upkg]") {
    // INV-1: the layout is right only when the reader ends exactly at the
    // export's end. One trailing byte is the smallest possible disagreement.
    std::vector<std::uint8_t> data = polysData(1, 3);
    data.push_back(0x00u);
    const std::vector<std::uint8_t> bytes = packageWithObject(68, "Polys", data);
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto polys = uta::upkg::readPolys(*package, package->exports()[0]);
    REQUIRE_FALSE(polys.has_value());
    CHECK(polys.error().code() == ErrorCode::MalformedData);
    CHECK(polys.error().message().find("unread") != std::string_view::npos);
}

TEST_CASE("a Polys declaring more polygons than the export can hold is refused",
          "[upkg]") {
    // INV-4: the count is checked against the bytes present before anything is
    // reserved from it.
    std::vector<std::uint8_t> data = emptyProperties();
    appendU32(data, 0x00FFFFFFu);
    appendU32(data, 0x00FFFFFFu);
    const std::vector<std::uint8_t> bytes = packageWithObject(68, "Polys", data);
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto polys = uta::upkg::readPolys(*package, package->exports()[0]);
    REQUIRE_FALSE(polys.has_value());
    CHECK(polys.error().code() == ErrorCode::MalformedData);
    CHECK(polys.error().message().find("more than the") != std::string_view::npos);
}

// --- Model --------------------------------------------------------------------
//
// docs/specs/UTA-0069-model-bsp-tables.md SS 4.7 tier 1. INV-2's malformed
// cases live in PackageMalformedTest.cpp beside Polys' and Palette's, which
// they follow in shape.

TEST_CASE("a Model export reads back its tables at known counts", "[upkg]") {
    // UTA-0069 INV-1: the fixture's length is exactly what the builder wrote,
    // and every table SS 4.4 and SS 4.5 settle sits where SS 4.4 places it --
    // a wrong width anywhere upstream would leave bytes unread or run the
    // cursor off the end before it gets here. The zone run and the five
    // trailing tables SS 4.5 settles are populated at their own distinct
    // counts, so a wrong width or a swap among THEM is no longer
    // byte-identical to this fixture either. `Leaves` joined them on
    // 2026-09-08, when SS 4.6's element was derived.
    const std::vector<std::uint8_t> bytes = packageWithObject(
        68, "Model", modelData(2, bspNodes(2), 1, bspSurfs(1),
                               /*zoneCount=*/2, /*lightMapCount=*/3, /*lightBitsCount=*/4,
                               /*boundsCount=*/5, /*leafHullsCount=*/6, /*lightsCount=*/7,
                               /*leavesCount=*/8));
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto model = uta::upkg::readModel(*package, package->exports()[0]);
    REQUIRE(model.has_value());
    CHECK(model->nodes.size() == 2);
    CHECK(model->surfs.size() == 1);
    CHECK(model->verts.empty());
    CHECK(model->numSharedSides == 3);
    CHECK(model->polys.raw() == 0);
    CHECK(model->rootOutside == 1);
    CHECK(model->linked == 0);
    CHECK(model->boundsMin.x == -1.0F);
    CHECK(model->boundsMax.z == 3.0F);
    CHECK(model->boundsValid);
    CHECK(model->sphereRadius == 5.0F);

    REQUIRE(model->zones.size() == 2);
    CHECK(model->zones[0].connectivity == 1000);
    CHECK(model->zones[1].connectivity == 1001);

    REQUIRE(model->lightMap.size() == 3);
    CHECK(model->lightMap[0].dataOffset == 1000);
    CHECK(model->lightMap[1].dataOffset == 2000);
    CHECK(model->lightMap[2].dataOffset == 3000);
    // Every field of one element, so the check is of the LAYOUT rather than
    // of the element's width: a placement shifted by any number of bytes
    // still consumes thirty and still reaches the end of the export.
    CHECK(model->lightMap[0].pan.x == 1.5F);
    CHECK(model->lightMap[0].pan.y == 2.5F);
    CHECK(model->lightMap[0].pan.z == 3.5F);
    CHECK(model->lightMap[0].uClamp == 7);
    CHECK(model->lightMap[0].vClamp == 100);
    CHECK(model->lightMap[0].uScale == 32.0F);
    CHECK(model->lightMap[0].vScale == 64.0F);
    CHECK(model->lightMap[0].iLightActors == -1);

    REQUIRE(model->lightBits.size() == 4);
    CHECK(model->lightBits[0] == 0x10u);
    CHECK(model->lightBits[3] == 0x13u);

    // Every field of one leaf, for the reason the lightmap element gets the
    // same treatment: a placement shifted within the element still consumes
    // the element and still reaches the end of the export.
    REQUIRE(model->leaves.size() == 8);
    CHECK(model->leaves[0].iZone == 1);
    CHECK(model->leaves[7].iZone == 8);
    CHECK(model->leaves[0].iPermeating == 200);
    CHECK(model->leaves[0].iVolumetric == -1);
    CHECK(model->leaves[0].visibleZones == 0x0DF0FECAEFBEADDEuLL);

    REQUIRE(model->bounds.size() == 5);
    CHECK(model->bounds[0].min.x == 1.0F);
    CHECK(model->bounds[4].min.x == 5.0F);

    REQUIRE(model->leafHulls.size() == 6);
    CHECK(model->leafHulls[0] == 500);
    CHECK(model->leafHulls[5] == 505);

    REQUIRE(model->lights.size() == 7);
    CHECK(model->lights[0].raw() == 7);
    CHECK(model->lights[6].raw() == 13);
}

TEST_CASE("a Model's element tables land at the file's own indices", "[upkg]") {
    // UTA-0069 INV-3. Known counts alone (the case above) cannot tell a
    // reader that compacts or reorders a table from one that does not; these
    // labels are distinct per position in every table SS 4.5 settles, so a
    // swap, a drop or a renumbering shows up at the position it happens --
    // not just in Nodes and Surfs, but in the zone run and every one of the
    // five trailing tables too.
    const std::vector<std::uint8_t> bytes = packageWithObject(
        68, "Model", modelData(3, bspNodes(3), 2, bspSurfs(2),
                               /*zoneCount=*/2, /*lightMapCount=*/3, /*lightBitsCount=*/4,
                               /*boundsCount=*/5, /*leafHullsCount=*/6, /*lightsCount=*/7));
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto model = uta::upkg::readModel(*package, package->exports()[0]);
    REQUIRE(model.has_value());

    REQUIRE(model->nodes.size() == 3);
    CHECK(model->nodes[0].nodeFlags == 0);
    CHECK(model->nodes[1].nodeFlags == 1);
    CHECK(model->nodes[2].nodeFlags == 2);

    REQUIRE(model->surfs.size() == 2);
    CHECK(model->surfs[0].polyFlags == 100u);
    CHECK(model->surfs[1].polyFlags == 200u);

    // The three SS 4.7 names explicitly: `connectivity` is the field that
    // identified ZoneProperties' layout in the first place (SS 4.5).
    REQUIRE(model->zones.size() == 2);
    CHECK(model->zones[0].connectivity == 1000);
    CHECK(model->zones[1].connectivity == 1001);

    REQUIRE(model->leafHulls.size() == 6);
    for (std::size_t index = 0; index < model->leafHulls.size(); ++index) {
        CHECK(model->leafHulls[index] == 500 + static_cast<std::int32_t>(index));
    }

    REQUIRE(model->lightBits.size() == 4);
    for (std::size_t index = 0; index < model->lightBits.size(); ++index) {
        CHECK(model->lightBits[index] ==
              static_cast<std::uint8_t>(0x10 + index));
    }

    // Not named by SS 4.7 as required, but labelled anyway (modelData gives
    // every settled table a per-index value) -- closing the same gap for
    // LightMap, Bounds and Lights costs nothing extra here.
    REQUIRE(model->lightMap.size() == 3);
    for (std::size_t index = 0; index < model->lightMap.size(); ++index) {
        CHECK(model->lightMap[index].dataOffset ==
              1000 * (static_cast<std::int32_t>(index) + 1));
    }

    REQUIRE(model->bounds.size() == 5);
    for (std::size_t index = 0; index < model->bounds.size(); ++index) {
        CHECK(model->bounds[index].min.x == static_cast<float>(index + 1));
    }

    REQUIRE(model->lights.size() == 7);
    for (std::size_t index = 0; index < model->lights.size(); ++index) {
        CHECK(model->lights[index].raw() == 7 + static_cast<std::int32_t>(index));
    }
}

TEST_CASE("a Model export with bytes left over is refused", "[upkg]") {
    // UTA-0069 INV-1, the other half of the "known counts" case above: the
    // layout is right only when the reader ends EXACTLY at the export's end.
    // One trailing byte is the smallest possible disagreement -- the same
    // shape as "a Polys export with bytes left over is refused" above, and
    // the case that proves the exact-consumption check fires at all: without
    // it, every Model fixture in this file ends where the reader expects and
    // the check is never exercised.
    std::vector<std::uint8_t> data =
        modelData(1, bspNodes(1), 0, {}, /*zoneCount=*/0, /*lightMapCount=*/0,
                  /*lightBitsCount=*/0, /*boundsCount=*/0, /*leafHullsCount=*/0,
                  /*lightsCount=*/0);
    data.push_back(0x00u);
    const std::vector<std::uint8_t> bytes = packageWithObject(68, "Model", data);
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto model = uta::upkg::readModel(*package, package->exports()[0]);
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().code() == ErrorCode::MalformedData);
    CHECK(model.error().message().find("unread") != std::string_view::npos);
}

TEST_CASE("a Model declaring more lightmap bytes than the export holds is refused", "[upkg]") {
    // UTA-0096 reads this table as one run, so its count bound is checked here
    // rather than inherited from readTable. The mirror of PackageMalformedTest's
    // oversized-nodes case: checkCount's own wording, and this table's name.
    const std::vector<std::uint8_t> data =
        modelData(1, bspNodes(1), 0, {}, /*zoneCount=*/0, /*lightMapCount=*/0,
                  /*lightBitsCount=*/0, /*boundsCount=*/0, /*leafHullsCount=*/0,
                  /*lightsCount=*/0, /*leavesCount=*/0, /*declaredLightBits=*/0x00FFFFFF);
    const std::vector<std::uint8_t> bytes = packageWithObject(68, "Model", data);
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto model = uta::upkg::readModel(*package, package->exports()[0]);
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().code() == ErrorCode::MalformedData);
    CHECK(model.error().message().find("more than the") != std::string_view::npos);
    CHECK(model.error().message().find("lightmap bytes") != std::string_view::npos);
}

TEST_CASE("a Model declaring a negative lightmap byte count is refused", "[upkg]") {
    // An EXACT match, for PackageMalformedTest's negative-nodes reason: without
    // the count<0 check, -1 casts to SIZE_MAX and readBytes refuses it with a
    // different message. So this is what grades that check -- measured, a
    // mutation deleting it survived every test before this one existed.
    const std::vector<std::uint8_t> data =
        modelData(1, bspNodes(1), 0, {}, /*zoneCount=*/0, /*lightMapCount=*/0,
                  /*lightBitsCount=*/0, /*boundsCount=*/0, /*leafHullsCount=*/0,
                  /*lightsCount=*/0, /*leavesCount=*/0, /*declaredLightBits=*/-1);
    const std::vector<std::uint8_t> bytes = packageWithObject(68, "Model", data);
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto model = uta::upkg::readModel(*package, package->exports()[0]);
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().code() == ErrorCode::MalformedData);
    CHECK(model.error().message() == "a Model declares -1 lightmap bytes");
}

// --- Palette ----------------------------------------------------------------

TEST_CASE("a Palette export reads back its colours", "[upkg]") {
    std::vector<std::uint8_t> data = emptyProperties();
    appendIndex(data, 2);
    for (const std::uint8_t component : {1, 2, 3, 4, 5, 6, 7, 8}) {
        appendU8(data, component);
    }
    const std::vector<std::uint8_t> bytes = packageWithObject(68, "Palette", data);
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto palette = uta::upkg::readPalette(*package, package->exports()[0]);
    REQUIRE(palette.has_value());
    REQUIRE(palette->entries.size() == 2);
    CHECK(palette->entries[0].r == 1);
    CHECK(palette->entries[0].a == 4);
    CHECK(palette->entries[1].r == 5);
    CHECK(palette->entries[1].a == 8);
}

TEST_CASE("a Palette declaring a count larger than the file is refused", "[upkg]") {
    // INV-4, and the case UTA-0003's INV-2 is graded on: a four-byte edit
    // asking for gigabytes must be refused before anything is reserved.
    std::vector<std::uint8_t> data = emptyProperties();
    appendIndex(data, 0x00FFFFFF);
    const std::vector<std::uint8_t> bytes = packageWithObject(68, "Palette", data);
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto palette = uta::upkg::readPalette(*package, package->exports()[0]);
    REQUIRE_FALSE(palette.has_value());
    CHECK(palette.error().code() == ErrorCode::MalformedData);
    CHECK(palette.error().message().find("more than the") != std::string_view::npos);
}

// --- Texture ----------------------------------------------------------------

TEST_CASE("a version 61 texture has no WidthOffset field", "[upkg]") {
    // SS 2.1: stock content sits on both sides of the version 63 line, so the
    // older branch is exercised by real packages and must be exercised here.
    std::vector<std::uint8_t> data = emptyProperties();
    appendU8(data, 1);
    appendMip(data, 61, 0, {0xAAu, 0xBBu, 0xCCu, 0xDDu}, 2, 2);
    const std::vector<std::uint8_t> bytes = packageWithObject(61, "Texture", data);
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto texture = uta::upkg::readTexture(*package, package->exports()[0]);
    REQUIRE(texture.has_value());
    REQUIRE(texture->mips.size() == 1);
    CHECK(texture->mips[0].width == 2);
    CHECK(texture->mips[0].pixels.size() == 4);
    CHECK(texture->compressedMips.empty());
}

TEST_CASE("a version 68 texture's WidthOffset must name the end of its data",
          "[upkg]") {
    // INV-5. The offset is redundant, which is exactly why a reader is tempted
    // to read it and throw it away; checking it costs nothing and catches a
    // desynchronised chain.
    const std::vector<std::uint8_t> pixels{0x11u, 0x22u, 0x33u, 0x44u};

    // Two passes: the correct WidthOffset is an offset into the whole file, so
    // it cannot be known until the export's own offset is.
    std::vector<std::uint8_t> probe = emptyProperties();
    appendU8(probe, 1);
    appendMip(probe, 68, 0, pixels, 2, 2);
    const std::size_t serialOffset =
        firstExportOffset(packageWithObject(68, "Texture", probe));
    // Within the export: the property list, the mip count, the WidthOffset
    // field itself, and the size byte -- then the pixels.
    const auto correct = static_cast<std::uint32_t>(
        serialOffset + emptyProperties().size() + 1 + 4 + 1 + pixels.size());

    SECTION("the correct offset is accepted") {
        std::vector<std::uint8_t> data = emptyProperties();
        appendU8(data, 1);
        appendMip(data, 68, correct, pixels, 2, 2);
        const std::vector<std::uint8_t> bytes = packageWithObject(68, "Texture", data);
        const auto package = Package::open(asBytes(bytes));
        REQUIRE(package.has_value());
        const auto texture = uta::upkg::readTexture(*package, package->exports()[0]);
        REQUIRE(texture.has_value());
        CHECK(texture->mips.size() == 1);
    }

    SECTION("an offset one byte out is refused") {
        std::vector<std::uint8_t> data = emptyProperties();
        appendU8(data, 1);
        appendMip(data, 68, correct + 1, pixels, 2, 2);
        const std::vector<std::uint8_t> bytes = packageWithObject(68, "Texture", data);
        const auto package = Package::open(asBytes(bytes));
        REQUIRE(package.has_value());
        const auto texture = uta::upkg::readTexture(*package, package->exports()[0]);
        REQUIRE_FALSE(texture.has_value());
        CHECK(texture.error().code() == ErrorCode::MalformedData);
        CHECK(texture.error().message().find("WidthOffset") != std::string_view::npos);
    }
}

TEST_CASE("a texture's second chain is read if and only if bHasComp says so",
          "[upkg]") {
    // INV-6, and both directions matter: a reader that looks for a second
    // chain by trying to read one and seeing whether bytes remain succeeds by
    // accident on a texture that has none.
    const std::vector<std::uint8_t> pixels{0x01u, 0x02u, 0x03u, 0x04u};

    SECTION("bHasComp false: one chain, and compressedMips is empty") {
        TaggedPropertyWriter writer;
        writer.addBool(NAME_HASCOMP, false);
        std::vector<std::uint8_t> data = writer.build(NAME_NONE);
        appendU8(data, 1);
        appendMip(data, 61, 0, pixels, 2, 2);
        const std::vector<std::uint8_t> bytes = packageWithObject(61, "Texture", data);
        const auto package = Package::open(asBytes(bytes));
        REQUIRE(package.has_value());

        const auto texture = uta::upkg::readTexture(*package, package->exports()[0]);
        REQUIRE(texture.has_value());
        CHECK(texture->mips.size() == 1);
        CHECK(texture->compressedMips.empty());
    }

    SECTION("bHasComp true: both chains, and both are read") {
        TaggedPropertyWriter writer;
        writer.addBool(NAME_HASCOMP, true);
        std::vector<std::uint8_t> data = writer.build(NAME_NONE);
        appendU8(data, 1);
        appendMip(data, 61, 0, pixels, 2, 2);
        appendU8(data, 2);
        appendMip(data, 61, 0, pixels, 2, 2);
        appendMip(data, 61, 0, pixels, 1, 1);
        const std::vector<std::uint8_t> bytes = packageWithObject(61, "Texture", data);
        const auto package = Package::open(asBytes(bytes));
        REQUIRE(package.has_value());

        const auto texture = uta::upkg::readTexture(*package, package->exports()[0]);
        REQUIRE(texture.has_value());
        CHECK(texture->mips.size() == 1);
        CHECK(texture->compressedMips.size() == 2);
    }
}

TEST_CASE("a texture subclass sharing the layout is read; another is refused",
          "[upkg]") {
    // INV-8. WaveTexture shares the mip chain and then stores something
    // further that this item does not describe, so reading its mips and
    // leaving the rest unread would defeat SS 4.3 for every caller at once.
    std::vector<std::uint8_t> data = emptyProperties();
    appendU8(data, 1);
    appendMip(data, 61, 0, {0x09u}, 1, 1);

    SECTION("WetTexture is modelled") {
        const std::vector<std::uint8_t> bytes =
            packageWithObject(61, "WetTexture", data);
        const auto package = Package::open(asBytes(bytes));
        REQUIRE(package.has_value());
        CHECK(uta::upkg::readTexture(*package, package->exports()[0]).has_value());
    }

    SECTION("WaveTexture is refused, and the message names the class") {
        const std::vector<std::uint8_t> bytes =
            packageWithObject(61, "WaveTexture", data);
        const auto package = Package::open(asBytes(bytes));
        REQUIRE(package.has_value());
        const auto texture = uta::upkg::readTexture(*package, package->exports()[0]);
        REQUIRE_FALSE(texture.has_value());
        CHECK(texture.error().code() == ErrorCode::InvalidArgument);
        CHECK(texture.error().message().find("WaveTexture") != std::string_view::npos);
    }
}

// --- Sound ------------------------------------------------------------------

TEST_CASE("a version 61 sound has no NextOffset field", "[upkg]") {
    std::vector<std::uint8_t> data = emptyProperties();
    appendIndex(data, NAME_WAV);
    appendIndex(data, 3);
    for (const std::uint8_t value : {0x52u, 0x49u, 0x46u}) {
        appendU8(data, value);
    }
    const std::vector<std::uint8_t> bytes = packageWithObject(61, "Sound", data);
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto sound = uta::upkg::readSound(*package, package->exports()[0]);
    REQUIRE(sound.has_value());
    CHECK(sound->formatName == static_cast<std::uint32_t>(NAME_WAV));
    CHECK(sound->data.size() == 3);
}

TEST_CASE("a version 68 sound's NextOffset must name the end of its data",
          "[upkg]") {
    // INV-5's other half, and the symmetry is the point: the same redundant
    // field in a different reader, checked the same way.
    const std::vector<std::uint8_t> payload{0x52u, 0x49u, 0x46u, 0x46u};

    std::vector<std::uint8_t> probe = emptyProperties();
    appendIndex(probe, NAME_WAV);
    appendU32(probe, 0);
    appendIndex(probe, static_cast<std::int32_t>(payload.size()));
    probe.insert(probe.end(), payload.begin(), payload.end());
    const std::size_t serialOffset =
        firstExportOffset(packageWithObject(68, "Sound", probe));
    const auto correct = static_cast<std::uint32_t>(
        serialOffset + emptyProperties().size() + 1 + 4 + 1 + payload.size());

    SECTION("the correct offset is accepted") {
        std::vector<std::uint8_t> data = emptyProperties();
        appendIndex(data, NAME_WAV);
        appendU32(data, correct);
        appendIndex(data, static_cast<std::int32_t>(payload.size()));
        data.insert(data.end(), payload.begin(), payload.end());
        const std::vector<std::uint8_t> bytes = packageWithObject(68, "Sound", data);
        const auto package = Package::open(asBytes(bytes));
        REQUIRE(package.has_value());
        CHECK(uta::upkg::readSound(*package, package->exports()[0]).has_value());
    }

    SECTION("an offset one byte out is refused") {
        std::vector<std::uint8_t> data = emptyProperties();
        appendIndex(data, NAME_WAV);
        appendU32(data, correct + 1);
        appendIndex(data, static_cast<std::int32_t>(payload.size()));
        data.insert(data.end(), payload.begin(), payload.end());
        const std::vector<std::uint8_t> bytes = packageWithObject(68, "Sound", data);
        const auto package = Package::open(asBytes(bytes));
        REQUIRE(package.has_value());
        const auto sound = uta::upkg::readSound(*package, package->exports()[0]);
        REQUIRE_FALSE(sound.has_value());
        CHECK(sound.error().message().find("NextOffset") != std::string_view::npos);
    }
}

TEST_CASE("a sound declaring more bytes than the export holds is refused",
          "[upkg]") {
    std::vector<std::uint8_t> data = emptyProperties();
    appendIndex(data, NAME_WAV);
    appendIndex(data, 0x00FFFFFF);
    const std::vector<std::uint8_t> bytes = packageWithObject(61, "Sound", data);
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto sound = uta::upkg::readSound(*package, package->exports()[0]);
    REQUIRE_FALSE(sound.has_value());
    CHECK(sound.error().code() == ErrorCode::MalformedData);
}

// --- The view rule ----------------------------------------------------------

TEST_CASE("mip pixels and sound payload are views into the caller's bytes",
          "[upkg]") {
    // INV-7, and it is not observable from behaviour: a reader that copied
    // into a vector would satisfy every other test in this file. Pointer
    // identity against the input buffer is what separates them.
    SECTION("a mip's pixels") {
        std::vector<std::uint8_t> data = emptyProperties();
        appendU8(data, 1);
        appendMip(data, 61, 0, {0x7Au, 0x7Bu}, 1, 1);
        const std::vector<std::uint8_t> bytes = packageWithObject(61, "Texture", data);
        const auto package = Package::open(asBytes(bytes));
        REQUIRE(package.has_value());
        const auto texture = uta::upkg::readTexture(*package, package->exports()[0]);
        REQUIRE(texture.has_value());
        REQUIRE(texture->mips.size() == 1);

        const auto* base = reinterpret_cast<const std::byte*>(bytes.data());
        CHECK(texture->mips[0].pixels.data() >= base);
        CHECK(texture->mips[0].pixels.data() < base + bytes.size());
    }

    SECTION("a sound's payload") {
        std::vector<std::uint8_t> data = emptyProperties();
        appendIndex(data, NAME_WAV);
        appendIndex(data, 2);
        appendU8(data, 0x5Au);
        appendU8(data, 0x5Bu);
        const std::vector<std::uint8_t> bytes = packageWithObject(61, "Sound", data);
        const auto package = Package::open(asBytes(bytes));
        REQUIRE(package.has_value());
        const auto sound = uta::upkg::readSound(*package, package->exports()[0]);
        REQUIRE(sound.has_value());

        const auto* base = reinterpret_cast<const std::byte*>(bytes.data());
        CHECK(sound->data.data() >= base);
        CHECK(sound->data.data() < base + bytes.size());
    }
}

// --- Level ------------------------------------------------------------------
//
// docs/specs/UTA-0057-level-tail-and-reachspecs.md SS 7 tier 1. These cases
// cover that spec's INV-1 and INV-4, UTA-0004's INV-9 (whose fixture case is
// owed here because this is the first item to implement the actor array), and
// UTA-0004's INV-1 at fixture level. None can be exercised by real content:
// the install supplies no truncated array and no known-wrong length.

namespace {

using uta::test::LevelExportWriter;

/// A level with a small actor array interleaving null and non-null slots, and
/// a reach-spec array whose entries are distinguishable by their distance.
LevelExportWriter levelWithSpecs(int specCount) {
    LevelExportWriter writer;
    writer.setProperties(emptyProperties());
    // Interleaved, so a reader that compacted the array or counted wrongly
    // disagrees on both members -- UTA-0004 INV-9.
    writer.addActor(1).addActor(0).addActor(2).addActor(0).addActor(0).addActor(3);
    writer.setURL("unreal", "host", "CTF-Fixture.unr", "portal", {"Game=Fixture", "Mutator=None"},
                  7777, 1);
    writer.setModel(9);
    for (int index = 0; index < specCount; ++index) {
        // distance is the label: position i in the file carries 1000 + i.
        writer.addReachSpec(1000 + index, 20 + index, 40 + index, 50 + index, 70 + index,
                            index, static_cast<std::uint8_t>(index % 2));
    }
    // The one trailer slot real content ever fills carries a TextBuffer
    // reference. Set here so a reader that stops before it, or reads it as a
    // fixed-width field, ends in the wrong place.
    writer.setTrailerFloat(24.17F).setTrailerIndex(7, 3000);
    return writer;
}

} // namespace

TEST_CASE("a Level reads back its actors, its slot count and its reach specs", "[upkg]") {
    const std::vector<std::uint8_t> bytes =
        packageWithObject(68, "Level", levelWithSpecs(10).build());
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto level = uta::upkg::readLevel(*package, package->exports()[0]);
    REQUIRE(level.has_value());

    // UTA-0004 INV-9: the null slots are counted, not returned.
    CHECK(level->rawSlotCount == 6u);
    REQUIRE(level->actors.size() == 3);
    CHECK(level->actors[0].raw() == 1);
    CHECK(level->actors[1].raw() == 2);
    CHECK(level->actors[2].raw() == 3);

    // INV-1: file order and file indexing, nothing dropped or renumbered. The
    // positions checked are spread across the array, so a reader that lost or
    // reordered one entry disagrees at the ones after it.
    REQUIRE(level->reachSpecs.size() == 10);
    CHECK(level->reachSpecs[0].distance == 1000);
    CHECK(level->reachSpecs[4].distance == 1004);
    CHECK(level->reachSpecs[9].distance == 1009);

    // The two fields the graph rests on, and the two nothing else checks --
    // SS 4.3 says the collision pair is graded by no invariant, so a fixture
    // is the only place their order is pinned at all.
    const uta::upkg::ReachSpec& fifth = level->reachSpecs[4];
    CHECK(fifth.start.raw() == 24);
    CHECK(fifth.end.raw() == 44);
    CHECK(fifth.collisionRadius == 54);
    CHECK(fifth.collisionHeight == 74);
    CHECK(fifth.reachFlags == 4);
    CHECK(fifth.pruned == 0u);
    CHECK(level->reachSpecs[5].pruned == 1u);
}

TEST_CASE("a Level returns the Model reference it holds", "[upkg]") {
    // UTA-0011 INV-13's reader half. UTA-0057 consumed this reference and
    // returned nothing, so a baker had to guess which Model was the world.
    const std::vector<std::uint8_t> bytes =
        packageWithObject(68, "Level", levelWithSpecs(2).build());
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto level = uta::upkg::readLevel(*package, package->exports()[0]);
    REQUIRE(level.has_value());
    CHECK(level->model.kind() == uta::upkg::ObjectReferenceKind::Export);
    CHECK(level->model.raw() == 9); // levelWithSpecs' setModel
    // The reach specs after it still land where they did.
    REQUIRE(level->reachSpecs.size() == 2);
    CHECK(level->reachSpecs[1].distance == 1001);
}

TEST_CASE("a Level stating a zero-length reach-spec array succeeds with an empty one",
          "[upkg]") {
    // INV-4: an empty array is returned when the FILE states one, and this is
    // the case that distinguishes that from a reader giving up.
    const std::vector<std::uint8_t> bytes =
        packageWithObject(68, "Level", levelWithSpecs(0).build());
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto level = uta::upkg::readLevel(*package, package->exports()[0]);
    REQUIRE(level.has_value());
    CHECK(level->reachSpecs.empty());
    CHECK(level->rawSlotCount == 6u);
}

TEST_CASE("a Level declaring more reach specs than the export can hold is refused",
          "[upkg]") {
    // INV-4's breaking case: the count is checked against the bytes present
    // before anything is reserved from it. A reader that instead treated the
    // short array as "no paths" and seeked to the end would consume its export
    // exactly and report an unpathed level, which UTA-0004 INV-1 cannot see.
    LevelExportWriter writer = levelWithSpecs(0);
    writer.setReachSpecCountOverride(0x00FFFFFF);
    const std::vector<std::uint8_t> bytes = packageWithObject(68, "Level", writer.build());
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto level = uta::upkg::readLevel(*package, package->exports()[0]);
    REQUIRE_FALSE(level.has_value());
    CHECK(level.error().code() == ErrorCode::MalformedData);
    CHECK(level.error().message().find("more than the") != std::string_view::npos);
}

TEST_CASE("a Level declaring more actor slots than the export can hold is refused",
          "[upkg]") {
    LevelExportWriter writer = levelWithSpecs(2);
    writer.setActorSlotCountOverride(0x00FFFFFF);
    const std::vector<std::uint8_t> bytes = packageWithObject(68, "Level", writer.build());
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto level = uta::upkg::readLevel(*package, package->exports()[0]);
    REQUIRE_FALSE(level.has_value());
    CHECK(level.error().code() == ErrorCode::MalformedData);
    CHECK(level.error().message().find("more than the") != std::string_view::npos);
}

TEST_CASE("a Level whose trailer ends in extra ZERO bytes is read", "[upkg]") {
    // The shape one map in the reference install carries: one more zero byte
    // than every other map. It is not explained, so it is not modelled as a
    // field -- what the reader requires is that the run be zero.
    LevelExportWriter writer = levelWithSpecs(3);
    writer.addTrailerByte(0).addTrailerByte(0);
    const std::vector<std::uint8_t> bytes = packageWithObject(68, "Level", writer.build());
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto level = uta::upkg::readLevel(*package, package->exports()[0]);
    REQUIRE(level.has_value());
    CHECK(level->reachSpecs.size() == 3);
}

TEST_CASE("a Level whose trailer ends in a NON-zero byte is refused", "[upkg]") {
    // UTA-0004 INV-1 at fixture level. The zero-run tolerance above is exactly
    // as wide as the content needs and no wider: a byte carrying a value is a
    // layout this reader does not describe, and is refused rather than
    // skipped to reach the export's end.
    LevelExportWriter writer = levelWithSpecs(3);
    writer.addTrailerByte(0).addTrailerByte(0x7Fu);
    const std::vector<std::uint8_t> bytes = packageWithObject(68, "Level", writer.build());
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto level = uta::upkg::readLevel(*package, package->exports()[0]);
    REQUIRE_FALSE(level.has_value());
    CHECK(level.error().code() == ErrorCode::MalformedData);
    CHECK(level.error().message().find("not zero") != std::string_view::npos);
}

TEST_CASE("a Level export with no serialised data is refused", "[upkg]") {
    // INV-4 again, in the shape a sizeless export takes: an export with no
    // bytes states no array, so returning an empty one would be the reader
    // giving up. This is where readLevel departs from readPolys, which returns
    // an empty result for a sizeless export.
    const std::vector<std::uint8_t> bytes = packageWithObject(68, "Level", {});
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto level = uta::upkg::readLevel(*package, package->exports()[0]);
    REQUIRE_FALSE(level.has_value());
    CHECK(level.error().code() == ErrorCode::MalformedData);
    CHECK(level.error().message().find("no serialised data") != std::string_view::npos);
}
