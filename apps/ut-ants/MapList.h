// The map launcher's parts that need no window -- UTA-0170.
//
// Compiled into the client and into the unit tests. Launcher.cpp is the SDL
// half. The user's decisions on UTA-0170 set the shape: every map in the
// install is listed, a picked map bakes then and the bake is kept, a map that
// fails is listed as failed, and each map has a notes box saved to one plain
// text file.

#pragma once

#include "core/Error.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace uta::client {

struct MapFile {
    std::string name;            ///< the file's stem, as spelled, e.g. "MH-NivenSB"
    std::filesystem::path path;  ///< the .unr file
};

/// Every `*.unr` in the install's Maps directory, matched case-insensitively
/// as ut-bake matches it, sorted by name ignoring case. Empty when there is
/// no Maps directory.
[[nodiscard]] std::vector<MapFile> listMaps(const std::filesystem::path& install);

/// The indices of `maps` whose name contains `filter`, ignoring case. An empty
/// filter keeps every map.
[[nodiscard]] std::vector<std::size_t> filterMaps(const std::vector<MapFile>& maps, std::string_view filter);

/// UTA-0179: the `mapPrefixes` of one `ut-bake --game-types <install>` run,
/// or nothing when the run failed or gave no such array.
[[nodiscard]] std::optional<std::vector<std::string>> readMapPrefixes(std::string_view output, int exitCode);

/// UTA-0179: the maps UT99's own lists offer -- those whose name starts with
/// one of `prefixes`, ignoring case. An empty prefix offers every map, as it
/// does in the game. CityIntro, the opening movie, starts with none.
[[nodiscard]] std::vector<MapFile> playableMaps(std::vector<MapFile> maps, const std::vector<std::string>& prefixes);

/// What one `ut-bake --install ... --out ... <map>` run said, read from its
/// standard output and exit code.
struct BakeAnswer {
    bool baked = false;          ///< written or cached, with a path
    std::filesystem::path path;  ///< the bundle, when baked
    std::string failure;         ///< why not, when not baked
};

/// ut-bake's top-level `verdict`, `path` and `error` (docs/specs/UTA-0011-map-baker.md
/// SS 4.8). A run that wrote no JSON or an unexpected verdict is a failure that
/// names the exit code.
[[nodiscard]] BakeAnswer readBakeAnswer(std::string_view output, int exitCode);

/// Where the launcher keeps what it writes.
struct LauncherPaths {
    std::filesystem::path bakes;    ///< ut-bake --out; derived, so under a content/ directory (design rule 15)
    std::filesystem::path notes;    ///< one <map>.txt per map, written by the user
    std::filesystem::path results;  ///< one <map>.txt per map: the last bake's outcome
};

/// Bakes under the per-user cache, notes and results under the per-user
/// state directory, which a cache clear does not take.
[[nodiscard]] Result<LauncherPaths> launcherPaths();

/// A map's remembered state, shown beside its name.
struct MapResult {
    bool failed = false;
    std::string failure;  ///< why, when failed
};

/// The last outcome recorded for `map`, or nothing when it was never opened.
[[nodiscard]] std::optional<MapResult> readResult(const std::filesystem::path& results, std::string_view map);

/// Record that `map` opened ("baked") or failed and why.
[[nodiscard]] Result<void> writeResult(const std::filesystem::path& results, std::string_view map,
                                       const MapResult& result);

/// A map's notes, or empty when it has none.
[[nodiscard]] std::string readNotes(const std::filesystem::path& notes, std::string_view map);

/// Save a map's notes. Empty notes remove the file, so the directory holds
/// only maps somebody wrote about.
[[nodiscard]] Result<void> writeNotes(const std::filesystem::path& notes, std::string_view map,
                                      std::string_view text);

/// UTA-0190: the file `writeNotes` keeps `map`'s notes in.
[[nodiscard]] std::filesystem::path notesFile(const std::filesystem::path& notes, std::string_view map);

/// UTA-0190: add `line` to the notes `file` on a line of its own, making the
/// file when there is none.
[[nodiscard]] Result<void> appendNote(const std::filesystem::path& file, std::string_view line);

/// `text` without its last UTF-8 character. Unchanged when empty.
[[nodiscard]] std::string withoutLastCharacter(std::string text);

/// `text` broken into lines of at most `width` bytes: at each newline, and
/// between words where a line would run over, or mid-word when one word is
/// longer than `width`. Always at least one line.
[[nodiscard]] std::vector<std::string> wrapText(std::string_view text, std::size_t width);

} // namespace uta::client
