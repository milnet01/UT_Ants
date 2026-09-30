// The ut-bench command line -- docs/specs/UTA-0129-benchmark-tool.md SS 4.3.

#include "Cli.h"

#include "Frame.h"

#include "common/Json.h"
#include "core/FileSystem.h"
#include "core/Jobs.h"
#include "core/Sha256.h"
#include "ubake/Bake.h"
#include "ubake/Name.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <system_error>
#include <thread>

namespace uta::bench {
namespace {

constexpr int SCHEMA = 1;

constexpr int EXIT_OK = 0;
constexpr int EXIT_FAILED = 1;
constexpr int EXIT_USAGE = 2;

using uta::tools::writeJsonString;

void usage(std::ostream& err) {
    err << "usage: ut-bench bake --install <install> --scratch <dir> [--runs <n>]\n"
           "               [--workers <n>] [--texture-cache <dir>] <map>...\n"
           "       ut-bench frame --cameras <file> [--tier <low|medium|high|ultra>]\n"
           "               [--size <width>x<height>] [--still <n>] [--steps <n>] <bundle>\n"
           "       ut-bench --help\n"
           "\n"
           "Bakes each map --runs times (3 unless given), never from a cache, and\n"
           "prints how long each step of the bake took: the smallest, the median and\n"
           "the largest over the runs, and each step's share of the whole. Compare on\n"
           "the smallest. Each run's bundle is written into --scratch, hashed and\n"
           "removed; runs of one map that give different bundles are an error.\n"
           "--workers sets the job system's worker count. --texture-cache is handed\n"
           "to the bake, as ut-bake's is. Standard output is one JSON object, naming\n"
           "the machine and the build beside the figures; a table for a person goes\n"
           "to standard error.\n"
           "\n"
           "frame draws a baked map with no window, from the cameras in <file>, one a\n"
           "line as ut-shot reads them: --still frames at each (60 unless given), then\n"
           "--steps frames moving to the next (120). It prints each view's and each\n"
           "move's frame time -- smallest, median, 99th percentile, largest -- and the\n"
           "shadow tiles redrawn a frame. It needs a Vulkan device. Compare on the\n"
           "median and the 99th percentile. --tier is high and --size 1920x1080 unless\n"
           "given.\n";
}

struct Arguments {
    bool help = false;
    std::optional<std::string_view> install;
    std::optional<std::string_view> scratch;
    std::optional<std::string_view> textureCache;
    unsigned runs = 3;
    unsigned workers = 0; ///< 0 is JobSystem's own default
    std::vector<std::string_view> maps;
};

/// The arguments, or nothing after saying on `err` what was wrong with them.
std::optional<Arguments> parse(std::span<const std::string_view> args, std::ostream& err) {
    Arguments parsed;
    bool bake = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        const auto value = [&]() -> std::optional<std::string_view> {
            if (i + 1 >= args.size()) {
                err << "ut-bench: " << arg << " needs a value\n";
                return std::nullopt;
            }
            return args[++i];
        };
        const auto number = [&](unsigned& into, unsigned least) {
            const std::optional<std::string_view> text = value();
            if (!text) return false;
            unsigned read = 0;
            const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), read);
            if (error != std::errc{} || end != text->data() + text->size() || read < least) {
                err << "ut-bench: " << arg << " takes a whole number of at least " << least << "\n";
                return false;
            }
            into = read;
            return true;
        };

