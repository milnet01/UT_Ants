// The baker version and the bake name.
//
// docs/specs/UTA-0011-map-baker.md SS 4.3 and SS 4.4, and docs/design.md
// SS Content addressing: "Every other bake input must be covered by one of the
// three, or the name is a lie." The three are the map, the recipe and the
// baker version; the map's import closure is folded in because a map's
// textures and its actors' ancestry live in other packages.
//
// THE NAME EXISTS BEFORE THE BAKE. It is what finds a cached one, so it is
// built from the map and what its imports name, never from what a bake
// happened to open. INV-2 checks that the bake opens nothing outside it.

#pragma once

#include "core/Error.h"
#include "ubake/Install.h"
#include "upkg/Class.h"
#include "upkg/Package.h"
#include "urecipe/Recipe.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace uta::ubake {

/// Bumped by hand whenever any code a bake runs changes what a bake writes:
/// `ubake` itself, or any library src/ubake/CMakeLists.txt links it to.
/// tests/unit/BakeGoldenTest.cpp fails a change without a bump, and a bump
/// without re-recording its golden hash (INV-5).
inline constexpr std::uint32_t BAKER_REVISION = 71; // 71 since UTA-0439 turns a mover's reach by the roll UTA-0119 SS 4.5 gives

/// "r<BAKER_REVISION>-f<ubundle::FORMAT_VERSION>-l<umat::libraryDigest()>",
/// the revision and format in decimal, the digest as sixteen lower-case hex
/// digits.
[[nodiscard]] std::string bakerVersion();

/// One package of the map's import closure: its folded name, and the digest of
/// the file it resolves to -- empty when the install's resolver returns no
/// package for it: no file, or a file that does not open.
struct ClosureEntry {
    std::string name;
    std::optional<std::array<std::byte, 32>> digest;
};

struct NameInputs {
    std::string bakerVersion;
    std::string mapName;               ///< the map file's folded stem
    std::array<std::byte, 32> mapDigest{};
    std::vector<ClosureEntry> closure; ///< any order; the name sorts it
    /// urecipe::bakeDigest of the map's recipe; absent when it has none.
    std::optional<std::array<std::byte, 32>> recipeDigest;
};

/// 64 lower-case hex digits. A null `recipe` is no recipe.
[[nodiscard]] Result<std::string> bakeName(const std::filesystem::path& map, Install& install,
                                           const urecipe::Recipe* recipe = nullptr);

namespace detail {

/// The lower-case hex SHA-256 of SS 4.4's byte string.
[[nodiscard]] std::string bakeNameOf(const NameInputs& inputs);

/// The folded names of the map's import closure, ascending. Every lookup goes
/// through `resolver`.
[[nodiscard]] Result<std::vector<std::string>> closure(const upkg::Package& map,
                                                       const upkg::PackageResolver& resolver);

/// The map file's folded stem -- the name SS 4.4 item 4 hashes and the
/// `package` a map's own textures are named under (SS 4.6).
[[nodiscard]] std::string mapNameOf(const std::filesystem::path& map);

/// `bakeName` for a map whose bytes the caller already holds, so a bake does
/// not read the map twice.
[[nodiscard]] Result<std::string> bakeName(std::span<const std::byte> mapBytes,
                                           std::string_view mapName, Install& install,
                                           const urecipe::Recipe* recipe = nullptr);

/// UTA-0245: the name a bake fitted to `budgetBytes` takes, from its full
/// bake's `name` -- 64 lower-case hex digits, never equal to `name`. A budget
/// other than the default is part of it, so a bake fitted to one budget is
/// never served for another; at the default the name is UTA-0245's own
/// (review-code 2026-10-15).
[[nodiscard]] std::string fittedName(std::string_view name, std::uint64_t budgetBytes);

/// Lower-case hex, two digits a byte.
[[nodiscard]] std::string hex(std::span<const std::byte> bytes);

} // namespace detail

} // namespace uta::ubake
