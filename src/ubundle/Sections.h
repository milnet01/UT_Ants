// The section codecs Bundle.cpp dispatches to, one .cpp file each.
//
// INTERNAL to uta_ubundle. Each section changes for its own reason -- ROOM
// with umap, NAVG and WIRG with unav, TEXS with umat, MATS with ubake -- so
// each lives in its own file, and work on one does not share a file with work
// on another (UTA-0091). Bundle.cpp keeps the framing: the header, the section
// table, and read and write.
//
// docs/specs/UTA-0008-bundle-container-and-origin.md SS 4.6 to SS 4.9,
// docs/specs/UTA-0052-texture-memory-budget.md SS 4.3 for TEXS, and
// docs/specs/UTA-0011-map-baker.md SS 4.10 for MATS.

#pragma once

#include "Codec.h"

#include "ubundle/Bundle.h"

#include <cstddef>
#include <vector>

namespace uta::ubundle::detail {

constexpr SectionId ID_ROOM = {'R', 'O', 'O', 'M'};
constexpr SectionId ID_NAVG = {'N', 'A', 'V', 'G'};
constexpr SectionId ID_WIRG = {'W', 'I', 'R', 'G'};
constexpr SectionId ID_TEXS = {'T', 'E', 'X', 'S'};
constexpr SectionId ID_MATS = {'M', 'A', 'T', 'S'};

// Structural validation -- SS 4.9.
//
// Every check runs before `read` returns, and `write` runs the same checks
// over the structure it was handed. Validating in the accessors instead is
// what UTA-0003's INV-6 records the cost of: uta::unav::edgesFrom builds a
// std::span from firstEdge and edgeCount with no guard of its own, so a run
// reaching past `edges` is undefined behaviour in the CONSUMER and not a
// wrong answer this library can be blamed for later.
//
// `code` is MalformedData on the read path and InvalidArgument on the write
// path: the same rule, blamed on the file or on the caller.

// ROOM -- RoomSection.cpp.
[[nodiscard]] Result<umap::RoomMap> readRoomMap(Cursor& cursor);
[[nodiscard]] Result<void> validateRoomMap(const umap::RoomMap& map, ErrorCode code);
[[nodiscard]] std::vector<std::byte> encodeRoomMap(const umap::RoomMap& map);

// NAVG -- NavSection.cpp.
[[nodiscard]] Result<unav::NavGraph> readNavGraph(Cursor& cursor);
[[nodiscard]] Result<void> validateNavGraph(const unav::NavGraph& graph, ErrorCode code);
[[nodiscard]] std::vector<std::byte> encodeNavGraph(const unav::NavGraph& graph);

// WIRG -- WiringSection.cpp.
[[nodiscard]] Result<unav::WiringGraph> readWiringGraph(Cursor& cursor);
[[nodiscard]] Result<void> validateWiringGraph(const unav::WiringGraph& graph, ErrorCode code);
[[nodiscard]] std::vector<std::byte> encodeWiringGraph(const unav::WiringGraph& graph);

// TEXS -- TextureSection.cpp. No post-decode validator: UTA-0052's INV-1 and
// INV-2 are decode-time refusals applied inside the element reader.
[[nodiscard]] Result<std::vector<CompressedTexture>> readTextures(Cursor& cursor);
[[nodiscard]] std::vector<std::byte> encodeTextures(const std::vector<CompressedTexture>& textures);

// MATS -- MaterialSection.cpp, UTA-0011 SS 4.10. The metallic byte is refused
// inside the element reader; the id order is the validator's.
[[nodiscard]] Result<std::vector<MaterialRecord>> readMaterials(Cursor& cursor);
[[nodiscard]] Result<void> validateMaterials(const std::vector<MaterialRecord>& materials,
                                             ErrorCode code);
[[nodiscard]] std::vector<std::byte> encodeMaterials(const std::vector<MaterialRecord>& materials);

} // namespace uta::ubundle::detail
