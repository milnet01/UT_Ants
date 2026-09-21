// Cli.h says what grades this.

#include "Cli.h"

#include "core/FileSystem.h"

#include <charconv>
#include <cmath>
#include <filesystem>
#include <string>

namespace uta::shot {

namespace {

/// Trim ASCII spaces and tabs from both ends. details.txt is written by
/// std::format with no padding, but a field is easier to read back than to
/// prove nobody ever hand-edits one.
std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

bool parseUnsigned(std::string_view text, std::uint32_t& out) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    return error == std::errc{} && end == text.data() + text.size();
}

/// from_chars for double is not in libstdc++ before GCC 11 for every build we
/// target, and stod throws; this keeps the parse total and the failure silent,
/// which is what leaves a bad field unset rather than guessed at.
bool parseDouble(std::string_view text, double& out) {
    const std::string owned(text);
    std::size_t consumed = 0;
    try {
        out = std::stod(owned, &consumed);
    } catch (...) {
        return false;
    }
    return consumed == owned.size() && std::isfinite(out);
}

/// A render scale the renderer can use. Config clamps to the tier's floor and
/// to 1, so the boundary this refuses is the one clamping cannot rescue.
bool usableScale(double scale) { return std::isfinite(scale) && scale > 0; }

void usage(std::ostream& err) {
    err << "usage: ut-shot [options] <bundle> <width> <height> <out prefix> < cameras\n"
           "       ut-shot --from-capture <folder> [options] <bundle> <out prefix>\n"
           "\n"
           "Each line of standard input is one camera: x y z pitch yaw roll\n"
           "horizontalFovDegrees, in UT99 units and angles. Writes\n"
           "<out prefix>-<line>.ppm for each, counting from 0.\n"
           "\n"
           "--linear        skip exposure and the tone map, writing linear light.\n"
           "--no-probes     draw without the baked indirect light.\n"
           "--tier <name>   low, medium, high or ultra. Default high.\n"
           "--render-scale <s>  draw at this share of the target and upscale.\n"
           "--light-time <s>    pin a pulsing light's phase to this many seconds.\n"
           "--from-capture <folder>\n"
           "                take the tier, render scale, light time and size from\n"
           "                a capture folder's details.txt, and the cameras from\n"
           "                its camera.txt. An option given as well wins over it,\n"
           "                so the same view can be drawn at another tier.\n";
}

} // namespace

CaptureDetails parseCaptureDetails(std::string_view text) {
    CaptureDetails details;
    while (!text.empty()) {
        const auto lineEnd = text.find('\n');
        const std::string_view line = trim(text.substr(0, lineEnd));
        text = lineEnd == std::string_view::npos ? std::string_view{} : text.substr(lineEnd + 1);

        const auto colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        const std::string_view key = trim(line.substr(0, colon));
        const std::string_view value = trim(line.substr(colon + 1));
        if (value.empty()) continue;

        if (key == "tier") {
            details.tier = urender::tierNamed(value);
        } else if (key == "render-scale") {
            if (double scale = 0; parseDouble(value, scale) && usableScale(scale))
                details.renderScale = scale;
        } else if (key == "light-seconds") {
            if (double seconds = 0; parseDouble(value, seconds)) details.lightSeconds = seconds;
        } else if (key == "render-size") {
            // "<width>x<height>", as captureDetails writes it.
            const auto by = value.find('x');
            if (by == std::string_view::npos) continue;
            std::uint32_t width = 0, height = 0;
            if (parseUnsigned(value.substr(0, by), width) &&
                parseUnsigned(value.substr(by + 1), height) && width > 0 && height > 0) {
                details.width = width;
                details.height = height;
            }
        }
    }
    return details;
}

std::optional<Options> parseOptions(std::span<const std::string_view> args, std::ostream& err) {
    Options options;
    std::optional<std::string> captureFolder;
    std::size_t index = 0;

    // A value-taking option needs the argument after it, so the count is
    // checked here rather than by a bound on the loop.
    const auto value = [&](std::string_view name) -> std::optional<std::string_view> {
        if (index + 1 >= args.size()) {
            err << "ut-shot: " << name << " needs a value\n";
            return std::nullopt;
        }
        return args[++index];
    };

    for (; index < args.size() && args[index].starts_with("--"); ++index) {
        const std::string_view flag = args[index];
        if (flag == "--linear") {
            options.linearOutput = true;
        } else if (flag == "--no-probes") {
            options.probes = false;
        } else if (flag == "--tier") {
            const auto name = value(flag);
            if (!name) return std::nullopt;
            options.tier = urender::tierNamed(*name);
            if (!options.tier) {
                err << "ut-shot: " << *name << " is not a tier; try low, medium, high or ultra\n";
                return std::nullopt;
            }
        } else if (flag == "--render-scale") {
            const auto text = value(flag);
            if (!text) return std::nullopt;
            double scale = 0;
            if (!parseDouble(*text, scale) || !usableScale(scale)) {
                err << "ut-shot: --render-scale wants a number above 0, given " << *text << "\n";
                return std::nullopt;
            }
            options.renderScale = scale;
        } else if (flag == "--light-time") {
            const auto text = value(flag);
            if (!text) return std::nullopt;
            double seconds = 0;
            if (!parseDouble(*text, seconds)) {
                err << "ut-shot: --light-time wants a number of seconds, given " << *text << "\n";
                return std::nullopt;
            }
            options.lightSeconds = seconds;
        } else if (flag == "--from-capture") {
            const auto folder = value(flag);
            if (!folder) return std::nullopt;
            captureFolder = std::string(*folder);
        } else {
            err << "ut-shot: " << flag << " is not an option\n";
            usage(err);
            return std::nullopt;
        }
    }

    const std::span<const std::string_view> rest = args.subspan(index);

    // --from-capture carries the size, so the two sizes are not typed out
    // again; without it they are where they have always been.
    if (captureFolder) {
        if (rest.size() != 2) {
            usage(err);
            return std::nullopt;
        }
        options.bundle = std::string(rest[0]);
        options.prefix = std::string(rest[1]);

        const std::filesystem::path folder(*captureFolder);
        const auto bytes = fs::readFile(folder / "details.txt");
        if (!bytes) {
            err << "ut-shot: " << *captureFolder
                << " does not read as a capture folder: " << bytes.error().message() << "\n";
            return std::nullopt;
        }
        const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        const CaptureDetails details = parseCaptureDetails(text);

        // Explicit options win, so a capture can be redrawn at another tier.
        if (!options.tier) options.tier = details.tier;
        if (!options.renderScale) options.renderScale = details.renderScale;
        if (!options.lightSeconds) options.lightSeconds = details.lightSeconds;

        if (!details.width || !details.height) {
            err << "ut-shot: " << *captureFolder
                << "/details.txt carries no readable render-size, so the frame's size is"
                   " unknown\n";
            return std::nullopt;
        }
        options.width = *details.width;
        options.height = *details.height;
        options.cameraFile = (folder / "camera.txt").string();
        return options;
    }

    if (rest.size() != 4) {
        usage(err);
        return std::nullopt;
    }
    options.bundle = std::string(rest[0]);
    if (!parseUnsigned(rest[1], options.width) || !parseUnsigned(rest[2], options.height) ||
        options.width == 0 || options.height == 0) {
        usage(err);
        return std::nullopt;
    }
    options.prefix = std::string(rest[3]);
    return options;
}

} // namespace uta::shot
