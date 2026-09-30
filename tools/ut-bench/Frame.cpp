// ut-bench's frame workload -- docs/specs/UTA-0129-benchmark-tool.md SS 4.4.

#include "Frame.h"

#include "common/Json.h"
#include "core/FileSystem.h"
#include "ubundle/Bundle.h"
#include "urender/Renderer.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>

namespace uta::bench {
namespace {

constexpr int EXIT_OK = 0;
constexpr int EXIT_FAILED = 1;
constexpr int EXIT_USAGE = 2;

/// Frames drawn at a view before any is timed: the first ones draw its shadow tiles.
constexpr int SETTLING_FRAMES = 4;

using uta::tools::writeJsonString;

struct Arguments {
    urender::Tier tier = urender::Tier::High;
    std::uint32_t width = 1920, height = 1080;
    unsigned still = 60;
    unsigned steps = 120;
    std::optional<std::string_view> cameras;
    std::optional<std::string_view> bundle;
};

bool whole(std::string_view text, std::uint32_t& into) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), into);
    return error == std::errc{} && end == text.data() + text.size();
}

std::optional<Arguments> parse(std::span<const std::string_view> args, std::ostream& err) {
    Arguments parsed;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        const auto value = [&]() -> std::optional<std::string_view> {
            if (i + 1 >= args.size()) {
                err << "ut-bench: " << arg << " needs a value\n";
                return std::nullopt;
            }
            return args[++i];
        };
        const auto count = [&](unsigned& into) {
            const std::optional<std::string_view> text = value();
            std::uint32_t read = 0;
            if (!text) return false;
            if (!whole(*text, read) || read < 1) {
                err << "ut-bench: " << arg << " takes a whole number of at least 1\n";
                return false;
            }
            into = read;
            return true;
        };
        if (arg == "--tier") {
            const std::optional<std::string_view> name = value();
            if (!name) return std::nullopt;
            const std::optional<urender::Tier> tier = urender::tierNamed(*name);
            if (!tier) {
                err << "ut-bench: --tier takes low, medium, high or ultra\n";
                return std::nullopt;
            }
            parsed.tier = *tier;
        } else if (arg == "--size") {
            const std::optional<std::string_view> size = value();
            if (!size) return std::nullopt;
            const std::size_t by = size->find('x');
            if (by == std::string_view::npos || !whole(size->substr(0, by), parsed.width)
                || !whole(size->substr(by + 1), parsed.height) || parsed.width == 0 || parsed.height == 0) {
                err << "ut-bench: --size takes <width>x<height>, each at least 1\n";
                return std::nullopt;
            }
        } else if (arg == "--still") {
            if (!count(parsed.still)) return std::nullopt;
        } else if (arg == "--steps") {
            if (!count(parsed.steps)) return std::nullopt;
        } else if (arg == "--cameras") {
            if (!(parsed.cameras = value())) return std::nullopt;
        } else if (arg.starts_with("-")) {
            err << "ut-bench: unknown option " << arg << "\n";
            return std::nullopt;
        } else if (parsed.bundle) {
            err << "ut-bench: one bundle at a time; " << arg << " is a second\n";
            return std::nullopt;
        } else {
            parsed.bundle = arg;
        }
    }
    if (!parsed.cameras || !parsed.bundle) {
        err << "ut-bench: frame needs --cameras and a bundle\n";
        return std::nullopt;
    }
    return parsed;
}

struct View {
    std::string line; ///< as the file gave it
    urender::Camera camera;
};

/// One camera a line, as ut-shot reads one: x y z pitch yaw roll
/// horizontalFovDegrees. Blank lines are skipped; a line that does not read is
/// named on `err` and ends the read with nothing.
std::optional<std::vector<View>> readCameras(const std::filesystem::path& file, double aspect, std::ostream& err) {
    std::ifstream in(file);
    if (!in) {
        err << "ut-bench: " << fs::utf8(file) << " does not open\n";
        return std::nullopt;
    }
    std::vector<View> views;
    for (std::string line; std::getline(in, line);) {
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        std::istringstream fields(line);
        View view;
        double horizontalFov = 0;
        if (!(fields >> view.camera.location[0] >> view.camera.location[1] >> view.camera.location[2]
              >> view.camera.rotation[0] >> view.camera.rotation[1] >> view.camera.rotation[2] >> horizontalFov)
            || !(horizontalFov > 0 && horizontalFov < 180)) {
            err << "ut-bench: a camera line does not read: " << line << "\n";
            return std::nullopt;
        }
        // UT99's field of view is horizontal; the renderer takes a vertical one.
        view.camera.verticalFovDegrees = static_cast<float>(
            std::atan(std::tan(horizontalFov * std::numbers::pi / 360.0) * aspect) * 360.0 / std::numbers::pi);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        view.line = std::move(line);
        views.push_back(std::move(view));
    }
    if (views.empty()) {
        err << "ut-bench: " << fs::utf8(file) << " holds no camera\n";
        return std::nullopt;
    }
    return views;
}