        if (arg == "--help" || arg == "-h") {
            parsed.help = true;
        } else if (arg == "--install") {
            if (!(parsed.install = value())) return std::nullopt;
        } else if (arg == "--scratch") {
            if (!(parsed.scratch = value())) return std::nullopt;
        } else if (arg == "--texture-cache") {
            if (!(parsed.textureCache = value())) return std::nullopt;
        } else if (arg == "--runs") {
            if (!number(parsed.runs, 1)) return std::nullopt;
        } else if (arg == "--workers") {
            if (!number(parsed.workers, 1)) return std::nullopt;
        } else if (arg.starts_with("-")) {
            err << "ut-bench: unknown option " << arg << "\n";
            return std::nullopt;
        } else if (!bake && arg == "bake") {
            bake = true;
        } else {
            parsed.maps.push_back(arg);
        }
    }
    if (parsed.help) return parsed;
    if (!bake) {
        err << "ut-bench: name a workload first: bake or frame\n";
        return std::nullopt;
    }
    if (!parsed.install || !parsed.scratch || parsed.maps.empty()) {
        err << "ut-bench: bake needs --install, --scratch and at least one map\n";
        return std::nullopt;
    }
    return parsed;
}

/// The first "model name" of /proc/cpuinfo, or "unknown" where there is none.
std::string cpuName() {
#if defined(__linux__)
    std::ifstream info("/proc/cpuinfo");
    for (std::string line; std::getline(info, line);) {
        if (!line.starts_with("model name")) continue;
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) break;
        const std::size_t first = line.find_first_not_of(" \t", colon + 1);
        if (first != std::string::npos) return line.substr(first);
    }
#endif
    return "unknown";
}

constexpr std::string_view osName() {
#if defined(__linux__)
    return "Linux";
#elif defined(_WIN32)
    return "Windows";
#else
    return "unknown";
#endif
}

/// The one-minute load average, where the platform has one.
std::optional<double> loadAverage() {
#if defined(__linux__)
    double load = 0;
    if (getloadavg(&load, 1) == 1) return load;
#endif
    return std::nullopt;
}

std::string fixed(double value) { return std::format("{:.6f}", value); }

void writeSpread(std::ostream& out, const detail::Spread& spread) {
    out << "{\"min\": " << fixed(spread.min) << ", \"median\": " << fixed(spread.median) << ", \"max\": "
        << fixed(spread.max) << '}';
}

/// What one map's runs gave.
struct MapReport {
    std::string_view map;
    std::string verdict; ///< "written", "over-budget" or "refused"
    std::string error;   ///< why, when refused
    std::vector<detail::Run> runs;
};

/// Bakes `map` `runs` times. A refusal, or a bundle that cannot be read back or
/// removed, ends the map there.
MapReport measure(std::string_view map, const Arguments& args, JobSystem& jobs, std::ostream& err) {
    MapReport report;
    report.map = map;
    ubake::BakeRequest request;
    request.install = std::filesystem::path(*args.install);
    request.map = std::filesystem::path(map);
    request.outDir = std::filesystem::path(*args.scratch);
    request.force = true; // never served from a cache
    if (args.textureCache) request.textureCache = std::filesystem::path(*args.textureCache);

    const auto refuse = [&report](std::string why) {
        report.verdict = "refused";
        report.error = std::move(why);
        report.runs.clear();
    };
    for (unsigned run = 0; run < args.runs; ++run) {
        const auto began = std::chrono::steady_clock::now();
        auto outcome = ubake::bakeToDirectory(request, jobs);
        detail::Run measured;
        measured.total = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        if (!outcome.has_value()) {
            refuse(std::string(outcome.error().message()));
            return report;
        }
        measured.phases = std::move(outcome->phases);
        if (outcome->verdict == ubake::Verdict::Written) {
            // Hashed and removed at once, so the scratch directory never holds two.
            const auto bytes = fs::readFile(outcome->path);
            std::error_code ec;
            std::filesystem::remove(outcome->path, ec);
            if (!bytes.has_value()) {
                refuse(std::string(bytes.error().message()));
                return report;
            }
            if (ec) {
                refuse("cannot remove " + fs::utf8(outcome->path) + ": " + ec.message());
                return report;
            }
            measured.bundleSha256 = ubake::detail::hex(sha256(*bytes));
        }
        if (run == 0) report.verdict = outcome->verdict == ubake::Verdict::Written ? "written" : "over-budget";
        err << "ut-bench: " << map << " run " << (run + 1) << " of " << args.runs << ": " << fixed(measured.total)
            << " s\n";
        report.runs.push_back(std::move(measured));
    }
    return report;
}

