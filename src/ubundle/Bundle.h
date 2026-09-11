// The `.utab` container: the header, the section table, and the origin field.
//
// docs/specs/UTA-0008-bundle-container-and-origin.md.
//
// SCOPE: the file layout and its version, never the meaning of a section's
// contents. This library links uta_core, uta_umap and uta_unav and NOTHING
// else -- INV-10, asserted at configure time in src/ubundle/CMakeLists.txt.
// It must not reach uta_upkg: docs/design.md rule 2 keeps the package reader
// out of every runtime target, and ut-ants links this library to load a
// bundle (UTA-0016).
//
// NO FILE I/O HAPPENS HERE. `read` takes bytes the caller owns and `write`
// returns a vector; opening files is uta::fs's. That is what lets every case
// in tests/unit/ run without a filesystem.
//
// FAIL CLOSED. Origin::Derived is the ZERO value, deliberately -- SS 3
// decision 4. A byte zeroed by a partial write therefore reads as the
// restrictive value, so the failure it produces is a bundle wrongly withheld
// rather than one wrongly published (docs/design.md rule 15).

#pragma once

#include "core/Error.h"
#include "umap/Rooms.h"
#include "unav/Graphs.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace uta::ubundle {

/// The only version this reader accepts. SS 4.3 checks it for EQUALITY, not
/// as a lower bound: there is no such thing as a later version's file this
/// reader should tolerate, so a mismatch is UnsupportedVersion before the
/// section table is read.
///
/// 4 since UTA-0109 added the GEOM section -- that item's SS 4.2. 3 came with
/// UTA-0011's MATS section, its SS 4.10, and 2 with UTA-0052's TEXS section,
/// its SS 4.7. Nothing else about the framing moved: the header is still
/// sixteen bytes and the descriptor twenty-four. No .utab exists that this
/// orphans, 0.1.0 not having been cut.
inline constexpr std::uint32_t FORMAT_VERSION = 4;

/// The header's own size, and the offset the section table begins at. There
/// is no table-offset field in the format -- SS 4.3 -- because a field whose
/// value is always 16 is a field that can be wrong.
inline constexpr std::size_t HEADER_SIZE = 16;

/// One section descriptor -- SS 4.4.
inline constexpr std::size_t SECTION_DESCRIPTOR_SIZE = 24;

/// Where a bundle's content came from -- SS 4.5, docs/design.md rule 15.
enum class Origin : std::uint8_t {
    Derived = 0,  ///< something out of somebody's UT install contributed
    Authored = 1, ///< nothing did
};

/// The most restrictive of two origins. Derived wins.
///
/// NOT a maximum and NOT a bitwise OR over the numeric values: with
/// Derived = 0 both return Authored for (Authored, Derived) -- the
/// publishable value, from an input that touched an install. INV-12.
[[nodiscard]] constexpr Origin combine(Origin a, Origin b) noexcept {
    return (a == Origin::Authored && b == Origin::Authored) ? Origin::Authored
                                                            : Origin::Derived;
}

/// What the container holds. `Character` names no sections in this version;
/// the byte is present now because adding it later would bump the format
/// version, and docs/standards/versioning-overrides.md SS Breaking surfaces
/// records what that costs -- SS 4.3.
enum class BundleKind : std::uint8_t { Map = 0, Character = 1 };

struct BundleHeader {
    std::uint32_t formatVersion = FORMAT_VERSION;
    Origin origin = Origin::Derived;
    BundleKind kind = BundleKind::Map;
};

/// Which block format a stored texture uses -- UTA-0052 SS 4.3. A byte
/// outside this set is MalformedData and is never defaulted (that item's
/// INV-3).
enum class BlockFormat : std::uint8_t { BC4 = 0, BC5 = 1, BC7 = 2 };

/// The largest upscale a stored texture may record -- UTA-0052 SS 4.3.
///
/// HERE rather than in umat, because that item's INV-2 is a DECODE-TIME rule:
/// `read` refuses a texture whose stored ratio exceeds it, and ubundle may not
/// depend on umat (UTA-0052's INV-12 runs umat -> ubundle and never back).
/// umat re-uses this constant rather than declaring a second one, or the
/// writer and the reader drift and umat produces bundles its own reader
/// refuses.
inline constexpr std::uint32_t MAX_UPSCALE_FACTOR = 4;

/// Bytes one 4x4 block occupies in `format`.
[[nodiscard]] constexpr std::size_t bytesPerBlock(BlockFormat format) noexcept {
    return format == BlockFormat::BC4 ? 8u : 16u;
}