/// The camera `t` of the way from `from` to `to`: a straight move, and the
/// short way round each 65536-unit turn.
urender::Camera between(const urender::Camera& from, const urender::Camera& to, double t) {
    urender::Camera camera = from;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        camera.location[axis] =
            static_cast<float>(from.location[axis] + t * (static_cast<double>(to.location[axis]) - from.location[axis]));
        double turn = std::fmod(static_cast<double>(to.rotation[axis]) - from.rotation[axis], 65536.0);
        if (turn > 32768) turn -= 65536;
        if (turn < -32768) turn += 65536;
        camera.rotation[axis] = static_cast<std::int32_t>(std::lround(from.rotation[axis] + t * turn));
    }
    camera.verticalFovDegrees =
        static_cast<float>(from.verticalFovDegrees + t * (static_cast<double>(to.verticalFovDegrees) - from.verticalFovDegrees));
    return camera;
}

struct Timed {
    std::vector<double> milliseconds;
    std::uint64_t shadowTiles = 0;
};

std::string fixed(double value) { return std::format("{:.3f}", value); }

void writeTimed(std::ostream& out, const Timed& timed) {
    const detail::FrameSpread spread = detail::frameSpreadOf(timed.milliseconds);
    const double tiles =
        timed.milliseconds.empty() ? 0 : static_cast<double>(timed.shadowTiles) / static_cast<double>(timed.milliseconds.size());
    out << "\"milliseconds\": {\"min\": " << fixed(spread.min) << ", \"median\": " << fixed(spread.median)
        << ", \"p99\": " << fixed(spread.p99) << ", \"max\": " << fixed(spread.max) << "}, \"shadowTiles\": "
        << fixed(tiles);
}

void tableRow(std::ostream& err, std::string_view what, const Timed& timed) {
    const detail::FrameSpread spread = detail::frameSpreadOf(timed.milliseconds);
    err << std::format("  {:<28} median {:8.3f} ms   99th {:8.3f}   slowest {:8.3f}   over {} frame(s)\n", what,
                       spread.median, spread.p99, spread.max, timed.milliseconds.size());
}

} // namespace

namespace detail {

FrameSpread frameSpreadOf(std::vector<double> milliseconds) {
    if (milliseconds.empty()) return {};
    std::ranges::sort(milliseconds);
    const std::size_t n = milliseconds.size();
    const double median = n % 2 == 1 ? milliseconds[n / 2] : (milliseconds[n / 2 - 1] + milliseconds[n / 2]) / 2.0;
    const auto rank = static_cast<std::size_t>(std::ceil(0.99 * static_cast<double>(n)));
    return {milliseconds.front(), median, milliseconds[std::clamp<std::size_t>(rank, 1, n) - 1], milliseconds.back()};
}

} // namespace detail

