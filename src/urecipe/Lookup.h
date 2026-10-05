// Finding a map's recipe -- docs/specs/UTA-0113-recipe-format.md SS 4.3.
//
// A bake takes at most one recipe: the one named on the command line, else the
// player's own, else the one shipped with the game. A recipe is refused for a
// map it was not written for, so it never applies to the wrong world.

#pragma once

#include "core/Error.h"
#include "urecipe/Recipe.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace uta::urecipe {

/// Where to look, in SS 4.3's order. An empty directory is not looked in.
struct Sources {
    std::optional<std::filesystem::path> named; ///< `ut-bake --recipe`; must exist when given
    std::filesystem::path player;               ///< `<data directory>/recipes`
    std::filesystem::path shipped;              ///< shippedDirectory()
};

struct Found {
    Recipe recipe;
    std::filesystem::path path;
};

/// The recipes shipped with the game: the repository's `recipes/`. No install
/// step exists yet, so this is the source tree's copy.
[[nodiscard]] std::filesystem::path shippedDirectory();

/// SS 4.3's three places. The player's is left empty where the platform names
/// no data directory.
[[nodiscard]] Sources standardSources(std::optional<std::filesystem::path> named);

/// The map file's stem, ASCII lower-cased -- the `mapName` a bake and `find`
/// take, and the stem of a recipe's file name.
[[nodiscard]] std::string mapNameOf(const std::filesystem::path& map);

/// The file `find` reads for `mapName`, opening none: `named` when given,
/// else the first `<mapName>.recipe` present in `player`, then `shipped`.
/// Nothing when neither has one. UTA-0287: the launcher stamps this file.
[[nodiscard]] std::optional<std::filesystem::path> located(const Sources& sources, std::string_view mapName);

/// The recipe for `mapName` (a folded file stem) whose file has `mapDigest`,
/// or nothing. `named` is used when given and refused when it does not exist:
/// a file the player named and the bake ignored would change nothing in
/// silence. Otherwise `<mapName>.recipe` in `player`, then in `shipped`.
///
/// Every refusal of parse, naming the file. InvalidArgument when the recipe's
/// `file` is not `mapName`, or its `sha256` is set and is not `mapDigest`.
[[nodiscard]] Result<std::optional<Found>> find(const Sources& sources, std::string_view mapName,
                                                const std::array<std::byte, 32>& mapDigest);

} // namespace uta::urecipe
