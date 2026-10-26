// ut-bench's frame workload -- docs/specs/UTA-0129-benchmark-tool.md SS 4.4.

#include "Frame.h"

#include "common/Camera.h"
#include "common/Json.h"
#include "common/Options.h"
#include "core/FileSystem.h"
#include "ubundle/Bundle.h"
#include "ubundle/TileAnswers.h"
#include "urender/Renderer.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

namespace uta::bench {
namespace {

using detail::EXIT_FAILED;
using detail::EXIT_OK;
using detail::EXIT_USAGE;
using detail::FRAME_SCHEMA;

/// Frames drawn at a view before any is timed: the first ones draw its shadow tiles.
constexpr int SETTLING_FRAMES = 4;

using uta::tools::writeJsonString;

struct Arguments {
    urender::Tier tier = urender::Tier::High;
    std::uint32_t width = 1920, height = 1080;
    double renderScale = 1; ///< asked for; the renderer raises it to the tier's floor
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
    tools::OptionValues values("ut-bench", args, err);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "--tier") {
            const std::optional<std::string_view> name = values.take(i);
            if (!name) return std::nullopt;
            const std::optional<urender::Tier> tier = urender::tierNamed(*name);
            if (!tier) {
                err << "ut-bench: --tier takes low, medium, high or ultra\n";
                return std::nullopt;
            }
            parsed.tier = *tier;
        } else if (arg == "--size") {
            const std::optional<std::string_view> size = values.take(i);
            if (!size) return std::nullopt;
            const std::size_t by = size->find('x');
            // UTA-0409: past MAX_DIMENSION the device cannot make the frame,
            // and an unchecked size draws through invalid Vulkan calls.
            if (by == std::string_view::npos || !whole(size->substr(0, by), parsed.width)
                || !whole(size->substr(by + 1), parsed.height) || parsed.width == 0 || parsed.height == 0
                || parsed.width > tools::MAX_DIMENSION || parsed.height > tools::MAX_DIMENSION) {
                err << "ut-bench: --size takes <width>x<height>, each from 1 to " << tools::MAX_DIMENSION << "\n";
                return std::nullopt;
            }
        } else if (arg == "--render-scale") {
            const std::optional<std::string_view> text = values.take(i);
            if (!text) return std::nullopt;
            const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), parsed.renderScale);
            if (error != std::errc{} || end != text->data() + text->size() || !(parsed.renderScale > 0)
                || parsed.renderScale > 1) {
                err << "ut-bench: --render-scale takes a number above 0 and at most 1\n";
                return std::nullopt;
            }
        } else if (arg == "--still") {
            if (!values.takeWhole(i, parsed.still, 1)) return std::nullopt;
        } else if (arg == "--steps") {
            if (!values.takeWhole(i, parsed.steps, 1)) return std::nullopt;
        } else if (arg == "--cameras") {
            if (!(parsed.cameras = values.take(i))) return std::nullopt;
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

/// One camera a line, read by the tools' shared reader (common/Camera.h).
/// Blank lines are skipped; a line that does not read is named on `err` and
/// ends the read with nothing.
std::optional<std::vector<View>> readCameras(const std::filesystem::path& file, double aspect, std::ostream& err) {
    std::ifstream in(file);
    if (!in) {
        err << "ut-bench: " << fs::utf8(file) << " does not open\n";
        return std::nullopt;
    }
    std::vector<View> views;
    for (std::string line; std::getline(in, line);) {
        if (tools::isBlankCameraLine(line)) continue;
        const std::optional<tools::CameraLine> read = tools::readCameraLine(line);
        if (!read || !tools::usableHorizontalFov(read->horizontalFovDegrees)) {
            err << "ut-bench: a camera line does not read: " << line << "\n";
            return std::nullopt;
        }
        View view;
        view.camera = read->camera;
        view.camera.verticalFovDegrees = tools::verticalFovDegrees(read->horizontalFovDegrees, aspect);
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
    /// UTA-0351: each step's GPU time a frame, empty where the queue kept none.
    std::array<std::vector<double>, urender::GPU_PASS_COUNT> passes;
    /// UTA-0352: the host's steps a frame, and what of `milliseconds` neither
    /// the recording nor any GPU step holds -- the submit, the blit out and
    /// the wait. The last is empty where the queue kept no timestamps.
    std::vector<double> prepare, record, submitAndWait;

    void add(const Timed& other) {
        const auto append = [](std::vector<double>& to, const std::vector<double>& from) {
            to.insert(to.end(), from.begin(), from.end());
        };
        append(milliseconds, other.milliseconds);
        shadowTiles += other.shadowTiles;
        for (std::size_t pass = 0; pass < passes.size(); ++pass) append(passes[pass], other.passes[pass]);
        append(prepare, other.prepare);
        append(record, other.record);
        append(submitAndWait, other.submitAndWait);
    }
};

double medianOf(const std::vector<double>& values) { return detail::frameSpreadOf(values).median; }

std::string threePlaces(double value) { return std::format("{:.3f}", value); }

void writeTimed(std::ostream& out, const Timed& timed) {
    const detail::FrameSpread spread = detail::frameSpreadOf(timed.milliseconds);
    const double tiles =
        timed.milliseconds.empty() ? 0 : static_cast<double>(timed.shadowTiles) / static_cast<double>(timed.milliseconds.size());
    out << "\"milliseconds\": {\"min\": " << threePlaces(spread.min) << ", \"median\": " << threePlaces(spread.median)
        << ", \"p99\": " << threePlaces(spread.p99) << ", \"max\": " << threePlaces(spread.max) << "}, \"shadowTiles\": "
        << threePlaces(tiles) << ", \"host\": {\"prepare\": " << threePlaces(medianOf(timed.prepare))
        << ", \"record\": " << threePlaces(medianOf(timed.record)) << ", \"submitAndWait\": "
        << (timed.submitAndWait.empty() ? "null" : threePlaces(medianOf(timed.submitAndWait))) << "}, \"passes\": ";
    if (timed.passes[0].empty()) {
        out << "null";
        return;
    }
    out << '{';
    for (std::size_t pass = 0; pass < urender::GPU_PASS_COUNT; ++pass)
        out << (pass == 0 ? "" : ", ") << '"' << urender::GPU_PASS_NAMES[pass]
            << "\": " << threePlaces(medianOf(timed.passes[pass]));
    out << '}';
}

void tableRow(std::ostream& err, std::string_view what, const Timed& timed) {
    const detail::FrameSpread spread = detail::frameSpreadOf(timed.milliseconds);
    err << std::format("  {:<28} median {:8.3f} ms   99th {:8.3f}   slowest {:8.3f}   over {} frame(s)\n", what,
                       spread.median, spread.p99, spread.max, timed.milliseconds.size());
    err << std::format("    host: prepare {:.3f}  record {:.3f}", medianOf(timed.prepare), medianOf(timed.record));
    if (!timed.submitAndWait.empty()) err << std::format("  submit and wait {:.3f}", medianOf(timed.submitAndWait));
    err << " ms\n";
    if (timed.passes[0].empty()) return;
    err << "    median by step:";
    for (std::size_t pass = 0; pass < urender::GPU_PASS_COUNT; ++pass)
        err << std::format("  {} {:.3f}", urender::GPU_PASS_NAMES[pass], medianOf(timed.passes[pass]));
    err << " ms\n";
}

/// UTA-0352: every stage's statistics, as the driver names them; null where
/// the device reports none.
void writeShaders(std::ostream& out, const std::vector<urender::ShaderStatistics>& shaders) {
    if (shaders.empty()) {
        out << "null";
        return;
    }
    out << '[';
    for (std::size_t s = 0; s < shaders.size(); ++s) {
        out << (s == 0 ? "{\"pipeline\": " : ", {\"pipeline\": ");
        writeJsonString(out, shaders[s].pipeline);
        out << ", \"stage\": ";
        writeJsonString(out, shaders[s].stage);
        out << ", \"statistics\": {";
        for (std::size_t v = 0; v < shaders[s].values.size(); ++v) {
            if (v != 0) out << ", ";
            writeJsonString(out, shaders[s].values[v].first);
            out << ": " << shaders[s].values[v].second;
        }
        out << "}}";
    }
    out << ']';
}

/// The table's lines for the statistics that decide a stage's occupancy. The
/// names are RADV's; another driver's are in the JSON only.
void shaderRows(std::ostream& err, const std::vector<urender::ShaderStatistics>& shaders) {
    if (shaders.empty()) {
        err << "  shader statistics: the device reports none\n";
        return;
    }
    err << "  shader registers, by pipeline and stage:\n";
    for (const urender::ShaderStatistics& shader : shaders) {
        std::string line;
        for (const auto& [name, value] : shader.values)
            if (name == "VGPRs" || name == "Subgroups per SIMD" || name == "Spilled VGPRs")
                line += std::format("  {} {}", name, value);
        if (!line.empty()) err << std::format("    {:<28} {:<20}{}\n", shader.pipeline, shader.stage, line);
    }
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
    auto bundle = ubundle::read(*bytes);
    if (!bundle) {
        err << "ut-bench: " << *parsed->bundle << " did not read: " << bundle.error().message() << "\n";
        return EXIT_FAILED;
    }
    // UTA-0277 SS 4.5: timed as the viewer draws it, with the player's answers.
    for (const std::string& warning : ubundle::applyTileAnswersFile(*bundle)) err << "ut-bench: " << warning << "\n";
    ubundle::applyAddedLamps(*bundle, true); // UTA-0256 SS 4.4: as the game draws it by default
    ubundle::applySun(*bundle);              // UTA-0338 SS 4.2
    *bytes = {};
    const std::optional<std::vector<View>> views =
        readCameras(std::filesystem::path(*parsed->cameras), static_cast<double>(parsed->height) / parsed->width, err);
    if (!views) return EXIT_FAILED;

    urender::Config config;
    config.width = parsed->width;
    config.height = parsed->height;
    config.tier = parsed->tier;
    config.fixedRenderScale = parsed->renderScale; // a frame's time at a scale that moves says nothing
    config.shaderStatistics = true; // UTA-0352: what each pipeline costs in registers
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
            into->prepare.push_back(stats.prepareMilliseconds);
            into->record.push_back(stats.recordMilliseconds);
            if (stats.passesTimed) {
                double gpu = 0;
                for (std::size_t pass = 0; pass < urender::GPU_PASS_COUNT; ++pass) {
                    into->passes[pass].push_back(stats.passMilliseconds[pass]);
                    gpu += stats.passMilliseconds[pass];
                }
                into->submitAndWait.push_back(stats.frameMilliseconds - stats.recordMilliseconds - gpu);
            }
        }
        return true;
    };
    std::vector<Timed> still(views->size()), moving(views->size() - 1);
    for (std::size_t v = 0; v < views->size() && failure.empty(); ++v) {
        const urender::Camera& camera = (*views)[v].camera;
        renderer.cut(); // UTA-0356: each view starts its fog afresh, as ut-shot's do
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
    for (const Timed& timed : still) allStill.add(timed);
    for (const Timed& timed : moving) allMoving.add(timed);

    out << "{\"schema\": " << FRAME_SCHEMA << ", \"workload\": \"frame\", ";
    detail::writeMachineAndBuild(out, build, load);
    out << ", \"device\": ";
    writeJsonString(out, renderer.deviceName());
    out << ", \"tier\": ";
    writeJsonString(out, urender::tierName(parsed->tier));
    // As drawn, not as asked: the renderer raises a fixed scale to the tier's floor.
    const double drawnScale = renderer.lastFrameStats().renderScale;
    out << ", \"width\": " << parsed->width << ", \"height\": " << parsed->height
        << ", \"renderScale\": " << drawnScale << ", \"bundle\": ";
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
    out << "}, \"shaders\": ";
    const std::vector<urender::ShaderStatistics> shaders = renderer.shaderStatistics();
    writeShaders(out, shaders);
    out << "}\n";

    err << *parsed->bundle << ", " << urender::tierName(parsed->tier) << ", " << parsed->width << "x" << parsed->height
        << " at scale " << drawnScale << ", on " << renderer.deviceName() << "\n";
    for (std::size_t v = 0; v < views->size(); ++v) {
        tableRow(err, "still at view " + std::to_string(v), still[v]);
        if (v + 1 < views->size()) tableRow(err, "moving " + std::to_string(v) + " to " + std::to_string(v + 1), moving[v]);
    }
    tableRow(err, "every still frame", allStill);
    if (!moving.empty()) tableRow(err, "every moving frame", allMoving);
    shaderRows(err, shaders);
    return EXIT_OK;
}

} // namespace uta::bench
