// Building a RoomMap from a level's own BSP tables.
//
// docs/specs/UTA-0007-room-partition-and-lookup.md SS 4.1, SS 4.4, SS 4.5.
//
// BAKE-SIDE ONLY, as src/unav/Build.h is. This library links uta_upkg, so no
// runtime target may link it -- docs/design.md rule 2, which is why the types
// and the lookup live in Rooms.h in a library that links uta_core alone. A
// single library would breach that rule through this item.
//
// It takes the `Model` UTA-0069 returns rather than re-reading the package,
// because a second decoder of that layout is what SS 4.1 forbids.

#ifndef UTA_UMAP_BUILD_H
#define UTA_UMAP_BUILD_H

#include "core/Error.h"
#include "umap/Rooms.h"
#include "upkg/Geometry.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace uta::umap {

/// The engine's own ceiling on a level's zone count, documented on the Unreal
/// wiki as "A level can only have 64 zones" -- SS 4.2. A Model declaring more
/// is refused: membership testing does NOT cover this, because a table of 300
/// zones passes every per-leaf membership check and then aliases zone 300 onto
/// 44 through SS 4.6's narrowing to a byte.
inline constexpr std::size_t ZONE_CEILING = 64;

/// What the bake may vary. Every default is ABSOLUTE: none is computed from
/// another field, so two conforming builders given the same options produce
/// the same map -- SS 4.1.
struct RoomBuildOptions {
    /// Half UT99's nominal 64-unit player width, so a doorway is several
    /// cells across.
    float sampleSpacing = 32.0F;
    /// Half the default spacing -- but a LITERAL, not a computation. A caller
    /// raising sampleSpacing does NOT move this, because a tolerance that
    /// tracked another field would differ between two builders that both read
    /// that document (SS 14 asks whether it should).
    float simplifyTolerance = 16.0F;
    /// Twice UT99's nominal player height, so a room and the gallery above it
    /// separate while a stepped floor does not.
    float floorSeparation = 128.0F;
};

/// What the build could not do, on the success path. NOT part of what
/// `ubundle` writes -- it is bake diagnostics, and design rule 17 governs the
/// room model rather than this.
struct RoomBuildReport {
    /// Zone indices of rooms that caught NO sample, so have no footprint --
    /// SS 4.4. A room that caught samples always traces (INV-6), so this is
    /// the unsampled population and not a trace-failure population.
    std::vector<std::uint32_t> roomsWithoutFootprint;
    /// Zone indices named by a leaf but outside the zone table -- SS 6.
    /// Ascending and without repeats: a zone named by a million leaves is one
    /// refusal, not a million. A NEGATIVE index is outside the table too, and
    /// appears here as its two's complement -- the field is unsigned because
    /// a zone index is, and no unsigned value spells the file's own -1.
    std::vector<std::uint32_t> refusedZones;
};

/// The map, and what the build could not do while making it.
///
/// The report rides on the SUCCESS path, not on `Result`'s error channel.
/// `Result`'s error arm means the build REFUSED; a room with no footprint is
/// a built map with a note attached, and the two must not share a channel or
/// `ubake` cannot tell a degraded map from a rejected one -- SS 4.1.
struct RoomBuildResult {
    RoomMap map;
    RoomBuildReport report;
};

/// Partition a level into rooms and trace each one's footprint.
///
/// Refuses -- the error arm -- on a `sampleSpacing` that is not a positive
/// finite number, on a zone table past `ZONE_CEILING`, and on a bounding box
/// whose lattice would not terminate. Everything else degrades and is
/// reported: SS 6 owns which is which.
[[nodiscard]] Result<RoomBuildResult> buildRoomMap(const uta::upkg::Model& model,
                                                   const RoomBuildOptions& options = {});

} // namespace uta::umap

#endif // UTA_UMAP_BUILD_H
