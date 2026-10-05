// One bake -- a map's sections and its materials -- and writing it.
//
// docs/specs/UTA-0011-map-baker.md SS 4.5 to SS 4.7.
//
// BAKE-SIDE ONLY: uta_ubake links the package reader, so docs/design.md rule 2
// keeps it out of both runtime targets.
//
// SCOPE: every section ubundle defines, each step plugging into this baker
// rather than starting a second one. A map's recipe (UTA-0113) is found here,
// enters the name, and sets material settings after the curated library.
//
// NEVER DEGRADES. Over budget, nothing is written -- UTA-0052's rule. A texture
// that cannot be made is skipped and named; the bake goes on without it.

#pragma once

#include "core/Error.h"
#include "core/Jobs.h"
#include "core/Timing.h"
#include "ubake/Flames.h"
#include "ubake/Install.h"
#include "ubake/TextureCache.h"
#include "ubake/TileKind.h"
#include "ubundle/Bundle.h"
#include "umap/Build.h"
#include "umat/Flames.h"
#include "umat/Library.h"
#include "umat/Material.h"
#include "upkg/Class.h"
#include "upkg/Level.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"
#include "urecipe/Lookup.h"
#include "urecipe/Recipe.h"

#include <array>
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

/// UTA-0277 SS 4.5: a material the bake could not judge, for the player to
/// answer. `view` is the largest surface wearing it.
struct TileQuestion {
    std::string material;
    std::string hash; ///< 64 lower-case hex digits, as the answers file keys it
    double lines = 0;
    double spots = 0;
    std::uint32_t surfaces = 0;
    std::array<double, 3> at{};
    std::array<double, 3> normal{}; ///< unit length
    double extent = 0;
    /// UTA-0294: the furthest of 192, 128, 96, 64 and 32 units out along
    /// `normal` from `at` that no drawn surface blocks; 32 when none is clear.
    double clear = 0;
};

struct BakeResult {
    ubundle::Bundle bundle; ///< origin Derived, kind Map
    umap::RoomBuildReport rooms;
    umat::BudgetReport budget;
    std::vector<SkippedTexture> skipped;
    /// UTA-0263 SS 6: flame surfaces carrying a sheet's flags that made no
    /// FLAM record, and so stay in GEOM.
    std::vector<SkippedFlame> skippedFlames;
    /// UTA-0105 SS 6: liquid materials made with no liquid look -- their
    /// class's defaults did not read -- so they show their still.
    std::vector<SkippedTexture> skippedLiquids;
    /// UTA-0286 SS 6: non-flame FireTexture materials made with no fire look
    /// -- a short palette or a look past ubundle's limits -- so they show
    /// their still.
    std::vector<SkippedTexture> skippedFires;
    /// UTA-0148: materials the texture cache served, and ones it had to make.
    /// Both 0 when the bake ran without one.
    std::uint32_t textureCacheHits = 0;
    std::uint32_t textureCacheMisses = 0;
    /// UTA-0129: detail::bake's steps and how long each took, depth 0.
    std::vector<Phase> phases;
    /// UTA-0113 SS 4.5: the recipe's assignments naming a texture the map does
    /// not use, ascending. Reported, never refused.
    std::vector<std::string> recipeUnused;
    /// UTA-0277 SS 4.5: every Unsure material, once, most surfaces first.
    std::vector<TileQuestion> tileQuestions;
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
    /// UTA-0245: over budget, shrink the textures until they fit (umat::
    /// fitToBudget) rather than refuse -- only when asked (user, 2026-09-29).
    /// A fitted bake is named apart (detail::fittedName), so a request
    /// without this is never served one.
    bool fitBudget = false;
    /// UTA-0148: where made materials are kept between bakes (TextureCache).
    /// Empty keeps none, the default; ut-bake's --texture-cache sets it.
    std::filesystem::path textureCache;
    /// UTA-0113 SS 4.3: where the map's recipe is looked for. Empty looks
    /// nowhere, the default; ut-bake passes urecipe::standardSources.
    urecipe::Sources recipes;
};

/// UTA-0141: a package name more than one install file carries, where the
/// file the game's Paths order picks lacks an object an import asks for and a
/// shadowed file holds it. The bake uses `used`, as the game does. A shared
/// name that changes nothing is not a clash here: the stock install has
/// several (user decision, 2026-09-25).
struct PackageClash {
    std::string package; ///< folded
    std::string object;  ///< the first import found that `used` cannot serve, as `package.Group.Name`
    std::filesystem::path used;
    /// The shadowed files that hold `object`, and any that could not be read or
    /// opened, which might.
    std::vector<std::filesystem::path> shadowed;
};

struct BakeOutcome {
    Verdict verdict = Verdict::Written;
    std::string name;
    std::filesystem::path path;         ///< outDir / (name + ".utab")
    std::optional<BakeResult> result;   ///< absent when Cached
    /// UTA-0245: set when this bake was fitted to its budget. Absent for a
    /// cached fitted bake, whose rounds were counted when it was written.
    std::optional<umat::FitReport> fitted;
    /// Found for a cached bake too: a clash is a fact about the install. The
    /// user's decision (UTA-0141, 2026-09-13) is to warn and carry on.
    std::vector<PackageClash> clashes;
    /// UTA-0129: every step that ran and how long it took, whatever the
    /// verdict; detail::bake's steps sit under `bake`, one deeper.
    std::vector<Phase> phases;
    /// UTA-0113: the recipe file the bake used, cached or not. Absent: none.
    std::optional<std::filesystem::path> recipe;
};

/// Name, look in the cache, bake, check the budget, write -- SS 4.7.
///
/// No part of the output path comes from the map's contents: the name is hex
/// digits and the directory is the caller's. Where bakes live is UTA-0016's
/// decision (SS 15), so this takes the directory rather than choosing one.
[[nodiscard]] Result<BakeOutcome> bakeToDirectory(const BakeRequest& request, JobSystem& jobs);

namespace detail {

/// The curated library's lookup -- `umat::curated` outside tests. UTA-0148:
/// the bake calls it from several threads at once, so it must be safe to.
using CuratedLookup = std::function<const umat::CuratedOverride*(std::uint64_t fingerprint)>;

/// Whether a picture is drawn as a flame -- `umat::isFlame` outside tests
/// (UTA-0263 SS 4.1). Called from several threads at once, as CuratedLookup is.
using FlameLookup = std::function<bool(std::uint64_t fingerprint)>;

/// One bake with its dependencies given -- a test seam. Every package lookup
/// goes through `resolver`. `bake` passes `install.resolver()`, `umat::curated`
/// and `umat::TEXTURE_BUDGET_BYTES`; `bakeToDirectory` passes its request's
/// `budgetBytes` and the recipe it found. A null `recipe` is no recipe.
/// `tileLimits` are UTA-0277 SS 4.2's, which a test moves to force a kind.
[[nodiscard]] Result<BakeResult> bake(const upkg::Package& map, std::string_view mapName,
                                      const upkg::PackageResolver& resolver, JobSystem& jobs,
                                      const CuratedLookup& curated, std::uint64_t budgetBytes,
                                      TextureCache* textureCache = nullptr,
                                      const FlameLookup& isFlame = umat::isFlame,
                                      const urecipe::Recipe* recipe = nullptr,
                                      const TileLimits& tileLimits = {});

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
