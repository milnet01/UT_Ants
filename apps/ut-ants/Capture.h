// UTA-0191: what F12 writes, and where.
//
// One folder per press, beside the launcher's notes and results
// (MapList.h's LauncherPaths). It holds the frame as shown, the same view
// drawn without exposure or the tone map so it can be measured, and one text
// file naming everything needed to draw that view again.
//
// This half needs no window and no Vulkan, so the unit tests compile it --
// main.cpp is the SDL half and is run by hand. That split is what lets the
// folder's contents be graded at all: no CI leg has a display
// (docs/specs/UTA-0014-vulkan-draw-path.md SS 4.12), so a test that had to
// present could never run.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include "core/Error.h"

namespace uta::client {

/// What one press records, beside the two images.
///
/// Every field is what was ACTUALLY drawn rather than what was asked for --
/// `tier` comes from the frame's own stats, not from the command line, because
/// a device that does not meet a tier is given a lower one and a capture
/// naming the request would misdescribe the picture beside it.
struct CaptureInfo {
    std::string map;                  ///< the map's name, as the launcher lists it
    std::string bundleHash;           ///< SHA-256 of the bundle's bytes, lowercase hex
    std::uint32_t formatVersion = 0;  ///< the bundle header's own field
    std::string bakerVersion;         ///< empty when unknown -- see writeCapture
    std::string commit;               ///< the build's commit, or "unknown"
    std::string tier;                 ///< the tier the frame was drawn at
    /// The colour target's size, which is what both PNGs are. `readback` copies
    /// the whole target, so this is the image's size whatever the scale below.
    std::uint32_t renderWidth = 0;
    std::uint32_t renderHeight = 0;
    /// UTA-0051 SS 4.4's dynamic resolution: the fraction of the target this
    /// frame actually drew before being upscaled into it. Recorded separately
    /// because the image's size does not show it, so a frame that drew at half
    /// scale and one that drew at full scale are otherwise indistinguishable.
    double renderScale = 1.0;
    std::uint32_t windowWidth = 0;    ///< the window's size in window units
    std::uint32_t windowHeight = 0;
    double lightSeconds = 0.0;        ///< the light time the frame was drawn at
    std::string cameraLine;           ///< ut-shot's own format, as UTA-0190's P writes
};

/// A bundle's identity for a capture: SHA-256 of its bytes, lowercase hex.
///
/// The bytes rather than the file's name, because a bake is content-addressed
/// and the name is therefore already a digest of its INPUTS -- which answers
/// what was asked for rather than what was opened. Take this before the bytes
/// are dropped: main() frees them once the bundle is decoded.
[[nodiscard]] std::string bundleHashHex(std::span<const std::byte> bundleBytes);

/// The folder one press gets: `<map>-<YYYYmmdd-HHMMSS>`, the time in UTC.
///
/// UTC rather than local time so two captures sort in the order they were
/// taken across a daylight-saving change, and so the name does not depend on
/// a time-zone database the platform may not ship.
[[nodiscard]] std::string captureFolderName(std::string_view map,
                                            std::chrono::system_clock::time_point when);

/// The text of `details.txt`.
///
/// One `key: value` a line, so a later session can grep one out without
/// parsing. Separate from writeCapture so a test can read it without a
/// filesystem.
[[nodiscard]] std::string captureDetails(const CaptureInfo& info);

/// Write one capture folder under `captures` and return the folder's path.
///
/// `shownRgba` and `linearRgba` are RGBA8, tightly packed, each exactly
/// `renderWidth * renderHeight * 4` bytes -- the two are the same view, so a
/// size disagreement means one was drawn at a resolution the other was not and
/// is InvalidArgument rather than a folder holding two unrelated pictures.
///
/// `info.bakerVersion` may be empty. The client cannot ask the baker directly:
/// docs/design.md rule 2 keeps uta_ubake out of a runtime target's link
/// closure, and apps/ut-ants/CMakeLists.txt asserts it at configure time. So
/// the value is whatever the launcher captured from ut-bake's own report, and
/// a bundle opened straight from the command line has none. `details.txt`
/// writes "unknown" rather than omitting the line, so its absence is never
/// mistaken for a baker that reported nothing.
[[nodiscard]] Result<std::filesystem::path> writeCapture(const std::filesystem::path& captures,
                                                         const CaptureInfo& info,
                                                         std::span<const std::byte> shownRgba,
                                                         std::span<const std::byte> linearRgba,
                                                         std::chrono::system_clock::time_point when);

} // namespace uta::client
