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
#include "ubake/Install.h"
#include "unav/Build.h"
#include "unav/Graphs.h"
#include "upkg/Class.h"
#include "upkg/Geometry.h"
#include "upkg/Level.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
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
            // UTA-0144: mapped, not copied.
            auto mapped = uta::fs::MappedFile::open(path->second);
            if (!mapped.has_value()) {
                failed_.insert(key);
                return nullptr;
            }
            const uta::fs::MappedFile& raw = bytes_.emplace(key, std::move(*mapped)).first->second;
            auto package = uta::upkg::Package::open(raw.bytes());
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
    std::map<std::string, uta::fs::MappedFile> bytes_;
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

void writeLocation(std::ostream& out, const std::optional<uta::upkg::Vector3>& location);

/// UTA-0201: the map's BSP surfaces, grouped by the texture they use and the
/// flags they carry.
///
/// GROUPED, not one row per surface: a map holds thousands, and the question
/// this answers is which textures are present and how they are flagged, not
/// where each polygon sits. `drawnNodes` is what makes a group worth having --
/// a surface no node references is in the file and on nobody's screen, which
/// is the case UTA-0188 went looking for and could not ask about.
///
/// The texture is named by its object name ALONE, not qualified by its
/// package. A texture reference is usually an import, and resolving its
/// package means walking the outer chain -- which UTA-0012's body records
/// getting wrong once, yielding the GROUP rather than the package and
/// reporting 587 maps as needing one called "Base". The bare name answers
/// whether a map uses a texture, which is what this is for.
///
/// UTA-0213: `--surface-list` adds `list`, one row per surface in index order,
/// for the question the groups cannot answer -- is THIS surface, here, solid.
/// UT_MonsterHunt's GAME-0008 asks it of fake-wall candidates: a brush's own
/// flags drift from the flags the map was built with, and `polyFlags` here is
/// the built one. `brush` and `brushPoly` name the brush actor and its polygon,
/// which is how a T3D export finds the same face; `base` and `normal` place it.
void writeSurfaces(std::ostream& out, const uta::upkg::Package& package,
                   const uta::upkg::Level& level, bool surfaceList) {
    // `Level::model` comes out of the export's DATA, which Package::open did
    // not validate, so its range is checked rather than trusted -- the same
    // guard ubake's findModel applies, restated here because ut-dump does not
    // link the baker.
    // Both refusals name themselves, as `levelError` does above. A bare null
    // cannot be told from a map with no BSP, and the first run of this hit one
    // of these two branches and could not say which.
    if (level.model.kind() != uta::upkg::ObjectReferenceKind::Export
        || level.model.index() >= package.exports().size()) {
        out << ",\n  \"surfaces\": null,\n  \"surfacesError\": ";
        writeJsonString(out, "the level names no Model export of this map");
        return;
    }
    const auto model = uta::upkg::readModel(package, package.exports()[level.model.index()]);
    if (!model.has_value()) {
        out << ",\n  \"surfaces\": null,\n  \"surfacesError\": ";
        writeJsonString(out, std::string(model.error().message()));
        return;
    }

    // Drawable nodes referencing each surface. Fewer than three vertices draws
    // nothing, which is ubake's own first test over the same array.
    std::vector<long long> nodesOf(model->surfs.size(), 0);
    for (const uta::upkg::BspNode& node : model->nodes) {
        if (node.numVertices < 3) continue;
        if (node.iSurf < 0 || static_cast<std::size_t>(node.iSurf) >= nodesOf.size()) continue;
        ++nodesOf[static_cast<std::size_t>(node.iSurf)];
    }

    // Ordered, so two runs over one map agree and a diff of two maps reads.
    std::map<std::pair<std::string, std::uint32_t>, std::pair<long long, long long>> groups;
    for (std::size_t i = 0; i < model->surfs.size(); ++i) {
        const uta::upkg::BspSurf& surf = model->surfs[i];
        auto& group = groups[{nameOr(package, surf.texture), surf.polyFlags}];
        ++group.first;
        group.second += nodesOf[i];
    }

    out << ",\n  \"surfaces\": {\"total\": " << model->surfs.size() << ", \"byTextureAndFlags\": [";
    bool firstGroup = true;
    for (const auto& [key, counts] : groups) {
        if (!firstGroup) out << ", ";
        firstGroup = false;
        out << "{\"texture\": ";
        writeJsonString(out, key.first);
        out << ", \"polyFlags\": " << key.second << ", \"surfaces\": " << counts.first
            << ", \"drawnNodes\": " << counts.second << "}";
    }
    out << "]";
    if (surfaceList) {
        // An index into points or vectors is file data like `level.model`, so
        // one out of range is written as null rather than trusted.
        const auto at = [](const std::vector<uta::upkg::Vector3>& pool, std::int32_t index) {
            return index >= 0 && static_cast<std::size_t>(index) < pool.size()
                       ? std::optional{pool[static_cast<std::size_t>(index)]}
                       : std::nullopt;
        };
        out << ", \"list\": [";
        for (std::size_t i = 0; i < model->surfs.size(); ++i) {
            const uta::upkg::BspSurf& surf = model->surfs[i];
            if (i != 0) out << ", ";
            out << "{\"index\": " << i << ", \"texture\": ";
            writeJsonString(out, nameOr(package, surf.texture));
            out << ", \"polyFlags\": " << surf.polyFlags << ", \"brush\": ";
            // A surface no brush owns stores a null actor; "?" would read as a
            // name that failed to resolve.
            if (surf.actor.kind() == uta::upkg::ObjectReferenceKind::Null) out << "null";
            else writeJsonString(out, nameOr(package, surf.actor));
            out << ", \"brushPoly\": " << surf.iBrushPoly << ", \"base\": ";
            writeLocation(out, at(model->points, surf.pBase));
            out << ", \"normal\": ";
            writeLocation(out, at(model->vectors, surf.vNormal));
            out << ", \"drawnNodes\": " << nodesOf[i] << "}";
        }
        out << "]";
    }
    out << "}";
}

