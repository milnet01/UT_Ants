// The ut-ants command line, as a function -- UTA-0016.
//
// Compiled into the client and into the unit tests, so the command line is
// tested without a window.

#pragma once

#include "urender/Renderer.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <ostream>
#include <span>
#include <string_view>

namespace uta::client {

constexpr int EXIT_OK = 0;
constexpr int EXIT_FAILED = 1;
constexpr int EXIT_USAGE = 2;

struct Options {
    bool help = false;
    std::filesystem::path install; ///< checked with `ut-bake --check` (docs/design.md rule 16)
    /// A baked map. Empty: open the map launcher instead (UTA-0170).
    std::filesystem::path bundle;
    /// Draw this many frames and exit, for a run nobody watches. Unset: run
    /// until the window closes.
    std::optional<std::uint32_t> frames;
    bool validation = false; ///< ask urender for the Vulkan validation layer
    /// UTA-0177: draw a material the bake could not make in magenta, not grey.
    bool showMissing = false;
    /// UTA-0153: a resizable window. Unset: borderless fullscreen at the desktop's size.
    bool windowed = false;
    /// UTA-0051: the quality tier. Unset: urender chooses from the device.
    std::optional<urender::Tier> tier;
    /// UTA-0190: the map's notes file, which P appends the camera to. Empty:
    /// the camera goes to standard error only.
    ///
    /// UTA-0191 also takes the capture folder's map name from this file's
    /// stem, because the launcher names it after the map it is opening
    /// (Launcher.cpp passes notesFile(paths.notes, opening)). A run given no
    /// notes falls back to the bundle's own stem, which is a content hash.
    std::filesystem::path notes;
    /// UTA-0191: what ut-bake reported as its `bakerVersion` for this bundle,
    /// which the capture folder records. Empty: unknown, and the capture says
    /// so. Nothing else can supply it -- the bundle does not store it and
    /// docs/design.md rule 2 keeps uta_ubake out of this program's link
    /// closure -- so the launcher passes on what the report told it.
    std::string bakerVersion;
};

/// The usage text, for `err`.
void usage(std::ostream& err);

/// `args` excludes the program name. The options, or nothing after saying on
/// `err` what was wrong with them.
[[nodiscard]] std::optional<Options> parseArguments(std::span<const std::string_view> args,
                                                    std::ostream& err);

} // namespace uta::client
