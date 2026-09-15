// The ut-ants command line -- UTA-0016. The roadmap item settled its shape:
// the install and the map are both named on the command line, and nothing is
// remembered between runs.

#include "Cli.h"

#include <charconv>
#include <string>
#include <system_error>
#include <vector>

namespace uta::client {

void usage(std::ostream& err) {
    err << "usage: ut-ants [--tier <low|medium|high|ultra>] [--frames <n>] [--validation] [--windowed]\n"
           "               <install> <bundle>\n"
           "       ut-ants --help\n"
           "\n"
           "Checks <install> with ut-bake --check, then opens <bundle>, a baked map,\n"
           "fullscreen at the desktop's resolution; --windowed opens a resizable\n"
           "window instead. The mouse looks; W, A, S and D fly; Space rises and Ctrl\n"
           "sinks; Shift flies faster; F turns the flashlight on and off; Escape\n"
           "quits. --frames draws that many frames and exits 0 if every one drew.\n"
           "--validation asks for the Vulkan validation layer.\n"
           "--tier picks the quality tier; without it the game picks one from the\n"
           "graphics card.\n";
}

std::optional<Options> parseArguments(std::span<const std::string_view> args, std::ostream& err) {
    Options options;
    std::vector<std::string_view> positional;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else if (arg == "--validation") {
            options.validation = true;
        } else if (arg == "--windowed") {
            options.windowed = true;
        } else if (arg == "--tier") {
            if (options.tier.has_value()) {
                err << "ut-ants: --tier is given twice\n";
                return std::nullopt;
            }
            if (i + 1 >= args.size()) {
                err << "ut-ants: --tier needs a value\n";
                return std::nullopt;
            }
            const std::string_view value = args[++i];
            options.tier = urender::tierNamed(value);
            if (!options.tier.has_value()) {
                err << "ut-ants: --tier takes low, medium, high or ultra, not " << value << "\n";
                return std::nullopt;
            }
        } else if (arg == "--frames") {
            if (options.frames.has_value()) {
                err << "ut-ants: --frames is given twice\n";
                return std::nullopt;
            }
            if (i + 1 >= args.size()) {
                err << "ut-ants: --frames needs a value\n";
                return std::nullopt;
            }
            const std::string_view value = args[++i];
            std::uint32_t frames = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), frames);
            if (error != std::errc{} || end != value.data() + value.size() || frames == 0) {
                err << "ut-ants: --frames takes a whole number above zero, not " << value << "\n";
                return std::nullopt;
            }
            options.frames = frames;
        } else if (arg.starts_with("-")) {
            err << "ut-ants: unknown option " << arg << "\n";
            return std::nullopt;
        } else {
            positional.push_back(arg);
        }
    }

    if (options.help) return options;
    if (positional.size() != 2) {
        err << "ut-ants: give an install and a bundle, in that order\n";
        return std::nullopt;
    }
    options.install = std::filesystem::path(std::string(positional[0]));
    options.bundle = std::filesystem::path(std::string(positional[1]));
    return options;
}

} // namespace uta::client
