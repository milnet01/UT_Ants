// The ut-paths command line -- docs/specs/UTA-0121-bot-path-seeds.md SS 4.2,
// INV-9.

#include "Cli.h"

#include "Seeds.h"
#include "Suspects.h"
#include "common/Json.h"
#include "common/Options.h"
#include "core/FileSystem.h"
#include "core/Md5.h"
#include "ubake/Bake.h"
#include "ubake/Install.h"
#include "ubake/Name.h"
#include "upkg/Level.h"
#include "upkg/Package.h"

#include <algorithm>
#include <exception>
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
           "       ut-paths --install <install> --out <dir> --map-file <file.unr>\n"
           "                --group EXIT_OFF_NET|PARTITIONED\n"
           "       ut-paths --install <install> --out <dir> --suspect-spots <file.unr> ...\n"
           "       ut-paths --help | -h\n"
           "\n"
           "Proposes bot path nodes for every EXIT_OFF_NET and PARTITIONED map of\n"
           "UT_MonsterHunt's census, or for the maps named, writing <dir>/<map>.json.\n"
           "A map with no file in <install>/Maps is skipped. Its summary entry names\n"
           "any file of that name elsewhere in the install, and any Maps/ name that\n"
           "differs from it only in punctuation, and reads neither: such a name is\n"
           "often a different map. Standard output is one\n"
           "JSON object, also written to <dir>/ut-paths-summary.json.\n"
           "\n"
           "With --map-file, the one map file named, wherever it is, with the group\n"
           "given, and no census; its file is <dir>/<its name>.json.\n"
           "\n"
           "With --suspect-spots (UTA-0514), for each map file named, the standing\n"
           "spots whose eye lies in empty render-BSP space with no leaf,\n"
           "one per 256-unit cube, most first, at most 16: <dir>/<its name>.spots.ini\n"
           "holds them as AddSpots=(X=,Y=,Z=) lines for UT_MonsterHunt's\n"
           "MHUndrawnProbe, and <dir>/ut-paths-spots.json counts them per map.\n";
}

struct Arguments {
    bool help = false;
    std::optional<std::string_view> install;
    std::optional<std::string_view> census;
    std::optional<std::string_view> out;
    std::optional<std::string_view> mapFile; ///< UTA-0495: one map, named by its path
    std::optional<std::string_view> group;   ///< UTA-0495: --map-file's census group
    bool suspects = false;                   ///< UTA-0514: the positionals are map files
    std::vector<std::string_view> maps;
};

/// The arguments, or nothing after saying on `err` what was wrong with them.
std::optional<Arguments> parse(std::span<const std::string_view> args, std::ostream& err) {
    Arguments parsed;
    tools::OptionValues values("ut-paths", args, err);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "--help" || arg == "-h") {
            parsed.help = true;
        } else if (arg == "--install") {
            if (!(parsed.install = values.take(i))) return std::nullopt;
        } else if (arg == "--census") {
            if (!(parsed.census = values.take(i))) return std::nullopt;
        } else if (arg == "--out") {
            if (!(parsed.out = values.take(i))) return std::nullopt;
        } else if (arg == "--map-file") {
            if (!(parsed.mapFile = values.take(i))) return std::nullopt;
        } else if (arg == "--group") {
            if (!(parsed.group = values.take(i))) return std::nullopt;
        } else if (arg == "--suspect-spots") {
            parsed.suspects = true;
        } else if (arg.starts_with("-")) {
            err << "ut-paths: unknown option " << arg << "\n";
            return std::nullopt;
        } else {
            parsed.maps.push_back(arg);
        }
    }
    if (parsed.help) return parsed;
    if (parsed.suspects) {
        if (!parsed.install || !parsed.out || parsed.maps.empty()) {
            err << "ut-paths: --suspect-spots needs --install, --out and at least one map file\n";
            return std::nullopt;
        }
        if (parsed.census || parsed.mapFile || parsed.group) {
            err << "ut-paths: --suspect-spots takes no --census, --map-file or --group\n";
            return std::nullopt;
        }
        return parsed;
    }
    if (parsed.mapFile || parsed.group) {
        // UTA-0495: one map with its group, in place of the census.
        if (!parsed.install || !parsed.out || !parsed.mapFile || !parsed.group) {
            err << "ut-paths: --map-file needs --install, --out and --group\n";
            return std::nullopt;
        }
        if (parsed.census || !parsed.maps.empty()) {
            err << "ut-paths: --map-file takes no --census and no map names\n";
            return std::nullopt;
        }
        if (*parsed.group != EXIT_OFF_NET && *parsed.group != PARTITIONED) {
            err << "ut-paths: --group is EXIT_OFF_NET or PARTITIONED, not " << *parsed.group << "\n";
            return std::nullopt;
        }
        return parsed;
    }
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
    /// When skipped, UTA-0137: files of the map's name in another directory of
    /// the install, and Maps/ names differing from it only in punctuation.
    /// Reported, never read.
    std::vector<std::string> elsewhere;
    std::vector<std::string> differentNames;
};

