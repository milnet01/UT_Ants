// The ut-bake command line -- docs/specs/UTA-0011-map-baker.md SS 4.8.

#include "Cli.h"

#include "common/Json.h"
#include "core/Jobs.h"
#include "ubake/Bake.h"
#include "ubake/Install.h"
#include "ubake/Name.h"
#include "umat/Material.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace uta::ubake {
namespace {

constexpr int SCHEMA = 1;

constexpr int EXIT_OK = 0;
constexpr int EXIT_FAILED = 1;
constexpr int EXIT_USAGE = 2;

/// ut-dump's escapes, which SS 4.8 names -- tools/common/Json.h's, shared by
/// every tool.
using uta::tools::writeJsonString;

template <class T, class Write>
void writeArray(std::ostream& out, const std::vector<T>& values, Write write) {
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out << ", ";
        write(values[i]);
    }
    out << ']';
}

void usage(std::ostream& err) {
    err << "usage: ut-bake --check <install>\n"
           "       ut-bake --install <install> --out <dir> [--force] <map>\n"
           "       ut-bake --help\n"
           "\n"
           "--check says whether a directory is a usable Unreal Tournament install,\n"
           "and why not. The second form bakes one map into <dir>, named by the\n"
           "SHA-256 of what it was baked from, and reuses a bake already there\n"
           "unless --force is given. Standard output is one JSON object.\n";
}

struct Arguments {
    bool help = false;
    bool force = false;
    std::optional<std::string_view> check;
    std::optional<std::string_view> install;
    std::optional<std::string_view> out;
    std::optional<std::string_view> map;
};

/// The arguments, or nothing after saying on `err` what was wrong with them.
std::optional<Arguments> parse(std::span<const std::string_view> args, std::ostream& err) {
    Arguments parsed;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        const auto takeValue = [&](std::optional<std::string_view>& into) {
            if (into.has_value()) {
                err << "ut-bake: " << arg << " is given twice\n";
                return false;
            }
            if (i + 1 >= args.size()) {
                err << "ut-bake: " << arg << " needs a value\n";
                return false;
            }
            into = args[++i];
            return true;
        };

        if (arg == "--help" || arg == "-h") {
            parsed.help = true;
        } else if (arg == "--force") {
            parsed.force = true;
        } else if (arg == "--check") {
            if (!takeValue(parsed.check)) return std::nullopt;
        } else if (arg == "--install") {
            if (!takeValue(parsed.install)) return std::nullopt;
        } else if (arg == "--out") {
            if (!takeValue(parsed.out)) return std::nullopt;
        } else if (arg.starts_with("-")) {
            err << "ut-bake: unknown option " << arg << "\n";
            return std::nullopt;
        } else if (parsed.map.has_value()) {
            err << "ut-bake: one map at a time; " << arg << " is a second\n";
            return std::nullopt;
        } else {
            parsed.map = arg;
        }
    }

    if (parsed.help) return parsed;
    if (parsed.check.has_value()) {
        if (parsed.install || parsed.out || parsed.map || parsed.force) {
            err << "ut-bake: --check takes an install and nothing else\n";
            return std::nullopt;
        }
        return parsed;
    }
    if (!parsed.install || !parsed.out || !parsed.map) {
        err << "ut-bake: a bake needs --install, --out and a map\n";
        return std::nullopt;
    }
    return parsed;
}

int runCheck(std::string_view install, std::ostream& out, std::ostream& err) {
    const CheckReport report = checkInstall(std::filesystem::path(install));

    out << "{\"schema\": " << SCHEMA << ", \"install\": ";
    writeJsonString(out, install);
    out << ", \"ok\": " << (report.ok ? "true" : "false") << ", \"problems\": ";
    writeArray(out, report.problems, [&out](const Problem& problem) {
        out << "{\"what\": ";
        writeJsonString(out, problem.what);
        out << ", \"why\": ";
        writeJsonString(out, problem.why);
        out << '}';
    });
    out << "}\n";

    for (const Problem& problem : report.problems) err << "ut-bake: " << problem.why << "\n";
    return report.ok ? EXIT_OK : EXIT_FAILED;
}

