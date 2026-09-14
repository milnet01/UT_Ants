// ut-dump: inspect an Unreal Engine 1 package from the command line.
//
// UTA-0012. Shaped against the consumer that asked for it -- a script over a
// map library, not a person reading one file -- so the machine-readable mode
// is the point rather than an afterthought, and the three day-one queries the
// roadmap item names are what it answers: every package a map imports, every
// actor of a class with the properties that place and wire it, and each
// navigation point with the reach specs joining it to the others.
//
// SCOPE: this reports. It opens no second decoder of any format, resolves
// nothing `unav` already resolves, and repairs nothing. Every number below
// comes from `upkg` or `unav`; where one of them counts something and
// discards the rest, the count is carried through rather than recomputed,
// because a second tally is a second answer.
//
// A package that fails to open is reported as a refusal on that package and
// the run continues to the next one. That is deliberate: the library this was
// written for holds files that do not open, and finding out which is the job.

#include "Cli.h"

#include "common/Json.h"
#include "core/FileSystem.h"
#include "unav/Build.h"
#include "unav/Graphs.h"
#include "upkg/Class.h"
#include "upkg/Level.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace uta::dump {

// Inside the namespace, because `fs` there would otherwise name uta::fs.
namespace fs = std::filesystem;

namespace {

constexpr int SCHEMA = 1;

// ---------------------------------------------------------------- JSON out
//
// Hand-written rather than a dependency: the output is a fixed shape this file
// owns entirely, and `docs/design.md` keeps the dependency list short on
// purpose. The string escaper is tools/common/Json.h's, shared by every tool.
using uta::tools::writeJsonString;

std::string foldCase(std::string_view text) {
    std::string folded{text};
    std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return folded;
}

/// The whole file in one read, or empty when it cannot be read. uta::fs owns
/// the reading; a character-at-a-time stream was over half of this tool's
/// CPU on a library run (UTA-0094).
std::vector<std::byte> readWhole(const fs::path& path) {
    auto bytes = uta::fs::readFile(path);
    return bytes.has_value() ? std::move(*bytes) : std::vector<std::byte>{};
}

// ------------------------------------------------------------- the resolver
//
// Class ancestry crosses packages -- a map's `Teleporter` descends from
// `NavigationPoint` in Engine.u -- so the node filter cannot work from the map
// alone. Packages are opened once and kept, because a library run asks for the
// same handful of System packages on every map.
class SystemPackages {
public:
    explicit SystemPackages(const fs::path& systemDir) {
        if (!fs::is_directory(systemDir)) {
            return;
        }
        for (const fs::directory_entry& entry : fs::directory_iterator{systemDir}) {
            if (entry.is_regular_file() && foldCase(entry.path().extension().string()) == ".u") {
                paths_.emplace(foldCase(entry.path().stem().string()), entry.path());
            }
        }
    }

    [[nodiscard]] bool empty() const noexcept { return paths_.empty(); }