int runFrame(std::span<const std::string_view> args, std::ostream& out, std::ostream& err, const BuildInfo& build) {
    const std::optional<Arguments> parsed = parse(args, err);
    if (!parsed) return EXIT_USAGE;

    auto bytes = fs::readFile(std::filesystem::path(*parsed->bundle));
    if (!bytes) {
        err << "ut-bench: " << bytes.error().message() << "\n";
        return EXIT_FAILED;
    }
    const auto bundle = ubundle::read(*bytes);
    if (!bundle) {
        err << "ut-bench: " << *parsed->bundle << " did not read: " << bundle.error().message() << "\n";
        return EXIT_FAILED;
    }
    *bytes = {};
    const std::optional<std::vector<View>> views =
        readCameras(std::filesystem::path(*parsed->cameras), static_cast<double>(parsed->height) / parsed->width, err);
    if (!views) return EXIT_FAILED;

    urender::Config config;
    config.width = parsed->width;
    config.height = parsed->height;
    config.tier = parsed->tier;
    config.fixedRenderScale = 1.0; // a frame's time at a scale that moves says nothing
    auto created = urender::Renderer::create(config);
    if (!created) {
        err << "ut-bench: the renderer did not start: " << created.error().message() << "\n";
        return EXIT_FAILED;
    }
    urender::Renderer renderer = std::move(*created);
    renderer.pinLightSeconds(0); // a pulsing light must not move a frame's cost between runs

    const std::optional<double> load = detail::conditions(build, err);

    std::string failure;
    const auto draw = [&](const urender::Camera& camera, Timed* into) {
        if (const auto drawn = renderer.draw(*bundle, camera); !drawn) {
            failure = std::string(drawn.error().message());
            return false;
        }
        if (into != nullptr) {
            const urender::FrameStats stats = renderer.lastFrameStats();
            into->milliseconds.push_back(stats.frameMilliseconds);
            into->shadowTiles += stats.renderedShadowTiles;
        }
        return true;
    };
    std::vector<Timed> still(views->size()), moving(views->size() - 1);
    for (std::size_t v = 0; v < views->size() && failure.empty(); ++v) {
        const urender::Camera& camera = (*views)[v].camera;
        for (int frame = 0; frame < SETTLING_FRAMES && failure.empty(); ++frame) draw(camera, nullptr);
        for (unsigned frame = 0; frame < parsed->still && failure.empty(); ++frame) draw(camera, &still[v]);
        if (v + 1 == views->size()) break;
        for (unsigned step = 1; step <= parsed->steps && failure.empty(); ++step)
            draw(between(camera, (*views)[v + 1].camera, static_cast<double>(step) / parsed->steps), &moving[v]);
    }
    if (!failure.empty()) {
        err << "ut-bench: a frame did not draw: " << failure << "\n";
        return EXIT_FAILED;
    }

    Timed allStill, allMoving;
    for (const Timed& timed : still) {
        allStill.milliseconds.insert(allStill.milliseconds.end(), timed.milliseconds.begin(), timed.milliseconds.end());
        allStill.shadowTiles += timed.shadowTiles;
    }
    for (const Timed& timed : moving) {
        allMoving.milliseconds.insert(allMoving.milliseconds.end(), timed.milliseconds.begin(), timed.milliseconds.end());
        allMoving.shadowTiles += timed.shadowTiles;
    }

    out << "{\"schema\": 1, \"workload\": \"frame\", ";
    detail::writeMachineAndBuild(out, build, load);
    out << ", \"device\": ";
    writeJsonString(out, renderer.deviceName());
    out << ", \"tier\": ";
    writeJsonString(out, urender::tierName(parsed->tier));
    out << ", \"width\": " << parsed->width << ", \"height\": " << parsed->height << ", \"bundle\": ";
    writeJsonString(out, *parsed->bundle);
    out << ", \"stillFrames\": " << parsed->still << ", \"moveFrames\": " << parsed->steps << ", \"views\": [";
    for (std::size_t v = 0; v < views->size(); ++v) {
        if (v != 0) out << ", ";
        out << "{\"camera\": ";
        writeJsonString(out, (*views)[v].line);
        out << ", ";
        writeTimed(out, still[v]);
        out << '}';
    }
    out << "], \"moves\": [";
    for (std::size_t m = 0; m < moving.size(); ++m) {
        if (m != 0) out << ", ";
        out << "{\"from\": " << m << ", \"to\": " << (m + 1) << ", ";
        writeTimed(out, moving[m]);
        out << '}';
    }
    out << "], \"still\": {";
    writeTimed(out, allStill);
    out << "}, \"moving\": {";
    writeTimed(out, allMoving);
    out << "}}\n";

    err << *parsed->bundle << ", " << urender::tierName(parsed->tier) << ", " << parsed->width << "x" << parsed->height
        << ", on " << renderer.deviceName() << "\n";
    for (std::size_t v = 0; v < views->size(); ++v) {
        tableRow(err, "still at view " + std::to_string(v), still[v]);
        if (v + 1 < views->size()) tableRow(err, "moving " + std::to_string(v) + " to " + std::to_string(v + 1), moving[v]);
    }
    tableRow(err, "every still frame", allStill);
    if (!moving.empty()) tableRow(err, "every moving frame", allMoving);
    return EXIT_OK;
}

} // namespace uta::bench