std::string_view verdictName(Verdict verdict) {
    switch (verdict) {
    case Verdict::Written: return "written";
    case Verdict::Cached: return "cached";
    case Verdict::OverBudget: return "over-budget";
    }
    return "refused"; // unreachable: every enumerator is named above
}

void writeResult(std::ostream& out, const BakeResult& result) {
    const auto writeNumber = [&out](std::uint32_t value) { out << value; };
    out << ", \"rooms\": {\"withoutFootprint\": ";
    writeArray(out, result.rooms.roomsWithoutFootprint, writeNumber);
    out << ", \"refusedZones\": ";
    writeArray(out, result.rooms.refusedZones, writeNumber);
    out << "}, \"budget\": {\"workingSetBytes\": " << result.budget.workingSetBytes
        << ", \"budgetBytes\": " << result.budget.budgetBytes << ", \"byTexture\": ";
    writeArray(out, result.budget.byTexture, [&out](const umat::TextureCost& cost) {
        out << "{\"name\": ";
        writeJsonString(out, cost.name);
        out << ", \"bytes\": " << cost.bytes << '}';
    });
    out << "}, \"skipped\": ";
    writeArray(out, result.skipped, [&out](const SkippedTexture& skipped) {
        out << "{\"material\": ";
        writeJsonString(out, skipped.material);
        out << ", \"why\": ";
        writeJsonString(out, skipped.reason);
        out << '}';
    });
}

int runBake(const Arguments& args, std::ostream& out, std::ostream& err,
            std::uint64_t budgetBytes) {
    JobSystem jobs;
    BakeRequest request;
    request.install = std::filesystem::path(*args.install);
    request.map = std::filesystem::path(*args.map);
    request.outDir = std::filesystem::path(*args.out);
    request.force = args.force;
    request.budgetBytes = budgetBytes;
    const auto outcome = bakeToDirectory(request, jobs);

    out << "{\"schema\": " << SCHEMA << ", \"map\": ";
    writeJsonString(out, *args.map);
    out << ", \"bakerVersion\": ";
    writeJsonString(out, bakerVersion());

    if (!outcome.has_value()) {
        out << ", \"verdict\": \"refused\", \"error\": ";
        writeJsonString(out, outcome.error().message());
        out << "}\n";
        err << "ut-bake: " << outcome.error().message() << "\n";
        return EXIT_FAILED;
    }

    out << ", \"verdict\": \"" << verdictName(outcome->verdict) << "\", \"name\": ";
    writeJsonString(out, outcome->name);
    out << ", \"path\": ";
    writeJsonString(out, detail::utf8(outcome->path));
    if (outcome->result.has_value()) writeResult(out, *outcome->result);
    out << "}\n";

    switch (outcome->verdict) {
    case Verdict::Written:
        err << "ut-bake: wrote " << detail::utf8(outcome->path) << "\n";
        return EXIT_OK;
    case Verdict::Cached:
        err << "ut-bake: already baked: " << detail::utf8(outcome->path) << "\n";
        return EXIT_OK;
    case Verdict::OverBudget: {
        const auto verdict = umat::enforceBudget(outcome->result->budget);
        err << "ut-bake: nothing written: "
            << (verdict.has_value() ? std::string_view("over budget") : verdict.error().message())
            << "\n";
        return EXIT_FAILED;
    }
    }
    return EXIT_FAILED;
}

} // namespace

int runCli(std::span<const std::string_view> args, std::ostream& out, std::ostream& err) {
    return detail::runCli(args, out, err, umat::TEXTURE_BUDGET_BYTES);
}

namespace detail {

int runCli(std::span<const std::string_view> args, std::ostream& out, std::ostream& err,
           std::uint64_t budgetBytes) {
    const std::optional<Arguments> parsed = parse(args, err);
    if (!parsed.has_value()) {
        usage(err);
        return EXIT_USAGE;
    }
    if (parsed->help) {
        usage(err);
        return EXIT_OK;
    }
    if (parsed->check.has_value()) return runCheck(*parsed->check, out, err);
    return runBake(*parsed, out, err, budgetBytes);
}

} // namespace detail

} // namespace uta::ubake