Entry refusal(std::string map, std::string why, std::ostream& err) {
    err << "ut-paths: " << map << ": " << why << "\n";
    return Entry{std::move(map), "refused", std::move(why)};
}

/// A name with everything but ASCII letters and digits removed, folded.
std::string alphanumericFolded(std::string_view name) {
    std::string out;
    for (const char ch : name) {
        const auto c = static_cast<unsigned char>(ch);
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')) {
            out += ch;
        } else if (c >= 'A' && c <= 'Z') {
            out += static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

/// UTA-0137: what a skipped row can say about where its map went, without
/// resolving it. A name differing only in punctuation is often a DIFFERENT
/// map -- a de-duplicated variant rather than a mangled name -- and only
/// content tells the two apart, so nothing found here is read. Every top-level
/// directory is searched, so no one install's layout is assumed.
void lookElsewhere(const stdfs::path& install, const Row& row, Entry& entry) {
    const std::string wanted = ubake::detail::fold(row.map);
    const std::string wantedAlphanumeric = alphanumericFolded(row.map);
    std::error_code listing;
    for (stdfs::directory_iterator dir(install, listing), end; !listing && dir != end; dir.increment(listing)) {
        std::error_code ignored;
        if (!dir->is_directory(ignored)) continue;
        const std::string dirName = ubake::detail::utf8(dir->path().filename());
        const bool maps = ubake::detail::fold(dirName) == "maps";
        std::error_code inner;
        for (stdfs::directory_iterator file(dir->path(), inner), stop; !inner && file != stop; file.increment(inner)) {
            if (!file->is_regular_file(ignored)
                || ubake::detail::fold(ubake::detail::utf8(file->path().extension())) != ".unr")
                continue;
            const std::string stem = ubake::detail::utf8(file->path().stem());
            if (!maps && ubake::detail::fold(stem) == wanted) {
                entry.elsewhere.push_back(dirName + "/" + ubake::detail::utf8(file->path().filename()));
            } else if (maps && alphanumericFolded(stem) == wantedAlphanumeric) {
                entry.differentNames.push_back(stem);
            }
        }
    }
    std::sort(entry.elsewhere.begin(), entry.elsewhere.end());
    std::sort(entry.differentNames.begin(), entry.differentNames.end());
}

/// `<name>.unr` in the install's Maps folder, folder and file matched by folded
/// name as the resolver matches a package (UTA-0372): on Linux a census
/// `DM-Foo` is `dm-foo.unr`, and an install may spell its folder `maps`. The
/// first in name order where more than one matches.
std::optional<stdfs::path> foldedMap(const stdfs::path& install, std::string_view name) {
    const std::string wanted = ubake::detail::fold(std::string(name) + ".unr");
    std::vector<stdfs::path> found;
    std::error_code ignored;
    std::error_code outer;
    for (stdfs::directory_iterator dir(install, outer), end; !outer && dir != end; dir.increment(outer)) {
        if (!dir->is_directory(ignored) || ubake::detail::fold(ubake::detail::utf8(dir->path().filename())) != "maps")
            continue;
        std::error_code inner;
        for (stdfs::directory_iterator file(dir->path(), inner), stop; !inner && file != stop; file.increment(inner))
            if (file->is_regular_file(ignored)
                && ubake::detail::fold(ubake::detail::utf8(file->path().filename())) == wanted)
                found.push_back(file->path());
    }
    if (found.empty()) return std::nullopt;
    return *std::ranges::min_element(found);
}

/// Whether `name` is a plain file name: not empty, not a dot or two, and
/// holding no separator, drive colon or NUL.
bool plainName(const std::string& name) {
    return !name.empty() && name != "." && name != ".." &&
           name.find_first_of(std::string_view{"/\\:\0", 4}) == std::string::npos;
}

/// The map in `file`, its proposal written to `outDir` as `<row.map>.json`.
/// `installed` is opened by the first map that needs it and kept for the
/// rest, so a library run lists the install and parses its packages once.
Entry runFile(const stdfs::path& install, std::optional<ubake::Install>& installed, const stdfs::path& outDir,
              const Row& row, const stdfs::path& file, std::ostream& err) {
    const auto bytes = uta::fs::readFile(file);
    if (!bytes.has_value()) return refusal(row.map, std::string(bytes.error().message()), err);
    if (!installed) {
        auto opened = ubake::Install::open(install);
        if (!opened.has_value()) return refusal(row.map, std::string(opened.error().message()), err);
        installed.emplace(std::move(*opened));
    }
    const auto map = upkg::Package::open(*bytes);
    if (!map.has_value()) return refusal(row.map, std::string(map.error().message()), err);
    const auto scene = sceneOf(*map, ubake::detail::mapNameOf(file), installed->resolver());
    if (!scene.has_value()) return refusal(row.map, std::string(scene.error().message()), err);

    const Proposal proposal = propose(*scene, row.group == PARTITIONED);
    const std::string json =
        toJson(row.map, ubake::detail::hex(uta::md5(*bytes)), row.group, *scene, proposal);
    // resolveUnder refuses a Windows device name such as NUL, which a plain
    // name can still be (review-code 2026-10-15).
    const auto target = uta::fs::resolveUnder(outDir, uta::fs::pathFromUtf8(row.map + ".json"));
    if (!target.has_value()) return refusal(row.map, "not a plain map name", err);
    const auto written = uta::fs::writeFileAtomically(
        *target, std::as_bytes(std::span<const char>(json.data(), json.size())));
    if (!written.has_value()) return refusal(row.map, std::string(written.error().message()), err);

    err << "ut-paths: wrote " << ubake::detail::utf8(*target) << ": " << scene->exits.size()
        << " exits, " << proposal.nodes.size() << " nodes\n";
    return Entry{row.map, "written", {}, scene->exits.size(), proposal.nodes.size()};
}

/// One census row's map, read from <install>/Maps.
Entry runMap(const stdfs::path& install, std::optional<ubake::Install>& installed, const stdfs::path& outDir,
             const Row& row, std::ostream& err) {
    // The census is another project's file. A name that is a path would read
    // outside Maps/ and write outside --out (review-code 2026-09-26).
    if (!plainName(row.map)) return refusal(row.map, "not a plain map name", err);
    // resolveUnder refuses what a plain name can still carry: a Windows
    // device name such as NUL, which opens a device in any folder
    // (review-code 2026-10-15).
    const auto resolved = uta::fs::resolveUnder(install / "Maps", uta::fs::pathFromUtf8(row.map + ".unr"));
    if (!resolved.has_value()) return refusal(row.map, "not a plain map name", err);
    stdfs::path file = *resolved;
    std::error_code ec;
    if (!stdfs::is_regular_file(file, ec))
        if (const auto folded = foldedMap(install, row.map)) file = *folded;
    if (!stdfs::is_regular_file(file, ec)) {
        err << "ut-paths: " << row.map << ": skipped, no file in Maps/\n";
        Entry skipped{row.map, "skipped", "no file in Maps/"};
        lookElsewhere(install, row, skipped);
        return skipped;
    }
    return runFile(install, installed, outDir, row, file, err);
}

/// UTA-0495: the one map file the caller named, wherever it is. Its name is
/// the file's stem; a missing file is refused, since the caller named it.
Entry runNamedFile(const stdfs::path& install, std::optional<ubake::Install>& installed, const stdfs::path& outDir,
                   const stdfs::path& file, std::string_view group, std::ostream& err) {
    const Row row{ubake::detail::utf8(file.stem()), std::string(group)};
    if (!plainName(row.map)) return refusal(row.map, "not a plain map name", err);
    std::error_code ec;
    if (!stdfs::is_regular_file(file, ec)) return refusal(row.map, "no such map file", err);
    return runFile(install, installed, outDir, row, file, err);
}

/// runMap, with anything it throws refused as that map's failure. walkGraph
/// rethrows a worker's exception, and one map must not end a library run
/// (UTA-0224).
template <typename Run>
Entry guarded(const std::string& map, std::ostream& err, Run run) {
    try {
        return run();
    } catch (const std::exception& thrown) {
        return refusal(map, std::string("threw: ") + thrown.what(), err);
    }
}

Entry runMapGuarded(const stdfs::path& install, std::optional<ubake::Install>& installed, const stdfs::path& outDir,
                    const Row& row, std::ostream& err) {
    return guarded(row.map, err, [&] { return runMap(install, installed, outDir, row, err); });
}

/// UTA-0514: one map's line in the suspect-spots summary.
struct SpotsEntry {
    std::string map;
    std::string why; ///< empty when written
    Suspects found;
};

/// The suspect spots of the map in `file`, written to `outDir` as
/// `<stem>.spots.ini`, even when there are none.
SpotsEntry runSuspects(const stdfs::path& install, std::optional<ubake::Install>& installed,
                       const stdfs::path& outDir, const stdfs::path& file, std::ostream& err) {
    SpotsEntry entry{ubake::detail::utf8(file.stem()), {}, {}};
    const auto refuse = [&](std::string why) {
        err << "ut-paths: " << entry.map << ": " << why << "\n";
        entry.why = std::move(why);
        return entry;
    };
    if (!plainName(entry.map)) return refuse("not a plain map name");
    const auto bytes = uta::fs::readFile(file);
    if (!bytes.has_value()) return refuse(std::string(bytes.error().message()));
    if (!installed) {
        auto opened = ubake::Install::open(install);
        if (!opened.has_value()) return refuse(std::string(opened.error().message()));
        installed.emplace(std::move(*opened));
    }
    const auto map = upkg::Package::open(*bytes);
    if (!map.has_value()) return refuse(std::string(map.error().message()));
    const std::string mapName = ubake::detail::mapNameOf(file);
    const auto scene = sceneOf(*map, mapName, installed->resolver());
    if (!scene.has_value()) return refuse(std::string(scene.error().message()));
    // sceneOf read the model to build the tree and keeps only the tree, so
    // the render BSP is read again here; each step has already succeeded once.
    const auto levelExport = ubake::detail::findLevel(*map, mapName);
    if (!levelExport.has_value()) return refuse(std::string(levelExport.error().message()));
    const auto level = upkg::readLevel(*map, **levelExport);
    if (!level.has_value()) return refuse(std::string(level.error().message()));
    const auto modelExport = ubake::detail::findModel(*map, *level, mapName);
    if (!modelExport.has_value()) return refuse(std::string(modelExport.error().message()));
    const auto model = upkg::readModel(*map, **modelExport);
    if (!model.has_value()) return refuse(std::string(model.error().message()));

    const WalkGraph graph = walkGraph(scene->tree);
    entry.found = suspectSpots(*model, graph);
    const std::string ini = addSpotsLines(entry.found.spots);
    const auto target = uta::fs::resolveUnder(outDir, uta::fs::pathFromUtf8(entry.map + ".spots.ini"));
    if (!target.has_value()) return refuse("not a plain map name");
    const auto written =
        uta::fs::writeFileAtomically(*target, std::as_bytes(std::span<const char>(ini.data(), ini.size())));
    if (!written.has_value()) return refuse(std::string(written.error().message()));
    err << "ut-paths: wrote " << ubake::detail::utf8(*target) << ": " << entry.found.spots.size() << " of "
        << entry.found.clusters << " clusters, " << entry.found.leafless << " of " << entry.found.standing
        << " standing spots suspect\n";
    return entry;
}

std::string spotsSummaryOf(const std::vector<SpotsEntry>& entries) {
    std::ostringstream out;
    out << "{\"schema\": " << SCHEMA << ", \"maps\": [";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const SpotsEntry& entry = entries[i];
        out << (i == 0 ? "\n  " : ",\n  ") << "{\"map\": ";
        writeJsonString(out, entry.map);
        if (entry.why.empty()) {
            const Suspects& f = entry.found;
            out << ", \"status\": \"written\", \"standing\": " << f.standing << ", \"leafless\": " << f.leafless
                << ", \"clusters\": " << f.clusters << ", \"spots\": " << f.spots.size() << '}';
        } else {
            out << ", \"status\": \"refused\", \"why\": ";
            writeJsonString(out, entry.why);
            out << '}';
        }
    }
    out << (entries.empty() ? "]}\n" : "\n]}\n");
    return out.str();
}

/// UTA-0514's run: every map file named, then the summary.
int runSuspectsCli(const Arguments& parsed, std::ostream& out, std::ostream& err) {
    const stdfs::path install = uta::fs::pathFromUtf8(*parsed.install);
    const stdfs::path outDir = uta::fs::pathFromUtf8(*parsed.out);
    std::error_code ec;
    stdfs::create_directories(outDir, ec);
    if (ec) err << "ut-paths: cannot create " << ubake::detail::utf8(outDir) << ": " << ec.message() << "\n";
    std::optional<ubake::Install> installed;
    std::vector<SpotsEntry> entries;
    for (const std::string_view name : parsed.maps) {
        const stdfs::path file = uta::fs::pathFromUtf8(name);
        try {
            entries.push_back(runSuspects(install, installed, outDir, file, err));
        } catch (const std::exception& thrown) {
            // walkGraph rethrows a worker's exception; one map must not end the run.
            const std::string why = std::string("threw: ") + thrown.what();
            err << "ut-paths: " << ubake::detail::utf8(file.stem()) << ": " << why << "\n";
            entries.push_back(SpotsEntry{ubake::detail::utf8(file.stem()), why, {}});
        }
    }
    const std::string summary = spotsSummaryOf(entries);
    out << summary;
    const auto written = uta::fs::writeFileAtomically(
        outDir / "ut-paths-spots.json", std::as_bytes(std::span<const char>(summary.data(), summary.size())));
    if (!written.has_value()) {
        err << "ut-paths: the summary was not written: " << written.error().message() << "\n";
        return EXIT_FAILED;
    }
    const bool refused =
        std::any_of(entries.begin(), entries.end(), [](const SpotsEntry& entry) { return !entry.why.empty(); });
    return refused ? EXIT_FAILED : EXIT_OK;
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
            // Written only when non-empty, so a skipped map with nothing to
            // report keeps the entry it always had.
            const auto list = [&out](std::string_view key, const std::vector<std::string>& values) {
                if (values.empty()) return;
                out << ", \"" << key << "\": [";
                for (std::size_t v = 0; v < values.size(); ++v) {
                    if (v != 0) out << ", ";
                    writeJsonString(out, values[v]);
                }
                out << ']';
            };
            list("elsewhere", entry.elsewhere);
            list("differentNames", entry.differentNames);
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
    if (parsed->suspects) return runSuspectsCli(*parsed, out, err);
    std::optional<std::vector<Row>> rows;
    if (!parsed->mapFile) {
        rows = readCensus(uta::fs::pathFromUtf8(*parsed->census), err);
        if (!rows.has_value()) return EXIT_FAILED; // well-formed arguments; the file failed
    }

    const stdfs::path install = uta::fs::pathFromUtf8(*parsed->install);
    const stdfs::path outDir = uta::fs::pathFromUtf8(*parsed->out);
    std::error_code ec;
    stdfs::create_directories(outDir, ec);
    if (ec) err << "ut-paths: cannot create " << ubake::detail::utf8(outDir) << ": " << ec.message() << "\n";

    std::optional<ubake::Install> installed;
    std::vector<Entry> entries;
    if (parsed->mapFile) {
        const stdfs::path file = uta::fs::pathFromUtf8(*parsed->mapFile);
        entries.push_back(guarded(ubake::detail::utf8(file.stem()), err, [&] {
            return runNamedFile(install, installed, outDir, file, *parsed->group, err);
        }));
    } else if (parsed->maps.empty()) {
        for (const Row& row : *rows)
            if (isWork(row)) entries.push_back(runMapGuarded(install, installed, outDir, row, err));
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
                entries.push_back(runMapGuarded(install, installed, outDir, *row, err));
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
