// The player's answers about which pictures may move -- docs/specs/UTA-0277-
// per-tile-variation.md SS 4.3 and SS 4.5.
//
// APPLIED WHERE A BUNDLE IS LOADED, not baked in. The bake's name exists
// before the bake and a picture's hash only once it is decoded, so an answer
// could not enter the name of only the maps using that picture (SS 4.3).
// Applied here, an answer takes effect the next time a map is loaded, with no
// re-bake.
//
// The file is `<fs::dataDirectory()>/tile-kinds.txt`, one answer a line:
//
//     # a comment
//     <64 hex digits> shuffle|fixed  [# package.group.name]
//
// Content, not name: one picture in two packages takes one answer.

#pragma once

#include "core/Error.h"
#include "ubundle/Bundle.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace uta::ubundle {

struct TileAnswer {
    std::array<std::byte, 32> hash{};
    TileKind kind = TileKind::Fixed; ///< Shuffle or Fixed, never Unsure
};

struct TileAnswers {
    std::vector<TileAnswer> answers; ///< ascending by hash, each hash once
    /// Each line not applied, and why: malformed, or a hash given two
    /// different answers, whose every line is dropped. Never a refusal (INV-8).
    std::vector<std::string> warnings;
};

/// SS 4.5's lines. Blank and `#` lines are skipped; a line repeating an
/// earlier answer exactly is not a conflict.
[[nodiscard]] TileAnswers parseTileAnswers(std::string_view text);

/// Every material whose hash an answer names takes that answer's kind. A
/// material whose hash is all zero is never changed: SS 4.2 step 1 excluded it.
/// Returns how many materials changed kind.
std::size_t applyTileAnswers(Bundle& bundle, const TileAnswers& answers);

/// The answers file's path: `<fs::dataDirectory()>/tile-kinds.txt`.
[[nodiscard]] Result<std::filesystem::path> tileAnswersPath();

/// Read the answers file and apply it. An absent file is no answers and no
/// warning -- the normal state (SS 14). A file that cannot be read, or no data
/// directory, is a warning and no answers (SS 6). Returns the warnings.
std::vector<std::string> applyTileAnswersFile(Bundle& bundle);

} // namespace uta::ubundle