    [[nodiscard]] uta::upkg::PackageResolver resolver() {
        return [this](std::string_view name) -> uta::Result<const uta::upkg::Package*> {
            const std::string key{name}; // the caller folds
            if (const auto cached = opened_.find(key); cached != opened_.end()) {
                return &cached->second;
            }
            const auto path = paths_.find(key);
            if (path == paths_.end() || failed_.contains(key)) {
                return nullptr; // absent is an ordinary answer, not an error
            }
            auto& raw = bytes_[key];
            raw = readWhole(path->second);
            auto package = uta::upkg::Package::open(raw);
            if (!package.has_value()) {
                // Remembered, so a package that will not open is read once
                // rather than on every lookup for the rest of the run.
                failed_.insert(key);
                bytes_.erase(key);
                return nullptr;
            }
            return &opened_.emplace(key, std::move(*package)).first->second;
        };
    }

private:
    std::map<std::string, fs::path> paths_;
    std::map<std::string, std::vector<std::byte>> bytes_;
    std::map<std::string, uta::upkg::Package> opened_;
    std::set<std::string> failed_;
};

// ------------------------------------------------------------ one package
struct ActorFacts {
    std::map<std::string, long long> classCounts;
    std::map<std::uint32_t, std::string> classOfExport;
};

std::string nameOr(const uta::upkg::Package& package, uta::upkg::ObjectReference ref) {
    const auto name = package.objectName(ref);
    return name.has_value() ? std::string{*name} : std::string{"?"};
}

/// UTA-0136: every node and every edge, one row each, inside `nav`. The fields
/// are the graph's own, unfiltered and undecoded -- a consumer deciding what a
/// walking bot may use applies its own rule to them, as ut-paths' sceneOf does.
/// A node is named by its actor's object name, which is how a T3D export names
/// the same actor; an edge's `from` and `to` are positions in `nodeList`.
void writeNavGraph(std::ostream& out, const uta::upkg::Package& package,
                   const uta::unav::NavGraph& nav) {
    out << ", \"nodeList\": [";
    for (std::size_t i = 0; i < nav.nodes.size(); ++i) {
        const uta::unav::NavNode& node = nav.nodes[i];
        std::string name = "?";
        if (node.exportIndex < package.exports().size()) {
            const auto found = package.name(package.exports()[node.exportIndex].objectName);
            if (found.has_value()) {
                name = std::string{*found};
            }
        }
        out << (i == 0 ? "\n   " : ",\n   ") << "{\"export\": " << node.exportIndex << ", \"name\": ";
        writeJsonString(out, name);
        out << ", \"class\": ";
        writeJsonString(out, node.className);
        out << "}";
    }
    out << "], \"edgeList\": [";
    for (std::size_t i = 0; i < nav.edges.size(); ++i) {
        const uta::unav::NavEdge& edge = nav.edges[i];
        out << (i == 0 ? "\n   " : ",\n   ") << "{\"from\": " << edge.from << ", \"to\": " << edge.to
            << ", \"distance\": " << edge.distance
            << ", \"collisionRadius\": " << edge.collisionRadius
            << ", \"collisionHeight\": " << edge.collisionHeight
            << ", \"reachFlags\": " << edge.reachFlags
            // A byte, so widened: streamed as it is, it prints as a character.
            << ", \"pruned\": " << static_cast<int>(edge.pruned) << "}";
    }
    out << "]";
}

// ---------------------------------------------------------------- UTA-0101
using uta::tools::writeJsonText;

/// The string property `want`, compared case-insensitively, at array index 0.
std::optional<std::string> textOf(const uta::upkg::Package& package,
                                  const std::vector<uta::upkg::Property>& properties,
                                  std::string_view want) {
    const std::string folded = foldCase(want);
    for (const uta::upkg::Property& property : properties) {
        const auto name = package.name(property.nameIndex);
        if (!name.has_value() || property.arrayIndex != 0 || foldCase(*name) != folded) continue;
        if (const auto* text = std::get_if<std::string>(&property.value)) return *text;
    }
    return std::nullopt;
}

/// `"key": {"title": ..., "author": ...}` from `entry`, or null when there is no
/// such export or its properties do not read. The values are the map's
/// EXPLICIT ones: null means the map sets none, and the game then shows the
/// class default. Both LevelInfo and LevelSummary are reported because maps
/// disagree on which one carries a title.
void writeCredits(std::ostream& out, std::string_view key, const uta::upkg::Package& package,
                  const uta::upkg::ExportEntry* entry) {
    out << ",\n  \"" << key << "\": ";
    if (entry == nullptr) {
        out << "null";
        return;
    }
    const auto properties = uta::upkg::readProperties(package, *entry);
    if (!properties.has_value()) {
        out << "null";
        return;
    }
    const auto field = [&](std::string_view want) {
        const auto text = textOf(package, *properties, want);
        if (text.has_value()) {
            writeJsonText(out, *text);
        } else {
            out << "null";
        }
    };
    out << "{\"title\": ";
    field("Title");
    out << ", \"author\": ";
    field("Author");
    out << "}";
}

/// The first export of `className`, or null.
const uta::upkg::ExportEntry* firstExportOf(const uta::upkg::Package& package,
                                            std::string_view className) {
    for (const auto& entry : package.exports()) {
        const auto name = package.objectName(entry.objectClass);
        if (name.has_value() && *name == className) return &entry;
    }
    return nullptr;
}

/// The LevelInfo the level's own actor list names, or null. Some maps carry
/// several LevelInfo exports, left behind by the editor, and only the one in
/// the level is the level's: taking the export table's first, or its last,
/// reports another export's title on exactly those maps.
const uta::upkg::ExportEntry* levelInfoOf(const uta::upkg::Package& package,
                                          const uta::upkg::Level& level) {
    for (const uta::upkg::ObjectReference slot : level.actors) {
        if (slot.kind() != uta::upkg::ObjectReferenceKind::Export || slot.index() >= package.exports().size())
            continue;
        const uta::upkg::ExportEntry& entry = package.exports()[slot.index()];
        const auto name = package.objectName(entry.objectClass);
        if (name.has_value() && *name == "LevelInfo") return &entry;
    }
    return nullptr;
}

/// ThingFactory's own default capacity. Its declaration names -1 as "no limit";
/// its default is this, which the factory counts down from and never reaches,
/// so it is no limit too and is not summed as a monster count.
constexpr std::int32_t FACTORY_NO_LIMIT = 1000000;

/// What an actor's class family makes it, for the monster total.
struct ActorKind {
    bool resolved = false; ///< the whole class family was found
    bool factory = false;  ///< descends from ThingFactory
    bool pawn = false;     ///< descends from ScriptedPawn
    std::optional<std::int32_t> defaultCapacity;
};

ActorKind kindOf(const uta::upkg::Package& map, std::string_view mapName,
                 uta::upkg::ObjectReference classReference,
                 const uta::upkg::PackageResolver& resolver) {
    ActorKind kind;
    const auto site = uta::upkg::resolveClass(map, mapName, classReference, resolver);
    if (!site.has_value() || site->resolved.package == nullptr) return kind;
    const auto ancestry =
        uta::upkg::readAncestry(*site->resolved.package, *site->resolved.entry, resolver);
    if (!ancestry.has_value()) return kind;
    for (const uta::upkg::ResolvedClass& link : ancestry->chain) {
        const auto name = link.package->name(link.entry->objectName);
        if (!name.has_value()) continue;
        const std::string folded = foldCase(*name);
        kind.factory = kind.factory || folded == "thingfactory";
        kind.pawn = kind.pawn || folded == "scriptedpawn";
    }
    // A chain cut short by a missing package may lack the ancestor that decides.
    kind.resolved = ancestry->end == uta::upkg::AncestryEnd::Root;
    if (kind.factory) {
        if (const auto defaults = uta::upkg::effectiveDefaults(*ancestry); defaults.has_value()) {
            for (const uta::upkg::EffectiveProperty& effective : *defaults) {
                if (effective.property.arrayIndex != 0 || foldCase(effective.name) != "capacity") continue;
                if (const auto* value = std::get_if<std::int32_t>(&effective.property.value))
                    kind.defaultCapacity = *value;
            }
        }
    }
    return kind;
}

/// The whole-map monster total: every ThingFactory descendant's capacity, the
/// actor's own else its class family's, and every ScriptedPawn placed in the
/// map. A factory with no limit, or with no capacity found, is counted apart
/// rather than summed. An actor whose class family did not resolve cannot be
/// sorted, and is counted too, so an incomplete total says so.
void writeMonsters(std::ostream& out, const uta::upkg::Package& map, std::string_view mapName,
                   const uta::upkg::Level& level, const uta::upkg::PackageResolver& resolver) {
    long long factories = 0, capacity = 0, unlimited = 0, unknown = 0, pawns = 0, unresolved = 0;
    std::map<std::int32_t, ActorKind> kinds; // by raw class reference
    std::set<std::uint32_t> seen;
    for (const uta::upkg::ObjectReference slot : level.actors) {
        // A map can name one actor in two slots (UTA-0124); it is one actor.
        if (slot.kind() != uta::upkg::ObjectReferenceKind::Export || slot.index() >= map.exports().size()
            || !seen.insert(slot.index()).second)
            continue;
        const uta::upkg::ExportEntry& entry = map.exports()[slot.index()];
        if (entry.objectClass.kind() == uta::upkg::ObjectReferenceKind::Null) continue;
        auto found = kinds.find(entry.objectClass.raw());
        if (found == kinds.end())
            found = kinds.emplace(entry.objectClass.raw(), kindOf(map, mapName, entry.objectClass, resolver))
                        .first;
        const ActorKind& kind = found->second;
        if (kind.pawn) ++pawns;
        if (!kind.factory) {
            if (!kind.resolved && !kind.pawn) ++unresolved;
            continue;
        }
        ++factories;
        std::optional<std::int32_t> value = kind.defaultCapacity;
        if (const auto properties = uta::upkg::readProperties(map, entry); properties.has_value()) {
            for (const uta::upkg::Property& property : *properties) {
                const auto name = map.name(property.nameIndex);
                if (!name.has_value() || property.arrayIndex != 0 || foldCase(*name) != "capacity") continue;
                if (const auto* own = std::get_if<std::int32_t>(&property.value)) value = *own;
            }
        }
        if (!value.has_value()) {
            ++unknown;
        } else if (*value < 0 || *value >= FACTORY_NO_LIMIT) {
            ++unlimited;
        } else {
            capacity += *value;
        }
    }
    out << ",\n  \"monsters\": {\"factories\": " << factories << ", \"capacity\": " << capacity
        << ", \"unlimitedFactories\": " << unlimited << ", \"unknownCapacityFactories\": " << unknown
        << ", \"placedPawns\": " << pawns << ", \"unresolvedActors\": " << unresolved << "}";
}

void dumpPackage(std::ostream& out, const fs::path& path, SystemPackages& system,
                 bool navGraph, bool first) {
    if (!first) {
        out << ",\n";
    }
    out << " {\n  \"file\": ";
    writeJsonString(out, path.string());

    const std::vector<std::byte> raw = readWhole(path);
    if (raw.empty()) {
        out << ",\n  \"ok\": false,\n  \"error\": \"unreadable or empty\"\n }";
        return;
    }

    const auto package = uta::upkg::Package::open(raw);
    if (!package.has_value()) {
        out << ",\n  \"ok\": false,\n  \"error\": ";
        writeJsonString(out, "package did not open");
        out << "\n }";
        return;
    }

    out << ",\n  \"ok\": true";
    out << ",\n  \"bytes\": " << raw.size();
    out << ",\n  \"exports\": " << package->exports().size();
    out << ",\n  \"imports\": " << package->imports().size();

    // upkg's supported call (UTA-0070), sorted here because this output always
    // has been. A texture's group is an object inside its package, not a
    // package, which is the mistake the call's rule exists to prevent.
    out << ",\n  \"importedPackages\": [";
    std::set<std::string> dependencies;
    if (const auto names = uta::upkg::importedPackages(*package); names.has_value()) {
        for (const std::string_view name : *names) {
            dependencies.emplace(name);
        }
    }
    bool firstDep = true;
    for (const std::string& dep : dependencies) {
        if (!firstDep) {
            out << ", ";
        }
        firstDep = false;
        writeJsonString(out, dep);
    }
    out << "]";

    // Actor classes. Counted over the export table rather than over the
    // level's actor array, so a package with no Level still answers.
    ActorFacts facts;
    for (std::uint32_t i = 0; i < package->exports().size(); ++i) {
        const auto& entry = package->exports()[i];
        std::string className = nameOr(*package, entry.objectClass);
        facts.classOfExport.emplace(i, className);
        ++facts.classCounts[className];
    }

    out << ",\n  \"classCounts\": {";
    bool firstClass = true;
    for (const auto& [className, count] : facts.classCounts) {
        if (!firstClass) {
            out << ", ";
        }
        firstClass = false;
        writeJsonString(out, className);
        out << ": " << count;
    }
    out << "}";

    // The Level export, and the two graphs over it. A package with no Level is
    // not a map -- reported as absent rather than as a failure, because
    // ut-dump is pointed at System packages too.
    const uta::upkg::ExportEntry* levelExport = nullptr;
    for (const auto& object : package->exports()) {
        const auto className = package->objectName(object.objectClass);
        if (className.has_value() && *className == "Level") {
            levelExport = &object;
            break;
        }
    }

    if (levelExport == nullptr) {
        out << ",\n  \"level\": null\n }";
        return;
    }

    const auto level = uta::upkg::readLevel(*package, *levelExport);
    if (!level.has_value()) {
        out << ",\n  \"level\": null,\n  \"levelError\": ";
        writeJsonString(out, "level export did not read");
        out << "\n }";
        return;
    }

    out << ",\n  \"level\": {";
    out << "\"actors\": " << level->actors.size();
    out << ", \"rawSlots\": " << level->rawSlotCount;
    out << ", \"reachSpecs\": " << level->reachSpecs.size();
    out << "}";

    auto resolver = system.resolver();
    writeCredits(out, "levelInfo", *package, levelInfoOf(*package, *level));
    writeCredits(out, "levelSummary", *package, firstExportOf(*package, "LevelSummary"));
    writeMonsters(out, *package, path.stem().string(), *level, resolver);

    const auto nav = uta::unav::buildNavGraph(*package, *level, resolver);
    if (nav.has_value()) {
        // A node with no outgoing edge is a waypoint nothing leads away from.
        // Counted here because it is the cheapest signal that a map's path
        // network is broken, and it needs no second pass over the file.
        long long isolated = 0;
        for (const uta::unav::NavNode& node : nav->nodes) {
            if (node.edgeCount == 0) {
                ++isolated;
            }
        }
        out << ",\n  \"nav\": {\"nodes\": " << nav->nodes.size()
            << ", \"edges\": " << nav->edges.size()
            << ", \"discardedEndpoints\": " << nav->discardedEndpoints
            << ", \"nodesWithNoExit\": " << isolated;
        if (navGraph) {
            writeNavGraph(out, *package, *nav);
        }
        out << "}";
    } else {
        out << ",\n  \"nav\": null";
    }

    const auto wiring = uta::unav::buildWiringGraph(*package);
    if (wiring.has_value()) {
        out << ",\n  \"wiring\": {\"nodes\": " << wiring->nodes.size()
            << ", \"edges\": " << wiring->edges.size()
            << ", \"dangling\": [";
        bool firstDangle = true;
        for (const uta::unav::DanglingEvent& dangle : wiring->dangling) {
            if (!firstDangle) {
                out << ", ";
            }
            firstDangle = false;
            // The node position is translated back to the actor's class here,
            // because a position means nothing outside this process and the
            // consumer's question is "what kind of thing is wired to nothing".
            std::string className = "?";
            if (dangle.from < wiring->nodes.size()) {
                const std::uint32_t exportIndex = wiring->nodes[dangle.from].exportIndex;
                if (const auto found = facts.classOfExport.find(exportIndex);
                    found != facts.classOfExport.end()) {
                    className = found->second;
                }
            }
            out << "{\"class\": ";
            writeJsonString(out, className);
            out << ", \"event\": ";
            writeJsonString(out, dangle.event);
            out << "}";
        }
        out << "]}";
    } else {
        out << ",\n  \"wiring\": null";
    }

    out << "\n }";
}

/// UTA-0145: one package's object on one line, for --ndjson. Every raw newline
/// dumpPackage writes is layout -- writeJsonString escapes the whole C0 range --
/// so a newline and the indentation after it are dropped, and no string is
/// touched.
std::string oneLine(std::string_view json) {
    std::string line;
    line.reserve(json.size());
    bool indent = true; // dumpPackage opens with a space
    for (const char ch : json) {
        if (ch == '\n') {
            indent = true;
        } else if (!(indent && ch == ' ')) {
            indent = false;
            line += ch;
        }
    }
    return line;
}

int usage(std::ostream& err) {
    err <<
        "usage: ut-dump --system <UT System dir> [--nav-graph] [--ndjson] <package|directory>...\n"
        "\n"
        "Writes one JSON object per package to stdout. --system points at the\n"
        "install's System directory, which is needed to resolve class ancestry\n"
        "across packages; without it a map's navigation graph is empty rather\n"
        "than wrong, and the run says so.\n"
        "\n"
        "--nav-graph adds every navigation node and reach spec to each map's\n"
        "`nav` object, as `nodeList` and `edgeList`, with the reach flags and\n"
        "collision size each spec carries.\n"
        "\n"
        "--ndjson writes one line per JSON object instead of one document: a\n"
        "header line holding the schema, then each package's object, in the\n"
        "order `packages` lists them. There is no end marker; the exit code\n"
        "says the run finished.\n";
    return 2;
}

} // namespace