/// UTA-0189 and UTA-0198: what one actor stores for itself -- its Location,
/// and the three per-node spec lists a NavigationPoint carries. The lists hold
/// the stored slots in slot order, each value the file's own index into the
/// level's reach-spec array; a slot the file does not store is left out.
/// Names are matched case-insensitively: UT99 spells `upstreamPaths` with a
/// lower-case u, and a file keeps its own spelling.
struct StoredFacts {
    std::optional<uta::upkg::Vector3> location;
    std::vector<std::int32_t> paths;
    std::vector<std::int32_t> upstreamPaths;
    std::vector<std::int32_t> prunedPaths;
};

StoredFacts storedFacts(const uta::upkg::Package& package, std::uint32_t exportIndex) {
    StoredFacts facts;
    if (exportIndex >= package.exports().size()) return facts;
    const auto properties = uta::upkg::readProperties(package, package.exports()[exportIndex]);
    if (!properties.has_value()) return facts;
    std::map<std::uint32_t, std::int32_t> paths, upstream, pruned;
    for (const uta::upkg::Property& property : *properties) {
        const auto name = package.name(property.nameIndex);
        if (!name.has_value()) continue;
        const std::string folded = foldCase(*name);
        if (folded == "location" && property.arrayIndex == 0) {
            if (const auto* at = std::get_if<uta::upkg::Vector3>(&property.value)) facts.location = *at;
            continue;
        }
        const auto* value = std::get_if<std::int32_t>(&property.value);
        if (value == nullptr) continue;
        if (folded == "paths") paths[property.arrayIndex] = *value;
        else if (folded == "upstreampaths") upstream[property.arrayIndex] = *value;
        else if (folded == "prunedpaths") pruned[property.arrayIndex] = *value;
    }
    for (const auto& [slot, value] : paths) facts.paths.push_back(value);
    for (const auto& [slot, value] : upstream) facts.upstreamPaths.push_back(value);
    for (const auto& [slot, value] : pruned) facts.prunedPaths.push_back(value);
    return facts;
}

/// `[x, y, z]` in the shortest form that reads back exactly, or `null`.
void writeLocation(std::ostream& out, const std::optional<uta::upkg::Vector3>& location) {
    if (!location.has_value()) {
        out << "null";
        return;
    }
    out << std::format("[{}, {}, {}]", location->x, location->y, location->z);
}

void writeIntArray(std::ostream& out, const std::vector<std::int32_t>& values) {
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) out << (i == 0 ? "" : ", ") << values[i];
    out << ']';
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
        const StoredFacts facts = storedFacts(package, node.exportIndex);
        out << ", \"location\": ";
        writeLocation(out, facts.location);
        out << ", \"paths\": ";
        writeIntArray(out, facts.paths);
        out << ", \"upstreamPaths\": ";
        writeIntArray(out, facts.upstreamPaths);
        out << ", \"prunedPaths\": ";
        writeIntArray(out, facts.prunedPaths);
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
            << ", \"pruned\": " << static_cast<int>(edge.pruned)
            // UTA-0198: what `paths` and its siblings index, so the two join.
            << ", \"spec\": " << edge.spec << "}";
    }
    out << "]";
}

