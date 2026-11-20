// The ut-ants command line -- UTA-0016: the install, and a baked map to open
// it, or no map to open the launcher (UTA-0170).

#include "Cli.h"

#include "FlyCamera.h"
#include "MapList.h"

#include <charconv>
#include <format>
#include <string>
#include <system_error>
#include <vector>

namespace uta::client {

void usage(std::ostream& err) {
    err << "usage: ut-ants [--tier <low|medium|high|ultra>] [--frames <n>] [--validation] [--windowed]\n"
           "               [--show-missing] [--no-added-lamps] [--fov <degrees>] [--notes <file>]\n"
           "               [--no-landing-marker] [--no-take-off-pitch]\n"
           "               [--coyote-time] [--mantle] [--high-step] [--no-gamepad]\n"
           "               [--no-landing-damage]\n"
           "               [--baker-version <text>] [--sounds <bundle>] <install> <bundle>\n"
           "       ut-ants [--tier <low|medium|high|ultra>] [--validation] [--windowed] [--show-missing]\n"
           "               [--no-added-lamps] [--fov <degrees>] [--no-landing-marker]\n"
           "               [--no-take-off-pitch] [--coyote-time] [--mantle] [--high-step]\n"
           "               [--no-gamepad] [--no-landing-damage] <install>\n"
           "       ut-ants --help\n"
           "\n"
           "Given only <install>, opens the map launcher: every playable map in the\n"
           "install, baked when picked and kept for next time, with a notes box per\n"
           "map.\n"
           "Up and Down pick, typing filters the list, Enter opens, Tab moves on to\n"
           "the map's visual name, the map it may duplicate, its notes and back to\n"
           "the list, Ctrl and + or - change the text size, Escape quits. A\n"
           "gamepad's pad picks and its bottom face button opens. Bakes are kept in\n"
           "the per-user cache; notes, marks and results in the per-user state\n"
           "directory, one text file per map.\n"
           "\n"
           "Checks <install> with ut-bake --check, then opens <bundle>, a baked map,\n"
           "fullscreen at the desktop's resolution; --windowed opens a resizable\n"
           "window instead. The mouse looks; W, A, S and D fly; Space rises and Ctrl\n"
           "sinks; Shift flies faster; F turns the flashlight on and off; [ and ]\n"
           "narrow and widen the view; H hides and shows the frame-rate counter in\n"
           "the top-left corner; Escape quits. A gamepad flies too: the left stick moves, the right stick looks,\n"
           "the right trigger rises and the left trigger or shoulder sinks, pressing the left\n"
           "stick flies faster, the top face button turns the flashlight on and off,\n"
           "and the Options or Menu button quits. P, or the pad's Share, Create or\n"
           "View button, writes where the camera is as a line ut-shot reads.\n"
           "G, or pressing the pad's right stick, switches between flying and\n"
           "walking as UT99's player does: the same keys and sticks move, a double\n"
           "tap or the pad's right face button dodges the way you are moving, Space\n"
           "or the bottom face button jumps, Ctrl or the left shoulder ducks, and\n"
           "Shift or pressing the left stick walks.\n"
           "Walking, you carry every stock weapon with full ammo. The left mouse\n"
           "button or the right trigger fires and the right button or the left\n"
           "trigger alt-fires; the sniper rifle's alt fire zooms. 1 to 9 and 0 pick\n"
           "a weapon, and the mouse wheel or the pad's left and right step through\n"
           "them. Hold Q, or the pad's right shoulder, to open the weapon wheel:\n"
           "point at a weapon with the mouse or the right stick and let go to pick\n"
           "it. While it is open the view holds still, nothing fires, and the game\n"
           "runs at a fifth of its speed.\n"
           "T, or the pad's up, stands a target dummy a few metres ahead: it\n"
           "turns from green to red as it is hurt and goes when it is killed. The\n"
           "weapon, its ammo, your health and the last hit's damage show under the\n"
           "frame-rate counter. Shots and explosions are plain shapes for now.\n"
           "--notes appends that line to a file too; the launcher passes the map's\n"
           "notes.\n"
           "F12, or the pad's left face button, saves a capture folder: the frame as\n"
           "shown, the same view without exposure or the tone map, and a text file\n"
           "naming the map, bundle, build and camera. One folder per press, beside\n"
           "the notes in the per-user state directory. The screen flashes once as it\n"
           "draws the second image.\n"
           "--baker-version records which baker made the bundle; the launcher passes\n"
           "what ut-bake reported.\n"
           "--sounds loads the weapon set's sounds from the bundle ut-bake\n"
           "--weapon-sounds wrote, beside the map's own; the launcher passes it.\n"
           "--frames draws that many frames and exits 0 if every one drew.\n"
           "--validation asks for the Vulkan validation layer.\n"
           "--show-missing draws a texture the bake could not convert in magenta\n"
           "rather than grey, so it stands out.\n"
           "--no-added-lamps draws the map without the lamps its recipe adds, as\n"
           "the map was made.\n"
           "--no-gamepad opens no gamepad, so one left connected cannot steer or\n"
           "fire in a run nobody is watching.\n"
           "--no-landing-marker hides the ring that shows, in the air, where you\n"
           "will land.\n"
           "--no-take-off-pitch keeps the view from tipping down to show it.\n"
           "Three assists change how the body moves, so each is off unless turned\n"
           "on: --coyote-time lets a jump start just after walking off a ledge;\n"
           "--mantle climbs onto a ledge in reach while you hold jump and push into\n"
           "it; --high-step walks up steps higher than UT99's.\n"
           "--no-landing-damage lets you land from any height unhurt; UT99 hurts\n"
           "you for a hard landing, and kills you for a very hard one.\n"
           "--tier picks the quality tier; without it the game picks one from the\n"
           "graphics card.\n"
           "--fov opens at that field of view, in degrees across the screen, as\n"
           "UT99 counts it. "
        << FlyCamera::DEFAULT_FOV << ", UT99's own, without it; " << FlyCamera::MIN_FOV << " to "
        << FlyCamera::MAX_FOV << ".\n\n";
    for (const std::string& line : wrapText(DISCLAIMER, 72)) err << line << '\n';
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
        } else if (arg == "--show-missing") {
            options.showMissing = true;
        } else if (arg == "--no-added-lamps") {
            options.addedLamps = false;
        } else if (arg == "--no-landing-marker") {
            options.landingMarker = false;
        } else if (arg == "--no-take-off-pitch") {
            options.takeOffPitch = false;
        } else if (arg == "--coyote-time") {
            options.coyoteTime = true;
        } else if (arg == "--mantle") {
            options.mantle = true;
        } else if (arg == "--high-step") {
            options.highStep = true;
        } else if (arg == "--no-landing-damage") {
            options.landingDamage = false;
        } else if (arg == "--windowed") {
            options.windowed = true;
        } else if (arg == "--no-gamepad") {
            options.gamepads = false;
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
        } else if (arg == "--fov") {
            if (options.fov.has_value()) {
                err << "ut-ants: --fov is given twice\n";
                return std::nullopt;
            }
            if (i + 1 >= args.size()) {
                err << "ut-ants: --fov needs a value\n";
                return std::nullopt;
            }
            const std::string_view value = args[++i];
            double fov = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), fov);
            if (error != std::errc{} || end != value.data() + value.size() || !(fov >= FlyCamera::MIN_FOV)
                || !(fov <= FlyCamera::MAX_FOV)) {
                err << "ut-ants: --fov takes degrees from " << FlyCamera::MIN_FOV << " to " << FlyCamera::MAX_FOV
                    << ", not " << value << "\n";
                return std::nullopt;
            }
            options.fov = fov;
        } else if (arg == "--notes") {
            if (!options.notes.empty()) {
                err << "ut-ants: --notes is given twice\n";
                return std::nullopt;
            }
            if (i + 1 >= args.size() || args[i + 1].empty()) {
                err << "ut-ants: --notes needs a file\n";
                return std::nullopt;
            }
            options.notes = std::filesystem::path(std::string(args[++i]));
        } else if (arg == "--baker-version") {
            if (!options.bakerVersion.empty()) {
                err << "ut-ants: --baker-version is given twice\n";
                return std::nullopt;
            }
            // Empty is refused, as for --notes: it would hide a second one.
            if (i + 1 >= args.size() || args[i + 1].empty()) {
                err << "ut-ants: --baker-version needs a value\n";
                return std::nullopt;
            }
            options.bakerVersion = std::string(args[++i]);
        } else if (arg == "--sounds") {
            if (!options.sounds.empty()) {
                err << "ut-ants: --sounds is given twice\n";
                return std::nullopt;
            }
            if (i + 1 >= args.size() || args[i + 1].empty()) {
                err << "ut-ants: --sounds needs a file\n";
                return std::nullopt;
            }
            options.sounds = std::filesystem::path(std::string(args[++i]));
        } else if (arg.starts_with("-")) {
            err << "ut-ants: unknown option " << arg << "\n";
            return std::nullopt;
        } else {
            positional.push_back(arg);
        }
    }

    if (options.help) return options;
    if (positional.empty() || positional.size() > 2) {
        err << "ut-ants: give an install, and a bundle after it to open one map\n";
        return std::nullopt;
    }
    options.install = std::filesystem::path(std::string(positional[0]));
    if (positional.size() == 2) {
        options.bundle = std::filesystem::path(std::string(positional[1]));
    } else if (options.frames.has_value()) {
        // UTA-0170: the launcher waits for a person, so nothing counts frames.
        err << "ut-ants: --frames needs a bundle; the launcher runs until it is closed\n";
        return std::nullopt;
    } else if (!options.notes.empty()) {
        // UTA-0190: the launcher keeps a notes file per map itself.
        err << "ut-ants: --notes needs a bundle; the launcher keeps its own notes\n";
        return std::nullopt;
    } else if (!options.bakerVersion.empty()) {
        // UTA-0191: it describes one bundle, and the launcher opens many.
        err << "ut-ants: --baker-version needs a bundle; it describes the one being opened\n";
        return std::nullopt;
    } else if (!options.sounds.empty()) {
        // UTA-0478: the launcher bakes the weapon set's sounds itself.
        err << "ut-ants: --sounds needs a bundle; the launcher passes its own\n";
        return std::nullopt;
    }
    return options;
}

std::vector<std::string> inheritedArguments(const Options& options) {
    std::vector<std::string> args;
    if (options.windowed) args.emplace_back("--windowed");
    if (!options.gamepads) args.emplace_back("--no-gamepad");
    if (options.validation) args.emplace_back("--validation");
    if (options.showMissing) args.emplace_back("--show-missing");
    if (!options.addedLamps) args.emplace_back("--no-added-lamps");
    if (!options.landingMarker) args.emplace_back("--no-landing-marker");
    if (!options.takeOffPitch) args.emplace_back("--no-take-off-pitch");
    if (options.coyoteTime) args.emplace_back("--coyote-time");
    if (options.mantle) args.emplace_back("--mantle");
    if (options.highStep) args.emplace_back("--high-step");
    if (!options.landingDamage) args.emplace_back("--no-landing-damage");
    if (options.fov.has_value()) {
        args.emplace_back("--fov");
        args.push_back(std::format("{}", *options.fov)); // the shortest text that reads back exactly
    }
    if (options.tier.has_value()) {
        args.emplace_back("--tier");
        args.emplace_back(urender::tierName(*options.tier));
    }
    return args;
}

} // namespace uta::client
