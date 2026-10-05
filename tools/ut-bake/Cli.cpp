// The ut-bake command line -- docs/specs/UTA-0011-map-baker.md SS 4.8.

#include "Cli.h"

#include "common/Json.h"
#include "core/FileSystem.h"
#include "core/Jobs.h"
#include "ubake/Bake.h"
#include "ubake/GameTypes.h"
#include "ubake/Install.h"
#include "ubake/Name.h"
#include "umat/Material.h"
#include "urecipe/Lookup.h"

#include <algorithm>
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

/// UTA-0264: a written bake lists this many of its largest textures. A whole
/// list runs to thousands of lines and is read only when a bake is over budget,
/// so that verdict, and --full-budget, still print every texture.
constexpr std::size_t BUDGET_LIST_SHORT = 10;

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
           "       ut-bake --game-types <install>\n"
           "       ut-bake --install <install> --out <dir> [--force] [--fit-budget]\n"
           "               [--texture-cache <dir>] [--full-budget] [--recipe <file>] <map>\n"
           "       ut-bake --help\n"
           "\n"
           "--check says whether a directory is a usable Unreal Tournament install,\n"
           "and why not. The second form bakes one map into <dir>, named by the\n"
           "SHA-256 of what it was baked from, and reuses a bake already there\n"
           "unless --force is given. A map whose textures exceed the budget is refused;\n"
           "with --fit-budget its textures are shrunk until they fit instead, and the\n"
           "bake is named apart from a full one (UTA-0245). --texture-cache keeps\n"
           "made textures in <dir> between bakes, capped at 4 GB, so baking a map\n"
           "again is faster; the output is the same either way (UTA-0148).\n"
           "A written bake lists its 10 largest textures; --full-budget lists every\n"
           "one, as an over-budget bake always does (UTA-0264).\n"
           "--recipe bakes with that recipe file; without it the bake takes the\n"
           "map's recipe from your own recipes folder, else the game's, if either\n"
           "has one (UTA-0113).\n"
           "--game-types lists the game types the install's\n"
           ".int files register, each with the MapPrefix its maps' names start with\n"
           "(UTA-0179). Standard output is one JSON object.\n";
}