// ---------------------------------------------------------------- UTA-0101
// The 8-bit repair this section needed is inside writeJsonString from
// UTA-0202, so there is no separate writer to reach for.

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
            writeJsonString(out, *text);
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

/// ThingFactory's own default capacity. The factory counts down from it and
/// never reaches zero, so it is no limit and is not summed as a monster count.
constexpr std::int32_t FACTORY_NO_LIMIT = 1000000;

/// What a class family makes an actor, for the monster total.
struct ActorKind {
    bool resolved = false; ///< the whole class family was found
    bool factory = false;  ///< descends from ThingFactory
    bool pawn = false;     ///< descends from ScriptedPawn
    /// A pawn that descends from neither Nali nor Cow -- the rule agreed with
    /// UT_MonsterHunt, whose MHMonsterCount mutator applies it (UTA-0173).
    bool monster = false;
    std::optional<std::int32_t> defaultCapacity;
    /// The class family's `prototype`, and the package its reference is read in.
    std::optional<uta::upkg::ObjectReference> defaultPrototype;
    const uta::upkg::Package* prototypeOrigin = nullptr;
};

ActorKind kindOf(const uta::upkg::Package& package, std::string_view packageName,
                 uta::upkg::ObjectReference classReference,
                 const uta::upkg::PackageResolver& resolver) {
    ActorKind kind;
    const auto site = uta::upkg::resolveClass(package, packageName, classReference, resolver);
    if (!site.has_value() || site->resolved.package == nullptr) return kind;
    const auto ancestry =
        uta::upkg::readAncestry(*site->resolved.package, *site->resolved.entry, resolver);
    if (!ancestry.has_value()) return kind;
    bool friendly = false;
    for (const uta::upkg::ResolvedClass& link : ancestry->chain) {
        const auto name = link.package->name(link.entry->objectName);
        if (!name.has_value()) continue;
        const std::string folded = foldCase(*name);
        kind.factory = kind.factory || folded == "thingfactory";
        kind.pawn = kind.pawn || folded == "scriptedpawn";
        friendly = friendly || folded == "nali" || folded == "cow";
    }
    // Nali and Cow sit below ScriptedPawn, so a chain that reached it has
    // already passed either.
    kind.monster = kind.pawn && !friendly;
    // A chain cut short by a missing package may lack the ancestor that decides.
    kind.resolved = ancestry->end == uta::upkg::AncestryEnd::Root;
    if (kind.factory) {
        if (const auto defaults = uta::upkg::effectiveDefaults(*ancestry); defaults.has_value()) {
            for (const uta::upkg::EffectiveProperty& effective : *defaults) {
                if (effective.property.arrayIndex != 0) continue;
                const std::string folded = foldCase(effective.name);
                if (folded == "capacity") {
                    if (const auto* value = std::get_if<std::int32_t>(&effective.property.value))
                        kind.defaultCapacity = *value;
                } else if (folded == "prototype") {
                    if (const auto* value = std::get_if<uta::upkg::ObjectReference>(&effective.property.value)) {
                        kind.defaultPrototype = *value;
                        kind.prototypeOrigin = effective.origin;
                    }
                }
            }
        }
    }
    return kind;
}

/// Each class family's ActorKind, walked once per class and shared by the
/// monster total and `level.chainsUnresolved` -- UTA-0012 SS 13: the count
/// adds no ancestry walk. Keyed by the package a class reference is read in
/// and the raw reference.
class ClassKinds {
public:
    explicit ClassKinds(const uta::upkg::PackageResolver& resolver) : resolver_(resolver) {}

    const ActorKind& of(const uta::upkg::Package& package, std::string_view packageName,
                        uta::upkg::ObjectReference reference) {
        auto found = kinds_.find({&package, reference.raw()});
        if (found == kinds_.end())
            found = kinds_.emplace(std::pair{&package, reference.raw()},
                                   kindOf(package, packageName, reference, resolver_))
                        .first;
        return found->second;
    }

private:
    const uta::upkg::PackageResolver& resolver_;
    std::map<std::pair<const uta::upkg::Package*, std::int32_t>, ActorKind> kinds_;
};

/// UTA-0012 SS 4.8: the level's actors, each export once, whose class chain
/// does not end at the root. An actor with no class has no chain to cut short
/// and does not count, as writeActorWiring reports it -- the same population
/// as its `chainsUnresolved`, which INV-8 holds.
long long unresolvedChains(const uta::upkg::Package& map, std::string_view mapName,
                           const uta::upkg::Level& level, ClassKinds& kinds) {
    long long unresolved = 0;
    std::set<std::uint32_t> seen;
    for (const uta::upkg::ObjectReference slot : level.actors) {
        if (slot.kind() != uta::upkg::ObjectReferenceKind::Export || slot.index() >= map.exports().size()
            || !seen.insert(slot.index()).second)
            continue;
        const uta::upkg::ExportEntry& entry = map.exports()[slot.index()];
        if (entry.objectClass.kind() != uta::upkg::ObjectReferenceKind::Null
            && !kinds.of(map, mapName, entry.objectClass).resolved)
            ++unresolved;
    }
    return unresolved;
}