/// A table for a person: each phase by share, largest first, named by its path.
void writeTable(std::ostream& err, const MapReport& report, const detail::MapSummary& summary) {
    struct Row {
        std::string path;
        const detail::PhaseSummary* phase;
    };
    std::vector<Row> rows;
    std::vector<std::string_view> open;
    for (const detail::PhaseSummary& phase : summary.phases) {
        open.resize(std::min<std::size_t>(open.size(), phase.depth));
        open.push_back(phase.name);
        std::string path;
        for (const std::string_view name : open) {
            if (!path.empty()) path += '/';
            path += name;
        }
        rows.push_back({std::move(path), &phase});
    }
    std::ranges::stable_sort(rows, [](const Row& a, const Row& b) { return a.phase->share > b.phase->share; });
    err << report.map << ": " << fixed(summary.total.min) << " s at best over " << report.runs.size() << " run(s)\n";
    for (const Row& row : rows)
        err << std::format("  {:5.1f}%  {:10.4f} s  {}\n", row.phase->share * 100.0, row.phase->seconds.min, row.path);
    const double unattributed = summary.total.min > 0 ? summary.unattributed.min / summary.total.min : 0;
    err << std::format("  {:5.1f}%  {:10.4f} s  (in no phase)\n", unattributed * 100.0, summary.unattributed.min);
}

} // namespace

namespace detail {

std::optional<double> conditions(const BuildInfo& build, std::ostream& err) {
    const std::optional<double> load = loadAverage();
    const unsigned cores = std::thread::hardware_concurrency();
    if (build.buildType != "Release")
        err << "ut-bench: warning: built as " << build.buildType << ", so these are not figures to compare\n";
    if (build.sanitizer != "none")
        err << "ut-bench: warning: built with the " << build.sanitizer << " sanitizer, so these are not figures to compare\n";
    if (load && *load > cores)
        err << "ut-bench: warning: the machine is busy (load " << fixed(*load) << " over " << cores
            << " cores), so these figures are slow by an unknown amount\n";
    return load;
}

void writeMachineAndBuild(std::ostream& out, const BuildInfo& build, std::optional<double> load) {
    out << "\"machine\": {\"cpu\": ";
    writeJsonString(out, cpuName());
    out << ", \"logicalCores\": " << std::thread::hardware_concurrency() << ", \"os\": ";
    writeJsonString(out, osName());
    out << ", \"load1\": " << (load ? fixed(*load) : std::string("null")) << "}, \"build\": {\"compiler\": ";
    writeJsonString(out, build.compiler);
    out << ", \"buildType\": ";
    writeJsonString(out, build.buildType);
    out << ", \"sanitizer\": ";
    writeJsonString(out, build.sanitizer);
    out << ", \"commit\": ";
    writeJsonString(out, build.commit);
    out << '}';
}

Spread spreadOf(std::vector<double> values) {
    if (values.empty()) return {};
    std::ranges::sort(values);
    const std::size_t middle = values.size() / 2;
    const double median = values.size() % 2 == 1 ? values[middle] : (values[middle - 1] + values[middle]) / 2.0;
    return {values.front(), median, values.back()};
}

MapSummary summarise(const std::vector<Run>& runs) {
    struct Seen {
        std::string path;
        PhaseSummary phase;
        std::vector<double> seconds;
    };
    MapSummary summary;
    std::vector<Seen> seen;
    std::vector<double> totals;
    std::vector<double> unattributed;
    for (const Run& run : runs) {
        double top = 0;
        std::vector<std::string_view> open; // the names around the phase, by depth
        for (const Phase& phase : run.phases) {
            if (phase.depth == 0) top += phase.seconds;
            open.resize(std::min<std::size_t>(open.size(), phase.depth));
            open.push_back(phase.name);
            std::string path;
            for (const std::string_view name : open) {
                path += '/';
                path += name;
            }
            auto at = std::ranges::find(seen, path, &Seen::path);
            if (at == seen.end()) {
                seen.push_back({path, {phase.name, phase.depth, phase.calls, {}, 0}, {}});
                at = seen.end() - 1;
            }
            at->seconds.push_back(phase.seconds);
        }
        totals.push_back(run.total);
        unattributed.push_back(run.total - top);
        if (run.bundleSha256 != runs.front().bundleSha256) summary.identical = false;
    }
    summary.total = spreadOf(std::move(totals));
    summary.unattributed = spreadOf(std::move(unattributed));
    for (Seen& entry : seen) {
        entry.phase.seconds = spreadOf(std::move(entry.seconds));
        entry.phase.share = summary.total.min > 0 ? entry.phase.seconds.min / summary.total.min : 0;
        summary.phases.push_back(std::move(entry.phase));
    }
    return summary;
}

} // namespace detail

