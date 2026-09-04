// Where files live, and how they are read and written.
//
// docs/design.md rule 15 says configuration and logs "live where the platform
// puts them", and quarantines everything derived from the player's Unreal
// Tournament install under content/. Both are path decisions, and a path
// decision made per call site is made differently each time.
//
// resolveUnder is the trust boundary. unet will name files from a remote
// server and ubake writes under content/, so a path that escapes its root is
// refused here rather than at each call site.

#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

#include "core/Error.h"

namespace uta::fs {

/// Per-user configuration. Linux: $XDG_CONFIG_HOME, else ~/.config, each
/// under "ut-ants". Windows: %APPDATA%\UT_Ants.
[[nodiscard]] Result<std::filesystem::path> configDirectory();

/// Per-user cache -- baked bundles and the host-download cache.
/// Linux: $XDG_CACHE_HOME, else ~/.cache. Windows: %LOCALAPPDATA%\UT_Ants\cache.
[[nodiscard]] Result<std::filesystem::path> cacheDirectory();

/// Per-user logs. Linux: $XDG_STATE_HOME, else ~/.local/state.
/// Windows: %LOCALAPPDATA%\UT_Ants\logs.
[[nodiscard]] Result<std::filesystem::path> logDirectory();
//
// For all three: the variable is honoured as the user set it, wherever it
// points -- core does not police the platform's own configuration. What is
// guaranteed is that every FALLBACK is absolute, so an unset variable can
// never put configuration or logs somewhere relative to the current working
// directory. Windows has no fallback beyond its own variables, so a function
// whose variable is unset there returns NotFound (INV-8).

/// Read a whole file. NotFound when it is absent, InvalidArgument when the
/// path names a directory, IoFailure when the read itself fails.
[[nodiscard]] Result<std::vector<std::byte>> readFile(const std::filesystem::path& path);

/// Write to a temporary in the destination's own directory, then rename over
/// the destination. The destination is never opened for writing, so a crash
/// leaves the old bytes rather than half the new ones. On any failure the
/// temporary is unlinked and the destination is left as it was (INV-7).
[[nodiscard]] Result<void> writeFileAtomically(const std::filesystem::path& path,
                                               std::span<const std::byte> bytes);

/// Join `relative` under `root` and refuse anything that escapes it.
///
/// Absolute inputs, empty inputs, and paths resolving outside `root` --
/// through "..", a symlink, or both -- are InvalidArgument. Comparison is
/// against weakly_canonical of both sides, so a symlink escape is caught
/// along with a lexical one (INV-6).
///
/// This is checked once, not continuously: a symlink swapped between this
/// call and the open defeats it, and core does not pretend to close that.
[[nodiscard]] Result<std::filesystem::path> resolveUnder(
    const std::filesystem::path& root, const std::filesystem::path& relative);

}  // namespace uta::fs