/// The whole-map monster total: the capacity of every ThingFactory descendant
/// whose prototype is a monster, the actor's own else its class family's, and
/// every monster placed in the map. A monster is a ScriptedPawn descended from
/// neither Nali nor Cow; a factory making anything else is not counted at all.
/// A capacity of 0 or below sends one monster, since Spawning's Begin runs
/// Timer once and StartBuilding re-arms only while capacity > 0. A factory
/// with no limit, or with no capacity found, is counted apart rather than
/// summed. An actor, or a factory's prototype, whose class family did not
/// resolve cannot be sorted, and is counted too, so an incomplete total says so.
void writeMonsters(std::ostream& out, const uta::upkg::Package& map, std::string_view mapName,
                   const uta::upkg::Level& level, ClassKinds& kinds) {
    long long factories = 0, capacity = 0, unlimited = 0, unknown = 0, pawns = 0, unresolved = 0;
    const auto kindIn = [&](const uta::upkg::Package& package, std::string_view packageName,
                            uta::upkg::ObjectReference reference) -> const ActorKind& {
        return kinds.of(package, packageName, reference);
    };
    std::set<std::uint32_t> seen;
    for (const uta::upkg::ObjectReference slot : level.actors) {
        // A map can name one actor in two slots (UTA-0124); it is one actor.
        if (slot.kind() != uta::upkg::ObjectReferenceKind::Export || slot.index() >= map.exports().size()
            || !seen.insert(slot.index()).second)
            continue;
        const uta::upkg::ExportEntry& entry = map.exports()[slot.index()];
        if (entry.objectClass.kind() == uta::upkg::ObjectReferenceKind::Null) continue;
        const ActorKind& kind = kindIn(map, mapName, entry.objectClass);
        if (kind.monster) ++pawns;
        if (!kind.factory) {
            if (!kind.resolved && !kind.pawn) ++unresolved;
            continue;
        }
        std::optional<std::int32_t> value = kind.defaultCapacity;
        std::optional<uta::upkg::ObjectReference> prototype = kind.defaultPrototype;
        const uta::upkg::Package* prototypeOrigin = kind.prototypeOrigin;
        if (const auto properties = uta::upkg::readProperties(map, entry); properties.has_value()) {
            for (const uta::upkg::Property& property : *properties) {
                const auto name = map.name(property.nameIndex);
                if (!name.has_value() || property.arrayIndex != 0) continue;
                const std::string folded = foldCase(*name);
                if (folded == "capacity") {
                    if (const auto* own = std::get_if<std::int32_t>(&property.value)) value = *own;
                } else if (folded == "prototype") {
                    if (const auto* own = std::get_if<uta::upkg::ObjectReference>(&property.value)) {
                        prototype = *own;
                        prototypeOrigin = &map;
                    }
                }
            }
        }
        if (!prototype.has_value() || prototype->kind() == uta::upkg::ObjectReferenceKind::Null) continue;
        // A default read in another package is labelled by no name; the label
        // is the site's, which kindOf does not read.
        const ActorKind& made =
            kindIn(*prototypeOrigin, prototypeOrigin == &map ? mapName : std::string_view{}, *prototype);
        if (!made.monster) {
            if (!made.resolved && !made.pawn) ++unresolved;
            continue;
        }
        ++factories;
        if (!value.has_value()) {
            ++unknown;
        } else if (*value >= FACTORY_NO_LIMIT) {
            ++unlimited;
        } else {
            capacity += std::max(*value, 1);
        }
    }
    out << ",\n  \"monsters\": {\"factories\": " << factories << ", \"capacity\": " << capacity
        << ", \"unlimitedFactories\": " << unlimited << ", \"unknownCapacityFactories\": " << unknown
        << ", \"placedPawns\": " << pawns << ", \"unresolvedActors\": " << unresolved << "}";
}

// ------------------------------------------------------------------ UTA-0172
// Per-actor event wiring -- docs/specs/UTA-0172-actor-event-wiring.md.
//
// The wiring graph above carries counts; a consumer deciding whether ANYTHING
// can switch a given actor on needs the identities the counts discard. This
// emits them, behind --wiring-graph because the array is roughly the map's
// actor count.

