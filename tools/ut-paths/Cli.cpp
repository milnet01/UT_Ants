// The ut-paths command line -- docs/specs/UTA-0121-bot-path-seeds.md SS 4.2,
// INV-9.

#include "Cli.h"

#include "Seeds.h"
#include "common/Json.h"
#include "core/FileSystem.h"
#include "core/Md5.h"
#include "ubake/Install.h"
#include "ubake/Name.h"
#include "upkg/Package.h"

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace uta::paths {
namespace {

namespace stdfs = std::filesystem;

constexpr int SCHEMA = 1;

constexpr int EXIT_OK = 0;
constexpr int EXIT_FAILED = 1;
constexpr int EXIT_USAGE = 2;

constexpr std::string_view EXIT_OFF_NET = "EXIT_OFF_NET";
constexpr std::string_view PARTITIONED = "PARTITIONED";

using uta::tools::writeJsonString;

void usage(std::ostream& err) {
    err << "usage: ut-paths --install <install> --census <tsv> --out <dir> [<map> ...]\n"
           "       ut-paths --help\n"
           "\n"
           "Proposes bot path nodes for every EXIT_OFF_NET and PARTITIONED map of\n"
           "UT_MonsterHunt's census, or for the maps named, writing <dir>/<map>.json.\n"
           "A map with no file in <install>/Maps is skipped. Standard output is one\n"
           "JSON object, also written to <dir>/ut-paths-summary.json.\n";
}

struct Arguments {
    bool help = false;
    std::optional<std::string_view> install;
    std::optional<std::string_view> census;
    std::optional<std::string_view> out;
    std::vector<std::string_view> maps;
};

/// The arguments, or nothing after saying on `err` what was wrong with them.
std::optional<Arguments> parse(std::span<const std::string_view> args, std::ostream& err) {
    Arguments parsed;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        const auto takeValue = [&](std::optional<std::string_view>& into) {
            if (into.has_value()) {
                err << "ut-paths: " << arg << " is given twice\n";
                return false;
            }
            if (i + 1 >= args.size()) {
                err << "ut-paths: " << arg << " needs a value\n";
                return false;
            }
            into = args[++i];
            return true;
        };

        if (arg == "--help" || arg == "-h") {
            parsed.help = true;
        } else if (arg == "--install") {
            if (!takeValue(parsed.install)) return std::nullopt;
        } else if (arg == "--census") {
            if (!takeValue(parsed.census)) return std::nullopt;
        } else if (arg == "--out") {
            if (!takeValue(parsed.out)) return std::nullopt;
        } else if (arg.starts_with("-")) {
            err << "ut-paths: unknown option " << arg << "\n";
            return std::nullopt;
        } else {
            parsed.maps.push_back(arg);
        }
    }
    if (parsed.help) return parsed;
    if (!parsed.install || !parsed.census || !parsed.out) {
        err << "ut-paths: --install, --census and --out are all needed\n";
        return std::nullopt;
    }
    return parsed;
}

struct Row {
    std::string map;
    std::string group;
};

std::vector<std::string_view> fields(std::string_view line) {
    std::vector<std::string_view> out;
    for (std::size_t at = 0;;) {
        const std::size_t tab = line.find('\t', at);
        out.push_back(line.substr(at, tab == std::string_view::npos ? std::string_view::npos : tab - at));
        if (tab == std::string_view::npos) return out;
        at = tab + 1;
    }
}

/// The census's rows. Its header names a `map` and a `group` column, and its
/// fields are tab-separated. Nothing, after saying why on `err`, when it does
/// not read.
std::optional<std::vector<Row>> readCensus(const stdfs::path& path, std::ostream& err) {
    const auto bytes = uta::fs::readFile(path);
    if (!bytes.has_value()) {
        err << "ut-paths: the census does not read: " << bytes.error().message() << "\n";
        return std::nullopt;
    }
    const std::string text(reinterpret_cast<const char*>(bytes->data()), bytes->size());

    std::vector<Row> rows;
    std::optional<std::size_t> mapColumn;
    std::optional<std::size_t> groupColumn;
    std::size_t lineNumber = 0;
    for (std::size_t at = 0; at < text.size(); ++lineNumber) {
        const std::size_t end = std::min(text.find('\n', at), text.size());
        std::string_view line(text.data() + at, end - at);
        at = end + 1;
        if (line.ends_with('\r')) line.remove_suffix(1);
        const std::vector<std::string_view> cells = fields(line);
        if (lineNumber == 0) {
            for (std::size_t i = 0; i < cells.size(); ++i) {
                if (cells[i] == "map") mapColumn = i;
                if (cells[i] == "group") groupColumn = i;
            }
            if (!mapColumn || !groupColumn) {
                err << "ut-paths: the census's header names no map column or no group column\n";
                return std::nullopt;
            }
            continue;
        }
        if (line.empty()) continue;
        if (cells.size() <= std::max(*mapColumn, *groupColumn)) {
            err << "ut-paths: census line " << lineNumber + 1 << " has too few fields\n";
            return std::nullopt;
        }
        rows.push_back(Row{std::string(cells[*mapColumn]), std::string(cells[*groupColumn])});
    }
    if (lineNumber == 0) {
        err << "ut-paths: the census is empty\n";
        return std::nullopt;
    }
    return rows;
}

bool isWork(const Row& row) {
    return row.group == EXIT_OFF_NET || row.group == PARTITIONED;
}

/// One map's line in the summary.
struct Entry {
    std::string map;
    std::string_view status; ///< written, skipped or refused
    std::string why;         ///< when not written
    std::size_t exits = 0;   ///< when written
    std::size_t nodes = 0;
};

Entry refusal(std::string map, std::string why, std::ostream& err) {
    err << "ut-paths: " << map << ": " << why << "\n";
    return Entry{std::move(map), "refused", std::move(why)};
}

/// One map, read from <install>/Maps, its file written to `outDir`.
Entry runMap(const stdfs::path& install, const stdfs::path& outDir, const Row& row, std::ostream& err) {
    const stdfs::path file = install / "Maps" / (row.map + ".unr");
    std::error_code ec;
    if (!stdfs::is_regular_file(file, ec)) {
        err << "ut-paths: " << row.map << ": skipped, no file in Maps/\n";
        return Entry{row.map, "skipped", "no file in Maps/"};
    }

    const auto bytes = uta::fs::readFile(file);
    if (!bytes.has_value()) return refusal(row.map, std::string(bytes.error().message()), err);
    auto installed = ubake::Install::open(install);
    if (!installed.has_value()) return refusal(row.map, std::string(installed.error().message()), err);
    const auto map = upkg::Package::open(*bytes);
    if (!map.has_value()) return refusal(row.map, std::string(map.error().message()), err);
    const auto scene = sceneOf(*map, ubake::detail::mapNameOf(file), installed->resolver());
    if (!scene.has_value()) return refusal(row.map, std::string(scene.error().message()), err);

    const Proposal proposal = propose(*scene, row.group == PARTITIONED);
    const std::string json =
        toJson(row.map, ubake::detail::hex(uta::md5(*bytes)), row.group, *scene, proposal);
    const stdfs::path target = outDir / (row.map + ".json");
    const auto written = uta::fs::writeFileAtomically(
        target, std::as_bytes(std::span<const char>(json.data(), json.size())));
    if (!written.has_value()) return refusal(row.map, std::string(written.error().message()), err);

    err << "ut-paths: wrote " << ubake::detail::utf8(target) << ": " << scene->exits.size()
        << " exits, " << proposal.nodes.size() << " nodes\n";
    return Entry{row.map, "written", {}, scene->exits.size(), proposal.nodes.size()};
}

std::string summaryOf(const std::vector<Entry>& entries) {
    std::ostringstream out;
    out << "{\"schema\": " << SCHEMA << ", \"maps\": [";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        out << (i == 0 ? "\n  " : ",\n  ") << "{\"map\": ";
        writeJsonString(out, entry.map);
        out << ", \"status\": \"" << entry.status << '"';
        if (entry.status == "written") {
            out << ", \"exits\": " << entry.exits << ", \"nodes\": " << entry.nodes;
        } else {
            out << ", \"why\": ";
            writeJsonString(out, entry.why);
        }
        out << '}';
    }
    out << (entries.empty() ? "]}\n" : "\n]}\n");
    return out.str();
}

} // namespace