struct Arguments {
    bool help = false;
    bool force = false;
    bool fitBudget = false;
    bool fullBudget = false;
    std::optional<std::string_view> textureCache;
    std::optional<std::string_view> recipe;
    std::optional<std::string_view> check;
    std::optional<std::string_view> gameTypes;
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
        } else if (arg == "--fit-budget") {
            parsed.fitBudget = true;
        } else if (arg == "--full-budget") {
            parsed.fullBudget = true;
        } else if (arg == "--texture-cache") {
            if (!takeValue(parsed.textureCache)) return std::nullopt;
        } else if (arg == "--recipe") {
            if (!takeValue(parsed.recipe)) return std::nullopt;
        } else if (arg == "--check") {
            if (!takeValue(parsed.check)) return std::nullopt;
        } else if (arg == "--game-types") {
            if (!takeValue(parsed.gameTypes)) return std::nullopt;
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
    if (parsed.check.has_value() || parsed.gameTypes.has_value()) {
        if ((parsed.check && parsed.gameTypes) || parsed.install || parsed.out || parsed.map || parsed.force
            || parsed.textureCache || parsed.recipe) {
            err << "ut-bake: " << (parsed.check ? "--check" : "--game-types")
                << " takes an install and nothing else\n";
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
    // UTA-0117: the version, and warnings that never change `ok`.
    out << ", \"version\": ";
    if (report.version.has_value()) out << *report.version;
    else out << "null";
    out << ", \"warnings\": ";
    writeArray(out, report.warnings, [&out](const std::string& warning) { writeJsonString(out, warning); });
    out << "}\n";

    for (const Problem& problem : report.problems) err << "ut-bake: " << problem.why << "\n";
    for (const std::string& warning : report.warnings) err << "ut-bake: warning: " << warning << "\n";
    return report.ok ? EXIT_OK : EXIT_FAILED;
}

/// UTA-0179: every game type found with its prefix, the names whose class was
/// not found, and the distinct prefixes -- what the launcher filters by.
int runGameTypes(std::string_view install, std::ostream& out, std::ostream& err) {
    auto opened = Install::open(std::filesystem::path(install));
    out << "{\"schema\": " << SCHEMA << ", \"install\": ";
    writeJsonString(out, install);
    // UTA-0208: the launcher calls this once at start, and compares this with
    // the baker that made each bake to tell a current bake from a stale one.
    out << ", \"bakerVersion\": ";
    writeJsonString(out, bakerVersion());
    if (!opened.has_value()) {
        out << ", \"error\": ";
        writeJsonString(out, opened.error().message());
        out << "}\n";
        err << "ut-bake: " << opened.error().message() << "\n";
        return EXIT_FAILED;
    }
    GameTypes types = readGameTypes(*opened);
    out << ", \"gameTypes\": ";
    writeArray(out, types.found, [&out](const GameType& type) {
        out << "{\"name\": ";
        writeJsonString(out, type.name);
        out << ", \"mapPrefix\": ";
        writeJsonString(out, type.mapPrefix);
        out << '}';
    });
    out << ", \"unresolved\": ";
    writeArray(out, types.unresolved, [&out](const std::string& name) { writeJsonString(out, name); });

    std::vector<std::string> prefixes;
    for (const GameType& type : types.found) {
        const bool seen = std::ranges::any_of(
            prefixes, [&](const std::string& prefix) { return detail::fold(prefix) == detail::fold(type.mapPrefix); });
        if (!seen) prefixes.push_back(type.mapPrefix);
    }
    std::ranges::sort(prefixes, {}, [](const std::string& prefix) { return detail::fold(prefix); });
    out << ", \"mapPrefixes\": ";
    writeArray(out, prefixes, [&out](const std::string& prefix) { writeJsonString(out, prefix); });
    out << "}\n";
    return EXIT_OK;
}

std::string_view verdictName(Verdict verdict) {
    switch (verdict) {
    case Verdict::Written: return "written";
    case Verdict::Cached: return "cached";
    case Verdict::OverBudget: return "over-budget";
    }
    return "refused"; // unreachable: every enumerator is named above
}

/// `listed` caps how many of byTexture's entries are printed; the rest are
/// counted in byTextureOmitted (UTA-0264).
void writeResult(std::ostream& out, const BakeResult& result, std::size_t listed) {
    const auto writeNumber = [&out](std::uint32_t value) { out << value; };
    out << ", \"rooms\": {\"withoutFootprint\": ";
    writeArray(out, result.rooms.roomsWithoutFootprint, writeNumber);
    out << ", \"refusedZones\": ";
    writeArray(out, result.rooms.refusedZones, writeNumber);
    out << "}, \"budget\": {\"workingSetBytes\": " << result.budget.workingSetBytes
        << ", \"budgetBytes\": " << result.budget.budgetBytes << ", \"byTexture\": ";
    const std::vector<umat::TextureCost>& costs = result.budget.byTexture;
    const std::size_t shown = std::min(listed, costs.size());
    const std::vector<umat::TextureCost> listedCosts(costs.begin(),
                                                     costs.begin() + static_cast<std::ptrdiff_t>(shown));
    writeArray(out, listedCosts, [&out](const umat::TextureCost& cost) {
        out << "{\"name\": ";
        writeJsonString(out, cost.name);
        out << ", \"bytes\": " << cost.bytes << '}';
    });
    out << ", \"byTextureOmitted\": " << costs.size() - shown;
    out << "}, \"textureCache\": {\"hits\": " << result.textureCacheHits
        << ", \"misses\": " << result.textureCacheMisses << "}, \"skipped\": ";
    writeArray(out, result.skipped, [&out](const SkippedTexture& skipped) {
        out << "{\"material\": ";
        writeJsonString(out, skipped.material);
        out << ", \"why\": ";
        writeJsonString(out, skipped.reason);
        out << '}';
    });
    // UTA-0263 SS 6: a flame surface with a sheet's flags that made no record.
    out << ", \"skippedFlames\": ";
    writeArray(out, result.skippedFlames, [&out](const SkippedFlame& skipped) {
        out << "{\"surface\": " << skipped.surface << ", \"why\": ";
        writeJsonString(out, skipped.reason);
        out << '}';
    });
    // UTA-0105 SS 6: a liquid made with no liquid look; UTA-0286 SS 6: a
    // non-flame FireTexture made with no fire look.
    for (const auto& [key, list] : {std::pair{"skippedLiquids", &result.skippedLiquids},
                                    std::pair{"skippedFires", &result.skippedFires}}) {
        out << ", \"" << key << "\": ";
        writeArray(out, *list, [&out](const SkippedTexture& skipped) {
            out << "{\"material\": ";
            writeJsonString(out, skipped.material);
            out << ", \"why\": ";
            writeJsonString(out, skipped.reason);
            out << '}';
        });
    }
    // UTA-0113 SS 4.5: a recipe's texture the map does not use.
    out << ", \"recipeUnused\": ";
    writeArray(out, result.recipeUnused, [&out](const std::string& texture) { writeJsonString(out, texture); });
    // UTA-0277 SS 4.5: the materials the bake could not judge, for the player.
    out << ", \"tileQuestions\": ";
    writeArray(out, result.tileQuestions, [&out](const TileQuestion& question) {
        const auto triple = [&out](const std::array<double, 3>& v) {
            out << '[' << v[0] << ", " << v[1] << ", " << v[2] << ']';
        };
        out << "{\"material\": ";
        writeJsonString(out, question.material);
        out << ", \"hash\": ";
        writeJsonString(out, question.hash);
        out << ", \"lines\": " << question.lines << ", \"spots\": " << question.spots
            << ", \"surfaces\": " << question.surfaces << ", \"view\": {\"at\": ";
        triple(question.at);
        out << ", \"normal\": ";
        triple(question.normal);
        out << ", \"extent\": " << question.extent << "}}";
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
    request.fitBudget = args.fitBudget;
    // UTA-0148: only when asked (user, 2026-09-29) -- maps share few
    // textures, so the cache pays on baking one map again, not on a new one.
    if (args.textureCache) request.textureCache = std::filesystem::path(*args.textureCache);
    request.recipes = urecipe::standardSources(
        args.recipe ? std::optional(uta::fs::pathFromUtf8(*args.recipe)) : std::nullopt);
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
    out << ", \"recipe\": ";
    if (outcome->recipe) writeJsonString(out, detail::utf8(*outcome->recipe));
    else out << "null";
    // UTA-0141: warn and carry on. The bake used the file the game would.
    out << ", \"packageClashes\": ";
    writeArray(out, outcome->clashes, [&out](const PackageClash& clash) {
        out << "{\"package\": ";
        writeJsonString(out, clash.package);
        out << ", \"object\": ";
        writeJsonString(out, clash.object);
        out << ", \"used\": ";
        writeJsonString(out, detail::utf8(clash.used));
        out << ", \"shadowed\": ";
        writeArray(out, clash.shadowed,
                   [&out](const std::filesystem::path& file) { writeJsonString(out, detail::utf8(file)); });
        out << '}';
    });
    for (const PackageClash& clash : outcome->clashes) {
        err << "ut-bake: warning: " << clash.object << " is not in " << detail::utf8(clash.used)
            << ", which the game's search order picks for " << clash.package << ", but is in";
        for (const std::filesystem::path& file : clash.shadowed) err << " " << detail::utf8(file);
        err << "\n";
    }
    if (outcome->result.has_value())
        for (const std::string& texture : outcome->result->recipeUnused)
            err << "ut-bake: warning: the recipe names " << texture << ", which this map does not use\n";
    if (outcome->result.has_value())
        writeResult(out, *outcome->result,
                    args.fullBudget || outcome->verdict == Verdict::OverBudget
                        ? outcome->result->budget.byTexture.size()
                        : BUDGET_LIST_SHORT);
    if (outcome->fitted.has_value())
        out << ", \"fitted\": {\"upscaleRounds\": " << outcome->fitted->upscaleRounds
            << ", \"sourceRounds\": " << outcome->fitted->sourceRounds
            << ", \"fits\": " << (outcome->fitted->fits ? "true" : "false") << '}';
    out << "}\n";

    switch (outcome->verdict) {
    case Verdict::Written:
        if (outcome->fitted.has_value())
            err << "ut-bake: textures shrunk to fit the budget: " << outcome->fitted->upscaleRounds
                << " round(s) of upscaling taken back, " << outcome->fitted->sourceRounds
                << " of source detail halved\n";
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
    if (parsed->gameTypes.has_value()) return runGameTypes(*parsed->gameTypes, out, err);
    return runBake(*parsed, out, err, budgetBytes);
}

} // namespace detail

} // namespace uta::ubake