/// The six naming properties, plus Tag, as the spec's SS 4.3 spells them.
/// Folded for comparison; the emitted key is the spelling here.
struct WiringFields {
    std::string tag;
    std::string event;
    std::string bumpEvent;
    std::string playerBumpEvent;
    std::string firstHatePlayerEvent;
    std::string monsterEndTag;
    std::map<std::uint32_t, std::string> outEvents;
    std::optional<bool> initiallyActive; // nullopt: the class family has no such property
    std::string initialState;
    // UTA-0130: what decides whether an exit is won by shooting it. Emitted
    // on `exits` only. nullopt: neither the actor nor its class sets one.
    std::optional<int> triggerType;
    std::optional<float> damageThreshold;
};

/// A Name or String property's text, resolved against the package its indices
/// belong to. INV-9 of docs/specs/UTA-0005-class-tables-and-ancestry.md is why
/// `origin` matters: a name index is a position in ONE package's name table,
/// so a default inherited from another package must be resolved against that
/// package or it reads as a different name entirely.
std::optional<std::string> textValue(const uta::upkg::Package& origin,
                                     const uta::upkg::PropertyValue& value) {
    if (const auto* ref = std::get_if<uta::upkg::NameRef>(&value)) {
        const auto text = origin.name(ref->index);
        if (!text.has_value()) return std::nullopt;
        return std::string{*text};
    }
    if (const auto* text = std::get_if<std::string>(&value)) return *text;
    return std::nullopt;
}

/// Fold one property into `fields`, whichever package it was read in.
void takeWiringProperty(WiringFields& fields, const uta::upkg::Package& origin,
                        std::string_view name, const uta::upkg::Property& property) {
    const std::string folded = foldCase(name);
    if (folded == "outevents") {
        if (auto text = textValue(origin, property.value); text.has_value() && !text->empty())
            fields.outEvents[property.arrayIndex] = std::move(*text);
        return;
    }
    // Every other field is scalar; an array index above 0 is not ours.
    if (property.arrayIndex != 0) return;
    if (folded == "binitiallyactive") {
        if (const auto* set = std::get_if<bool>(&property.value)) fields.initiallyActive = *set;
        return;
    }
    if (folded == "triggertype") {
        if (const auto* set = std::get_if<std::uint8_t>(&property.value)) fields.triggerType = *set;
        return;
    }
    if (folded == "damagethreshold") {
        if (const auto* set = std::get_if<float>(&property.value)) fields.damageThreshold = *set;
        return;
    }
    auto text = textValue(origin, property.value);
    if (!text.has_value()) return;
    if (folded == "tag") fields.tag = std::move(*text);
    else if (folded == "event") fields.event = std::move(*text);
    else if (folded == "bumpevent") fields.bumpEvent = std::move(*text);
    else if (folded == "playerbumpevent") fields.playerBumpEvent = std::move(*text);
    else if (folded == "firsthateplayerevent") fields.firstHatePlayerEvent = std::move(*text);
    else if (folded == "monsterendtag") fields.monsterEndTag = std::move(*text);
    else if (folded == "initialstate") fields.initialState = std::move(*text);
}

/// What a CLASS contributes: its chain, how the walk ended, and the event
/// fields its family defaults. Cached per class reference because a map's
/// actors share very few classes -- MH-GolgothaAL_fix has 2905 actors, and
/// walking the ancestry once per actor rather than once per class took a
/// whole-library sweep from seconds to 55 minutes (measured 2026-09-21).
/// `writeMonsters` above caches the same way for the same reason.
struct ClassWiring {
    std::vector<std::string> chain;
    std::string end = "root";
    WiringFields defaults;
};