/// One block-compressed texture and its mip chain -- UTA-0052 SS 4.3.
///
/// SCOPE: every field here is LAYOUT. `blocks` is opaque to this library --
/// it is validated for LENGTH against the fields above it (UTA-0052's INV-1)
/// and never read. What a texel means is umat's, and umat is build-time only,
/// so nothing here may reach it (INV-10).
struct CompressedTexture {
    std::string name;
    BlockFormat format = BlockFormat::BC7;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    /// The dimensions of the image handed to umat::compress -- after any
    /// resample, before any upscale. Stored because nothing else in the
    /// bundle records them, so without them the cap is not auditable after a
    /// bake. The applied factor is `width / sourceWidth`, DERIVED and never
    /// stored: a stored copy is a field that can disagree with the two it
    /// comes from.
    std::uint16_t sourceWidth = 0;
    std::uint16_t sourceHeight = 0;
    std::uint8_t mipCount = 1;
    std::vector<std::byte> blocks;
};

/// The bytes `blocks` must hold for the fields beside it -- UTA-0052 SS 4.3.
///
/// Levels are consecutive halvings floored at one, so level l measures
/// max(1, width >> l) by max(1, height >> l), and each contributes
/// ceil(w/4) * ceil(h/4) * bytesPerBlock(format).
///
/// Zero for a combination the format does not permit, which UTA-0052's INV-2
/// refuses first. A caller must not read a zero as "no bytes required".
[[nodiscard]] std::uint64_t expectedBlockBytes(const CompressedTexture& texture) noexcept;

/// One material's own values -- UTA-0011 SS 4.10. Its maps are the TEXS
/// entries named `<id>:<map>`.
///
/// SCOPE: `id` is opaque here, as a texture's blocks are. This library does
/// not check that a record has maps: a section never reads another's meaning,
/// and the baker guarantees the pairing (UTA-0011 INV-17).
struct MaterialRecord {
    std::string id;
    bool metallic = false;
};

/// One corner of a triangle -- UTA-0109 SS 4.2.
struct GeometryVertex {
    std::array<float, 3> position{}; ///< UT99's own coordinates and units
    std::array<float, 3> normal{};   ///< its surface's normal, as the file stores it
    float u = 0;                     ///< 1.0 is one repeat of the texture
    float v = 0;
};

/// A run of triangles wearing one material under one set of flags.
///
/// SCOPE: `material` is opaque here, as a MATS id is. This library does not
/// check that it names a MATS record; the baker guarantees it (UTA-0109
/// INV-10).
struct GeometryBatch {
    std::string material;         ///< a MATS id, or empty for none
    std::uint32_t polyFlags = 0;  ///< UT99's PolyFlags, verbatim
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0; ///< three per triangle
};

/// The level's drawable surfaces as triangles -- UTA-0109 SS 4.2.
struct Geometry {
    std::vector<GeometryVertex> vertices;
    std::vector<std::uint32_t> indices;
    /// Strictly ascending by `material` bytewise, then `polyFlags`, and tiling
    /// `indices` from its first element to its last.
    std::vector<GeometryBatch> batches;
};

/// A bundle's contents.
///
/// A section absent from the file is an empty optional, which is DISTINCT
/// from a present but empty one -- SS 4.4. An empty NavGraph says the level
/// was examined and had no navigation points; an absent NAVG says nothing.
struct Bundle {
    BundleHeader header;
    std::optional<umap::RoomMap> rooms;
    std::optional<unav::NavGraph> nav;
    std::optional<unav::WiringGraph> wiring;
    std::optional<std::vector<CompressedTexture>> textures;
    /// In strictly ascending bytewise `id` order -- UTA-0011 SS 4.10.
    std::optional<std::vector<MaterialRecord>> materials;
    /// UTA-0109 SS 4.2.
    std::optional<Geometry> geometry;
};

/// Decode a whole bundle.
///
/// Total: every input returns. Never throws, never reads outside `bytes`,
/// and never sizes an allocation from a count the file supplied before that
/// count has been checked against the bytes remaining in its own section
/// (INV-1, INV-2). A successful result satisfies every structural rule
/// SS 4.9 lists (INV-3).
[[nodiscard]] Result<Bundle> read(std::span<const std::byte> bytes);

/// Encode a bundle. Sections are emitted in the fixed order ROOM, NAVG,
/// WIRG, TEXS, MATS, GEOM, omitting absent ones, and the output is byte-identical for equal
/// inputs on every compiler (INV-7, INV-8) -- docs/design.md SS Close calls
/// names a bundle by the hash of its own contents.
///
/// A bundle whose structures violate SS 4.9 is refused with InvalidArgument
/// rather than written, so a bad bundle cannot be produced here and then
/// blamed on the reader.
[[nodiscard]] Result<std::vector<std::byte>> write(const Bundle& bundle);

/// The header alone, from the first 16 bytes -- UTA-0013's entry point.
///
/// It validates the header's OWN fields and nothing else: magic,
/// formatVersion, origin, kind and reserved. It does NOT apply SS 4.4's
/// sectionCount bound, and must not -- that rule needs the file's total size,
/// which a caller holding sixteen bytes does not have. A readHeader that
/// applied it would refuse every valid bundle, and under SS 4.5's fail-closed
/// rule every refusal reads as "not authored", so the quarantine guard would
/// block every push of an authored bundle while looking like it was working.
[[nodiscard]] Result<BundleHeader> readHeader(std::span<const std::byte> bytes);

} // namespace uta::ubundle