int runCli(std::span<const std::string_view> args, std::ostream& out, std::ostream& err) {
    fs::path systemDir;
    bool navGraph = false;
    bool ndjson = false;
    std::vector<fs::path> targets;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "--system") {
            if (i + 1 >= args.size()) {
                return usage(err);
            }
            systemDir = fs::path{std::string{args[++i]}};
        } else if (arg == "--nav-graph") {
            navGraph = true;
        } else if (arg == "--ndjson") {
            ndjson = true;
        } else if (arg == "-h" || arg == "--help") {
            usage(err);
            return 0;
        } else if (arg.starts_with("-")) {
            err << "ut-dump: unknown option " << arg << "\n";
            return usage(err);
        } else {
            targets.emplace_back(std::string{arg});
        }
    }

    if (targets.empty()) {
        return usage(err);
    }

    std::vector<fs::path> files;
    for (const fs::path& target : targets) {
        if (fs::is_directory(target)) {
            for (const fs::directory_entry& entry : fs::directory_iterator{target}) {
                if (entry.is_regular_file()) {
                    files.push_back(entry.path());
                }
            }
        } else {
            files.push_back(target);
        }
    }
    std::sort(files.begin(), files.end());

    SystemPackages system{systemDir};
    if (system.empty()) {
        err << "ut-dump: no System packages found; class ancestry will not "
               "resolve across packages and navigation graphs will be empty\n";
    }

    if (ndjson) {
        // The shape agreed with UT_MonsterHunt, whose reader refuses an unknown
        // schema before it reads a package: the header first, then one line
        // per package, so a consumer can let go of each map once it is read.
        out << "{\"schema\":" << SCHEMA << "}\n";
        for (const fs::path& file : files) {
            std::ostringstream package;
            dumpPackage(package, file, system, navGraph, true);
            out << oneLine(package.str()) << '\n' << std::flush;
        }
        return 0;
    }

    out << "{\n \"schema\": " << SCHEMA << ",\n \"packages\": [\n";
    bool first = true;
    for (const fs::path& file : files) {
        dumpPackage(out, file, system, navGraph, first);
        first = false;
    }
    out << "\n ]\n}\n";
    return 0;
}

} // namespace uta::dump