/// `wiring.actors` -- every actor, with its class chain and its event fields.
/// Returns the number whose ancestry walk did not reach a root, which the
/// caller reports as `chainsUnresolved` (SS 4.2).
long long writeActorWiring(std::ostream& out, const uta::upkg::Package& map,
                           std::string_view mapName, const uta::upkg::Level& level,
                           const uta::upkg::PackageResolver& resolver) {
    out << ", \"actors\": [";
    long long unresolved = 0;
    bool firstActor = true;

    // By the package a class reference is read in and the raw reference --
    // writeMonsters' key, for writeMonsters' reason.
    std::map<std::pair<const uta::upkg::Package*, std::int32_t>, ClassWiring> classes;

    // One actor per export, in export-table order. A map can name one actor in
    // two slots (UTA-0124) and it is one actor, so the set is deduplicated --
    // and ordering by export index is what makes `index` an identity a
    // consumer can key on, which `name` is not (SS 4.3).
    std::set<std::uint32_t> actorExports;
    for (const uta::upkg::ObjectReference slot : level.actors) {
        if (slot.kind() != uta::upkg::ObjectReferenceKind::Export) continue;
        if (slot.index() >= map.exports().size()) continue;
        actorExports.insert(slot.index());
    }

    for (const std::uint32_t exportIndex : actorExports) {
        const uta::upkg::ExportEntry& entry = map.exports()[exportIndex];

        std::string className = "?";
        if (const auto found = map.objectName(entry.objectClass); found.has_value())
            className = std::string{*found};
        std::string actorName = "?";
        if (const auto found = map.name(entry.objectName); found.has_value())
            actorName = std::string{*found};

        // The class chain, and the honesty field that says whether it is
        // complete. A chain cut short is NOT a negative answer -- SS 4.4.
        const ClassWiring* classWiring = nullptr;
        if (entry.objectClass.kind() != uta::upkg::ObjectReferenceKind::Null) {
            const std::pair key{&map, entry.objectClass.raw()};
            auto found = classes.find(key);
            if (found == classes.end()) {
                ClassWiring built;
                const auto site = uta::upkg::resolveClass(map, mapName, entry.objectClass, resolver);
                if (site.has_value() && site->resolved.package != nullptr) {
                    // Walked once per CLASS and used twice -- for the chain
                    // and for the defaults merge.
                    const auto ancestry = uta::upkg::readAncestry(*site->resolved.package,
                                                                  *site->resolved.entry, resolver);
                    if (ancestry.has_value()) {
                        for (const uta::upkg::ResolvedClass& link : ancestry->chain) {
                            const auto name = link.package->name(link.entry->objectName);
                            built.chain.emplace_back(name.has_value() ? std::string{*name} : "?");
                        }
                        switch (ancestry->end) {
                        case uta::upkg::AncestryEnd::Root: built.end = "root"; break;
                        case uta::upkg::AncestryEnd::PackageMissing: built.end = "packageMissing"; break;
                        case uta::upkg::AncestryEnd::ClassMissing: built.end = "classMissing"; break;
                        }
                        // Defaults first, then the actor's own stored values
                        // over the top -- SS 4.5. Reading stored properties
                        // alone would emit "" for every exit's Tag, since Tag
                        // defaults to the class name and is stored on none.
                        if (const auto defaults = uta::upkg::effectiveDefaults(*ancestry);
                            defaults.has_value()) {
                            for (const uta::upkg::EffectiveProperty& effective : *defaults) {
                                if (effective.origin == nullptr) continue;
                                takeWiringProperty(built.defaults, *effective.origin, effective.name,
                                                   effective.property);
                            }
                        }
                    }
                } else {
                    built.end = "classMissing";
                }
                found = classes.emplace(key, std::move(built)).first;
            }
            classWiring = &found->second;
        }

        static const ClassWiring EMPTY_CLASS;
        if (classWiring == nullptr) classWiring = &EMPTY_CLASS;
        const std::vector<std::string>& chain = classWiring->chain;
        const std::string& chainEnd = classWiring->end;
        WiringFields fields = classWiring->defaults;
        if (chainEnd != "root") ++unresolved;

        if (const auto properties = uta::upkg::readProperties(map, entry); properties.has_value()) {
            for (const uta::upkg::Property& property : *properties) {
                const auto name = map.name(property.nameIndex);
                if (!name.has_value()) continue;
                takeWiringProperty(fields, map, *name, property);
            }
        }

        if (!firstActor) out << ", ";
        firstActor = false;
        out << "{\"index\": " << exportIndex << ", \"name\": ";
        writeJsonString(out, actorName);
        out << ", \"class\": ";
        writeJsonString(out, className);
        out << ", \"classChain\": [";
        for (std::size_t i = 0; i < chain.size(); ++i) {
            if (i != 0) out << ", ";
            writeJsonString(out, chain[i]);
        }
        out << "], \"chainEnd\": ";
        writeJsonString(out, chainEnd);
        out << ", \"tag\": ";
        writeJsonString(out, fields.tag);
        out << ", \"events\": {\"Event\": ";
        writeJsonString(out, fields.event);
        out << ", \"OutEvents\": {";
        bool firstOut = true;
        for (const auto& [index, value] : fields.outEvents) {
            if (!firstOut) out << ", ";
            firstOut = false;
            writeJsonString(out, std::to_string(index));
            out << ": ";
            writeJsonString(out, value);
        }
        out << "}, \"BumpEvent\": ";
        writeJsonString(out, fields.bumpEvent);
        out << ", \"PlayerBumpEvent\": ";
        writeJsonString(out, fields.playerBumpEvent);
        out << ", \"FirstHatePlayerEvent\": ";
        writeJsonString(out, fields.firstHatePlayerEvent);
        out << ", \"MonsterEndTag\": ";
        writeJsonString(out, fields.monsterEndTag);
        out << "}, \"bInitiallyActive\": ";
        // null, never false, where the class family has no such property --
        // INV-9. Collapsing the two makes "no such property" and "switched
        // off" the same value, and off-ness is half the consumer's rule.
        if (fields.initiallyActive.has_value()) out << (*fields.initiallyActive ? "true" : "false");
        else out << "null";
        out << ", \"initialState\": ";
        writeJsonString(out, fields.initialState);
        out << "}";
    }

    out << "]";
    return unresolved;
}