int runCli(std::span<const std::string_view> args, std::ostream& out, std::ostream& err) {
    const std::optional<Arguments> parsed = parse(args, err);
    if (!parsed.has_value()) {
        usage(err);
        return EXIT_USAGE;
    }
    if (parsed->help) {
        usage(err);
        return EXIT_OK;
    }
    const std::optional<std::vector<Row>> rows = readCensus(stdfs::path(*parsed->census), err);
    if (!rows.has_value()) return EXIT_USAGE;

    const stdfs::path install(*parsed->install);
    const stdfs::path outDir(*parsed->out);
    std::error_code ec;
    stdfs::create_directories(outDir, ec);
    if (ec) err << "ut-paths: cannot create " << ubake::detail::utf8(outDir) << ": " << ec.message() << "\n";

    std::vector<Entry> entries;
    if (parsed->maps.empty()) {
        for (const Row& row : *rows)
            if (isWork(row)) entries.push_back(runMap(install, outDir, row, err));
    } else {
        // A named map is matched as the resolver matches a package, folded.
        for (const std::string_view name : parsed->maps) {
            const auto row = std::find_if(rows->begin(), rows->end(), [&](const Row& candidate) {
                return ubake::detail::fold(candidate.map) == ubake::detail::fold(name);
            });
            if (row == rows->end() || !isWork(*row))
                entries.push_back(refusal(std::string(name),
                                          "not an EXIT_OFF_NET or PARTITIONED row of the census", err));
            else
                entries.push_back(runMap(install, outDir, *row, err));
        }
    }

    const std::string summary = summaryOf(entries);
    out << summary;
    const auto written = uta::fs::writeFileAtomically(
        outDir / "ut-paths-summary.json",
        std::as_bytes(std::span<const char>(summary.data(), summary.size())));
    if (!written.has_value()) {
        err << "ut-paths: the summary was not written: " << written.error().message() << "\n";
        return EXIT_FAILED;
    }
    const bool refused = std::any_of(entries.begin(), entries.end(),
                                     [](const Entry& entry) { return entry.status == "refused"; });
    return refused ? EXIT_FAILED : EXIT_OK;
}

} // namespace uta::paths