int runCli(std::span<const std::string_view> args, std::ostream& out, std::ostream& err, const BuildInfo& build) {
    // SS 4.4: the frame workload has its own arguments, after its name.
    if (!args.empty() && args.front() == "frame") {
        const int code = runFrame(args.subspan(1), out, err, build);
        if (code == EXIT_USAGE) usage(err);
        return code;
    }
    const std::optional<Arguments> parsed = parse(args, err);
    if (!parsed.has_value()) {
        usage(err);
        return EXIT_USAGE;
    }
    if (parsed->help) {
        usage(err);
        return EXIT_OK;
    }

    const std::optional<double> load = detail::conditions(build, err);

    JobSystem jobs(parsed->workers);
    std::vector<MapReport> reports;
    for (const std::string_view map : parsed->maps) reports.push_back(measure(map, *parsed, jobs, err));

    out << "{\"schema\": " << SCHEMA << ", \"workload\": \"bake\", ";
    detail::writeMachineAndBuild(out, build, load);
    out << ", \"bakerVersion\": ";
    writeJsonString(out, ubake::bakerVersion());
    out << ", \"workers\": " << jobs.workerCount() << ", \"runs\": " << parsed->runs
        << ", \"textureCache\": " << (parsed->textureCache ? "true" : "false") << ", \"maps\": [";

    bool failed = false;
    for (std::size_t m = 0; m < reports.size(); ++m) {
        const MapReport& report = reports[m];
        if (m != 0) out << ", ";
        out << "{\"map\": ";
        writeJsonString(out, report.map);
        out << ", \"verdict\": ";
        writeJsonString(out, report.verdict);
        if (report.runs.empty()) {
            failed = true;
            out << ", \"error\": ";
            writeJsonString(out, report.error);
            out << '}';
            err << "ut-bench: " << report.map << ": " << report.error << "\n";
            continue;
        }
        const detail::MapSummary summary = detail::summarise(report.runs);
        if (!report.runs.front().bundleSha256.empty()) {
            out << ", \"bundleSha256\": ";
            writeJsonString(out, report.runs.front().bundleSha256);
            out << ", \"identical\": " << (summary.identical ? "true" : "false");
            if (!summary.identical) {
                failed = true;
                err << "ut-bench: " << report.map << ": its runs gave different bundles\n";
            }
        }
        out << ", \"total\": ";
        writeSpread(out, summary.total);
        out << ", \"unattributed\": ";
        writeSpread(out, summary.unattributed);
        out << ", \"phases\": [";
        for (std::size_t p = 0; p < summary.phases.size(); ++p) {
            const detail::PhaseSummary& phase = summary.phases[p];
            if (p != 0) out << ", ";
            out << "{\"name\": ";
            writeJsonString(out, phase.name);
            out << ", \"depth\": " << phase.depth << ", \"calls\": " << phase.calls << ", \"seconds\": ";
            writeSpread(out, phase.seconds);
            out << ", \"share\": " << fixed(phase.share) << '}';
        }
        out << "]}";
        writeTable(err, report, summary);
    }
    out << "]}\n";
    return failed ? EXIT_FAILED : EXIT_OK;
}

} // namespace uta::bench