/// UTA-0189: every actor of a MonsterEnd-family class, which a map is won by
/// triggering. These are Triggers, not NavigationPoints, so `nodeList` never
/// holds them. Matched on the actor's own class name, case-insensitively.
/// `tag` is resolved through the class family's defaults, as `wiring.actors`
/// resolves it: a Tag is usually the class default and stored on no actor.
void writeExits(std::ostream& out, const uta::upkg::Package& map, std::string_view mapName,
                const uta::upkg::Level& level, const uta::upkg::PackageResolver& resolver) {
    static const std::set<std::string> EXIT_CLASSES{"monsterend", "monsterendsb", "monsterarenaend"};
    std::set<std::uint32_t> actorExports;
    for (const uta::upkg::ObjectReference slot : level.actors) {
        if (slot.kind() == uta::upkg::ObjectReferenceKind::Export && slot.index() < map.exports().size())
            actorExports.insert(slot.index());
    }
    out << ",\n  \"exits\": [";
    bool first = true;
    for (const std::uint32_t exportIndex : actorExports) {
        const uta::upkg::ExportEntry& entry = map.exports()[exportIndex];
        const auto className = map.objectName(entry.objectClass);
        if (!className.has_value() || !EXIT_CLASSES.contains(foldCase(*className))) continue;

        WiringFields fields;
        if (const auto site = uta::upkg::resolveClass(map, mapName, entry.objectClass, resolver);
            site.has_value() && site->resolved.package != nullptr) {
            if (const auto ancestry =
                    uta::upkg::readAncestry(*site->resolved.package, *site->resolved.entry, resolver);
                ancestry.has_value()) {
                if (const auto defaults = uta::upkg::effectiveDefaults(*ancestry); defaults.has_value()) {
                    for (const uta::upkg::EffectiveProperty& effective : *defaults) {
                        if (effective.origin == nullptr) continue;
                        takeWiringProperty(fields, *effective.origin, effective.name, effective.property);
                    }
                }
            }
        }
        if (const auto properties = uta::upkg::readProperties(map, entry); properties.has_value()) {
            for (const uta::upkg::Property& property : *properties) {
                if (const auto name = map.name(property.nameIndex); name.has_value())
                    takeWiringProperty(fields, map, *name, property);
            }
        }

        std::string actorName = "?";
        if (const auto found = map.name(entry.objectName); found.has_value()) actorName = std::string{*found};
        out << (first ? "" : ", ") << "{\"export\": " << exportIndex << ", \"name\": ";
        first = false;
        writeJsonString(out, actorName);
        out << ", \"class\": ";
        writeJsonString(out, std::string{*className});
        out << ", \"location\": ";
        writeLocation(out, storedFacts(map, exportIndex).location);
        out << ", \"tag\": ";
        writeJsonString(out, fields.tag);
        // UTA-0130: MonsterEndSB's TakeDamage wins the map only with
        // bInitiallyActive, TriggerType TT_Shoot (4) and a hit of at least
        // DamageThreshold. null: nothing in the file sets it.
        out << ", \"triggerType\": ";
        if (fields.triggerType.has_value()) out << *fields.triggerType;
        else out << "null";
        out << ", \"damageThreshold\": ";
        if (fields.damageThreshold.has_value()) out << std::format("{}", *fields.damageThreshold);
        else out << "null";
        out << ", \"bInitiallyActive\": ";
        if (fields.initiallyActive.has_value()) out << (*fields.initiallyActive ? "true" : "false");
        else out << "null";
        out << "}";
    }
    out << "]";
}

