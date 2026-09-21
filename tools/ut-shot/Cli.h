// The ut-shot command line, as a function -- UTA-0199.
//
// Compiled into the ut-shot binary and into the unit tests, so the options are
// tested without starting a process and without a Vulkan device. Only the
// parsing lives here; drawing needs a device and stays in main.cpp.
//
// UTA-0191's capture folder writes down everything needed to draw its view
// again -- the tier, the render scale, the light time and the size. --from-
// capture reads that file, so a capture redraws to its own linear.png without
// a field being copied out by hand. An explicit option wins over it, so the
// same view can be drawn at another tier on purpose.

#pragma once

#include "urender/Renderer.h"

#include <cstdint>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>

namespace uta::shot {

/// The fields of a capture folder's details.txt that ut-shot can draw with.
/// Every one is optional: a key the file does not carry, or carries in a form
/// that does not read, is left unset rather than guessed at.
struct CaptureDetails {
    std::optional<urender::Tier> tier;
    std::optional<double> renderScale;
    std::optional<double> lightSeconds;
    std::optional<std::uint32_t> width, height;
};

/// Reads the `key: value` lines apps/ut-ants/Capture.cpp's `captureDetails`
/// writes. Unknown keys are ignored, so a field added there does not stop an
/// older ut-shot reading the rest.
[[nodiscard]] CaptureDetails parseCaptureDetails(std::string_view text);

/// What the command line asked for, with --from-capture already folded in.
struct Options {
    std::string bundle, prefix;
    std::uint32_t width = 0, height = 0;
    std::optional<urender::Tier> tier;
    std::optional<double> renderScale;
    /// Set: draw every frame at this light time rather than reading the clock,
    /// so two builds agree on a pulsing light's phase (Renderer::pinLightSeconds).
    std::optional<double> lightSeconds;
    bool linearOutput = false;
    bool probes = true;
    /// Set by --from-capture: read the cameras from this file rather than from
    /// standard input, the folder's camera.txt being the view it recorded.
    std::optional<std::string> cameraFile;
};

/// `args` excludes the program name. Returns the options, or nothing after
/// writing the usage or the reason to `err`. --from-capture reads the folder's
/// details.txt from disk, as ut-origin's command line reads its file.
[[nodiscard]] std::optional<Options> parseOptions(std::span<const std::string_view> args,
                                                  std::ostream& err);

} // namespace uta::shot
