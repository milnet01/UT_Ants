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

#include "core/FileSystem.h"
#include "unav/Build.h"
#include "unav/Graphs.h"
#include "upkg/Class.h"
#include "upkg/Level.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int SCHEMA = 1;

// ---------------------------------------------------------------- JSON out
//
// Hand-written rather than a dependency: the output is a fixed shape this file
// owns entirely, and `docs/design.md` keeps the dependency list short on
// purpose. Only the escapes JSON requires, plus the C0 range, which a package
// name can contain and which would otherwise emit invalid JSON.
void writeJsonString(std::ostream& out, std::string_view text) {
    out << '"';
    for (const char raw : text) {
        const auto ch = static_cast<unsigned char>(raw);
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20) {
                char buf[7];
                std::snprintf(buf, sizeof buf, "\\u%04x", ch);
                out << buf;
            } else {
                out << raw;
            }
        }
    }
    out << '"';
}

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

/// Every package this one imports, by name, de-duplicated and folded.
std::set<std::string> importedPackages(const uta::upkg::Package& package) {
    std::set<std::string> out;
    for (const uta::upkg::ImportEntry& entry : package.imports()) {
        // An import whose outer is null names a PACKAGE. One with an outer
        // names an object INSIDE a package, and the chain can be more than one
        // link long: a texture is `Package.Group.Texture`, so the immediate
        // outer is the GROUP and only the outermost is the file that has to be
        // on disk. Reading the immediate outer instead reports group names --
        // Base, Floor, Wall -- as missing dependencies, which is what this
        // loop was doing until it was checked against the install.
        const uta::upkg::ImportEntry* current = &entry;
        for (std::size_t hops = 0; hops <= package.imports().size(); ++hops) {
            const uta::upkg::ObjectReference outer = current->outer;
            if (outer.kind() == uta::upkg::ObjectReferenceKind::Null) {
                const auto name = package.name(current->objectName);
                if (name.has_value()) {
                    out.emplace(*name);
                }
                break;
            }
            if (outer.kind() != uta::upkg::ObjectReferenceKind::Import
                || outer.index() >= package.imports().size()) {
                break; // an outer in the export table is this package's own
            }
            current = &package.imports()[outer.index()];
        }
    }
    return out;
}

void dumpPackage(std::ostream& out, const fs::path& path, SystemPackages& system,
                 bool first) {
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

    out << ",\n  \"importedPackages\": [";
    bool firstDep = true;
    for (const std::string& dep : importedPackages(*package)) {
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
            << ", \"nodesWithNoExit\": " << isolated << "}";
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

int usage() {
    std::cerr <<
        "usage: ut-dump --system <UT System dir> <package|directory>...\n"
        "\n"
        "Writes one JSON object per package to stdout. --system points at the\n"
        "install's System directory, which is needed to resolve class ancestry\n"
        "across packages; without it a map's navigation graph is empty rather\n"
        "than wrong, and the run says so.\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    fs::path systemDir;
    std::vector<fs::path> targets;

    for (int i = 1; i < argc; ++i) {
        const std::string arg{argv[i]};
        if (arg == "--system") {
            if (i + 1 >= argc) {
                return usage();
            }
            systemDir = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        } else if (arg.starts_with("-")) {
            std::cerr << "ut-dump: unknown option " << arg << "\n";
            return usage();
        } else {
            targets.emplace_back(arg);
        }
    }

    if (targets.empty()) {
        return usage();
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
        std::cerr << "ut-dump: no System packages found; class ancestry will not "
                     "resolve across packages and navigation graphs will be empty\n";
    }

    std::cout << "{\n \"schema\": " << SCHEMA << ",\n \"packages\": [\n";
    bool first = true;
    for (const fs::path& file : files) {
        dumpPackage(std::cout, file, system, first);
        first = false;
    }
    std::cout << "\n ]\n}\n";
    return 0;
}