void dumpPackage(std::ostream& out, const fs::path& path, const uta::upkg::PackageResolver& resolver,
                 bool navGraph, bool wiringGraph, bool surfaceList, bool first) {
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

    ClassKinds kinds{resolver};

    out << ",\n  \"level\": {";
    out << "\"actors\": " << level->actors.size();
    out << ", \"rawSlots\": " << level->rawSlotCount;
    out << ", \"reachSpecs\": " << level->reachSpecs.size();
    out << ", \"chainsUnresolved\": " << unresolvedChains(*package, path.stem().string(), *level, kinds);
    out << "}";

    writeSurfaces(out, *package, *level, surfaceList);

    writeCredits(out, "levelInfo", *package, levelInfoOf(*package, *level));
    writeCredits(out, "levelSummary", *package, firstExportOf(*package, "LevelSummary"));
    writeMonsters(out, *package, path.stem().string(), *level, kinds);

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
        out << "]";
        if (wiringGraph) {
            // Emitted after `dangling` so the counts stay first for a reader
            // scanning the head of the object. UTA-0172.
            std::ostringstream actors;
            const long long unresolvedChains =
                writeActorWiring(actors, *package, path.stem().string(), *level, resolver);
            out << ", \"chainsUnresolved\": " << unresolvedChains << actors.str();
        }
        out << "}";
    } else {
        out << ",\n  \"wiring\": null";
    }
    writeExits(out, *package, path.stem().string(), *level, resolver);

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
        "usage: ut-dump (--install <UT dir> | --system <UT System dir>)\n"
        "                [--nav-graph] [--wiring-graph] [--surface-list] [--ndjson]\n"
        "                <package|directory>...\n"
        "\n"
        "Writes one JSON object per package to stdout. Class ancestry crosses\n"
        "packages, so it needs the install: without one a map's navigation graph\n"
        "is empty rather than wrong, and the run says so.\n"
        "\n"
        "--install points at the install's root and searches it as the game and\n"
        "ut-bake do: System, Maps, Textures, Sounds and Music, in that order.\n"
        "Some maps' classes live in a texture package, which only this finds.\n"
        "--system points at the System directory and reads its .u files only.\n"
        "\n"
        "--nav-graph adds every navigation node and reach spec to each map's\n"
        "`nav` object, as `nodeList` and `edgeList`, with the reach flags and\n"
        "collision size each spec carries.\n"
        "\n"
        "--wiring-graph adds every actor to each map's `wiring` object, as\n"
        "`actors`, with its class chain, Tag and event properties resolved\n"
        "through the class family's defaults. It answers whether anything in\n"
        "the map can switch a given actor on. The array is roughly the map's\n"
        "actor count, which is why it is opt-in.\n"
        "\n"
        "--surface-list adds every BSP surface to each map's `surfaces` object,\n"
        "as `list`, in index order: its texture, built polyFlags, brush actor\n"
        "and polygon, base point, normal and drawn node count.\n"
        "\n"
        "Packages are listed in path order, not argument order: key on each\n"
        "one's `file`. docs/specs/UTA-0012-ut-dump-output-shape.md is the\n"
        "output's contract.\n"
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
    fs::path installDir;
    bool navGraph = false;
    bool wiringGraph = false;
    bool surfaceList = false;
    bool ndjson = false;
    std::vector<fs::path> targets;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "--system") {
            if (i + 1 >= args.size()) {
                return usage(err);
            }
            systemDir = fs::path{std::string{args[++i]}};
        } else if (arg == "--install") {
            if (i + 1 >= args.size()) {
                return usage(err);
            }
            installDir = fs::path{std::string{args[++i]}};
        } else if (arg == "--nav-graph") {
            navGraph = true;
        } else if (arg == "--wiring-graph") {
            wiringGraph = true;
        } else if (arg == "--surface-list") {
            surfaceList = true;
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
    if (!systemDir.empty() && !installDir.empty()) {
        err << "ut-dump: give --install or --system, not both\n";
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

    // UTA-0206: --install searches the install as ut-bake does, through the
    // same code. Both kept alive for the run: the resolver points into them.
    SystemPackages system{systemDir};
    std::optional<uta::ubake::Install> install;
    uta::upkg::PackageResolver resolver = system.resolver();
    bool found = !system.empty();
    if (!installDir.empty()) {
        if (auto opened = uta::ubake::Install::open(installDir); opened.has_value()) {
            install.emplace(std::move(*opened));
            resolver = install->resolver();
            const auto core = resolver("core");
            found = core.has_value() && *core != nullptr;
        }
    }
    if (!found) {
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
            dumpPackage(package, file, resolver, navGraph, wiringGraph, surfaceList, true);
            out << oneLine(package.str()) << '\n' << std::flush;
        }
        return 0;
    }

    out << "{\n \"schema\": " << SCHEMA << ",\n \"packages\": [\n";
    bool first = true;
    for (const fs::path& file : files) {
        dumpPackage(out, file, resolver, navGraph, wiringGraph, surfaceList, first);
        first = false;
    }
    out << "\n ]\n}\n";
    return 0;
}

} // namespace uta::dump
