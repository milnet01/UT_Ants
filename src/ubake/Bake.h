// One bake -- a map's sections and its materials -- and writing it.
//
// docs/specs/UTA-0011-map-baker.md SS 4.5 to SS 4.7.
//
// BAKE-SIDE ONLY: uta_ubake links the package reader, so docs/design.md rule 2
// keeps it out of both runtime targets.
//
// SCOPE: the sections that exist today -- ROOM, NAVG, WIRG, TEXS, MATS, and
// GEOM from UTA-0109. Lights and placements, collision, baked light and the
// recipe are UTA-0110 to UTA-0113, each plugging into this baker rather than
// starting a second one. Until UTA-0113 lands every map bakes with no recipe.
//
// NEVER DEGRADES. Over budget, nothing is written -- UTA-0052's rule. A texture
// that cannot be made is skipped and named; the bake goes on without it.

#pragma once

#include "core/Error.h"
#include "core/Jobs.h"
#include "ubake/Install.h"
#include "ubundle/Bundle.h"
#include "umap/Build.h"
#include "umat/Library.h"
#include "umat/Material.h"
#include "upkg/Class.h"
#include "upkg/Level.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace uta::ubake {

/// A material variant the bake did not make, and why.
struct SkippedTexture {
    std::string material; ///< umat::materialId of the variant not made
    std::string reason;
};

struct BakeResult {
    ubundle::Bundle bundle; ///< origin Derived, kind Map
    umap::RoomBuildReport rooms;
    umat::BudgetReport budget;
    std::vector<SkippedTexture> skipped;
};

/// Build a bundle from one map. Writes nothing and enforces no budget.
///
/// `mapName` is the map file's folded stem (detail::mapNameOf). MalformedData,
/// naming the map, when the map has no `Level` export or more than one, or
/// when the level names no `Model` export of its own.
[[nodiscard]] Result<BakeResult> bake(const upkg::Package& map, std::string_view mapName,
                                      Install& install, JobSystem& jobs);

enum class Verdict { Written, Cached, OverBudget };

struct BakeRequest {
    std::filesystem::path install;
    std::filesystem::path map;
    std::filesystem::path outDir;
    bool force = false;
    std::uint64_t budgetBytes = umat::TEXTURE_BUDGET_BYTES;
};

struct BakeOutcome {
    Verdict verdict = Verdict::Written;
    std::string name;
    std::filesystem::path path;         ///< outDir / (name + ".utab")
    std::optional<BakeResult> result;   ///< absent when Cached
};

/// Name, look in the cache, bake, check the budget, write -- SS 4.7.
///
/// No part of the output path comes from the map's contents: the name is hex
/// digits and the directory is the caller's. Where bakes live is UTA-0016's
/// decision (SS 15), so this takes the directory rather than choosing one.
[[nodiscard]] Result<BakeOutcome> bakeToDirectory(const BakeRequest& request, JobSystem& jobs);

namespace detail {

/// The curated library's lookup -- `umat::curated` outside tests.
using CuratedLookup = std::function<const umat::CuratedOverride*(std::uint64_t fingerprint)>;

/// One bake with its dependencies given -- a test seam. Every package lookup
/// goes through `resolver`. `bake` passes `install.resolver()`, `umat::curated`
/// and `umat::TEXTURE_BUDGET_BYTES`; `bakeToDirectory` passes its request's
/// `budgetBytes`.
[[nodiscard]] Result<BakeResult> bake(const upkg::Package& map, std::string_view mapName,
                                      const upkg::PackageResolver& resolver, JobSystem& jobs,
                                      const CuratedLookup& curated, std::uint64_t budgetBytes);

/// SS 4.5 step 1: the map's one Level export. MalformedData, naming the map,
/// when it has none or more than one. ut-paths reads a map's level the bake's
/// way through this (UTA-0121 SS 4.6).
[[nodiscard]] Result<const upkg::ExportEntry*> findLevel(const upkg::Package& map,
                                                         std::string_view mapName);

/// SS 4.5 step 2: the Model export `level` names. MalformedData, naming the
/// map, when the reference is not an export of the map or names another class.
[[nodiscard]] Result<const upkg::ExportEntry*> findModel(const upkg::Package& map,
                                                         const upkg::Level& level,
                                                         std::string_view mapName);

/// A texture's scale -- UTA-0109 SS 4.4: its `DrawScale` property where that
/// is a finite positive float, else 1. `DrawScale` is the script's name for
/// the member UT 4.32's UnTex.h calls `UTexture::Scale`.
[[nodiscard]] double textureScale(const upkg::Package& holder,
                                  std::span<const upkg::Property> properties);

/// Where a surface's texture reference leads -- UTA-0011 SS 4.6 "Which
/// textures". `holder` is null where it does not resolve.
struct TextureExport {
    const upkg::Package* holder = nullptr;
    const upkg::ExportEntry* entry = nullptr;
};

[[nodiscard]] Result<TextureExport> resolveTexture(const upkg::Package& map,
                                                   std::string_view mapName,
                                                   upkg::ObjectReference reference,
                                                   const upkg::PackageResolver& resolver);

} // namespace detail

} // namespace uta::ubake
