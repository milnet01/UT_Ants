#include "unav/Build.h"

#include "upkg/Properties.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace uta::unav {
namespace {

using upkg::ExportEntry;
using upkg::ObjectReference;
using upkg::ObjectReferenceKind;
using upkg::Package;

constexpr std::string_view NAVIGATION_POINT = "NavigationPoint";
constexpr std::string_view TAG_PROPERTY = "Tag";
constexpr std::string_view EVENT_PROPERTY = "Event";

/// No node holds this position, so it is what an export index maps to when the
/// actor is not a node. `nodes` can never be this long: it is bounded by the
/// export table, whose count is a signed 32-bit field.
constexpr std::uint32_t NOT_A_NODE = std::numeric_limits<std::uint32_t>::max();

/// How far an import's outer chain is followed before giving up. The chain is
/// package/group/object, so this is far more than any real package needs; it is
/// here because a malformed chain must terminate.
constexpr int OUTER_DEPTH_CAP = 32;

/// A class export, recognised by a NULL class reference -- exactly the set
/// `readProperties` refuses with InvalidArgument (`upkg/Properties.h`). SS 4.3
/// makes skipping these required rather than an optimisation: a map defining
/// its own classes is the ordinary case, so a scan that does not skip them
/// fails on ordinary content.
bool isClassExport(const ExportEntry& entry) {
    return entry.objectClass.kind() == ObjectReferenceKind::Null;
}

std::string foldCase(std::string_view value) {
    std::string folded{value};
    for (char& character : folded) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return folded;
}

/// The package an import ultimately lives in: follow its outer chain to the
/// root, which is where the package name sits.
Result<std::string> importPackageName(const Package& package, ObjectReference reference) {
    for (int depth = 0; depth < OUTER_DEPTH_CAP; ++depth) {
        if (reference.kind() != ObjectReferenceKind::Import) {
            return std::string{};
        }
        const upkg::ImportEntry& import = package.imports()[reference.index()];
        if (import.outer.kind() == ObjectReferenceKind::Null) {
            UTA_TRY(const std::string_view name, package.name(import.objectName));
            return std::string{name};
        }
        reference = import.outer;
    }
    return std::string{};
}

/// Whether an actor's class descends from `NavigationPoint`.
///
/// SS 4.3 measured why this must walk ancestry rather than match a name list:
/// no export of the literal class carries a `Paths` entry, and the set of
/// subclasses does not close.
///
/// Cached per CLASS, not per actor, which is what makes it affordable: a map
/// holds thousands of actors of a few dozen classes, and the walk is what
/// costs.
Result<bool> descendsFromNavigationPoint(const Package& package, const ExportEntry& actor,
                                        const upkg::PackageResolver& resolver,
                                        std::map<std::string, bool>& cache) {
    UTA_TRY(const std::string_view className, package.objectName(actor.objectClass));
    const bool imported = actor.objectClass.kind() == ObjectReferenceKind::Import;

    // SS 4.3: fold the package name to lower case BEFORE calling the resolver.
    // `readAncestry` folds for its own calls, but this call is outside it, so
    // the fold is this builder's own and nothing else performs it. Skip it and
    // `BotPack` misses `BotPack.u` on a case-sensitive filesystem: every actor
    // of a class in that package fails the descent test, its nodes vanish, its
    // reach specs are then discarded as naming non-nodes, and the result is a
    // nearly empty graph with no error, on Linux only.
    std::string home;
    if (imported) {
        UTA_TRY(home, importPackageName(package, actor.objectClass));
        home = foldCase(home);
    } else {
        // Not a package name, so it cannot collide with one. The cache lives
        // for one call, so a map-local class is unambiguous within it.
        home = "<this package>";
    }

    const std::string key = home + "/" + std::string{className};
    if (const auto found = cache.find(key); found != cache.end()) {
        return found->second;
    }

    // `readAncestry` is entered from a class EXPORT, and a stock map actor's
    // class is an IMPORT -- `Engine.PathNode`, not a class in the map. `upkg`
    // exposes no call for that resolution, so SS 4.3 makes it this builder's
    // own step.
    const Package* classHome = nullptr;
    const ExportEntry* classExport = nullptr;
    if (!imported) {
        // In range because `Package::open` validated every reference in the
        // tables -- UTA-0003 INV-6.
        classHome = &package;
        classExport = &package.exports()[actor.objectClass.index()];
    } else if (!home.empty()) {
        UTA_TRY(const Package* const resolved, resolver(home));
        if (resolved != nullptr) {
            for (const ExportEntry& candidate : resolved->exports()) {
                // `readClass` refuses a class export with no serialised data,
                // so one is no use as a walk's entry.
                if (!isClassExport(candidate) || candidate.serialSize == 0) {
                    continue;
                }
                UTA_TRY(const std::string_view name, resolved->name(candidate.objectName));
                if (name == className) {
                    classHome = resolved;
                    classExport = &candidate;
                    break;
                }
            }
        }
    }

    bool descends = false;
    if (classHome != nullptr && classExport != nullptr) {
        // An ancestry that ends PackageMissing or ClassMissing is a SUCCESSFUL
        // end (UTA-0005 INV-7): the actor is simply not a navigation node, and
        // SS 6 keeps the row. A MalformedData end is a different fact and
        // propagates.
        UTA_TRY(const upkg::Ancestry ancestry,
                upkg::readAncestry(*classHome, *classExport, resolver));
        for (const upkg::ResolvedClass& link : ancestry.chain) {
            UTA_TRY(const std::string_view name, link.package->name(link.entry->objectName));
            if (name == NAVIGATION_POINT) {
                descends = true;
                break;
            }
        }
    }
    return cache.emplace(key, descends).first->second;
}

/// A reach spec's endpoint as a NODE POSITION, or nothing when one of SS 4.5's
/// four cases applies: the reference is Null, it is an Import, it is an Export
/// that is not a navigation node, or it is an Export past the end of the table.
///
/// The `kind()` guard is SS 4.2's, and it is not defensive coding against a
/// case that cannot arise -- null endpoints are real content. `index()` is the
/// export position ONLY when `kind()` is Export: for a null reference the value
/// is meaningless and returns 0, and a negative one is an IMPORT index. So an
/// unguarded call aliases every null endpoint onto export 0 and every import
/// endpoint onto an unrelated export. That is a well-formed edge to the WRONG
/// ACTOR, which the discard rule cannot catch, because the index it produces is
/// usually a real node.
std::optional<std::uint32_t> endpointNode(ObjectReference reference,
                                          const std::vector<std::uint32_t>& nodeOfExport) {
    if (reference.kind() != ObjectReferenceKind::Export) {
        return std::nullopt;
    }
    // Required even though no endpoint in the reference install is out of
    // range: `Package::open` validates the three TABLES, and a reach spec's
    // endpoints live in an export's serialised DATA, which that validation does
    // not cover. Without it the first malformed file reads out of bounds.
    const std::uint32_t index = reference.index();
    if (index >= nodeOfExport.size() || nodeOfExport[index] == NOT_A_NODE) {
        return std::nullopt;
    }
    return nodeOfExport[index];
}

/// Give each node the offset and length of its own run in `edges`, which is
/// what makes SS 4.7's queries a span rather than a search. `edges` must
/// already be grouped by the key `keyOf` reads.
template <typename Node, typename Edge, typename KeyOf, typename Assign>
void fillRuns(std::vector<Node>& nodes, const std::vector<Edge>& edges, KeyOf keyOf,
              Assign assign) {
    std::size_t cursor = 0;
    for (std::size_t position = 0; position < nodes.size(); ++position) {
        std::size_t count = 0;
        while (cursor + count < edges.size() &&
               keyOf(edges[cursor + count]) == static_cast<std::uint32_t>(position)) {
            ++count;
        }
        assign(nodes[position], static_cast<std::uint32_t>(cursor),
               static_cast<std::uint32_t>(count));
        cursor += count;
    }
}

} // namespace

Result<NavGraph> buildNavGraph(const Package& package, const upkg::Level& level,
                               const upkg::PackageResolver& resolver) {
    NavGraph graph;

    // Nodes in export-table order, which is what leaves `nodes` sorted by
    // `exportIndex` for `nodeOf`.
    std::map<std::string, bool> navigationClasses;
    std::vector<std::uint32_t> nodeOfExport(package.exports().size(), NOT_A_NODE);
    for (std::size_t index = 0; index < package.exports().size(); ++index) {
        const ExportEntry& actor = package.exports()[index];
        if (isClassExport(actor)) {
            continue; // a class is not an actor
        }
        UTA_TRY(const bool descends,
                descendsFromNavigationPoint(package, actor, resolver, navigationClasses));
        if (!descends) {
            continue;
        }
        UTA_TRY(const std::string_view className, package.objectName(actor.objectClass));
        nodeOfExport[index] = static_cast<std::uint32_t>(graph.nodes.size());
        NavNode node;
        node.exportIndex = static_cast<std::uint32_t>(index);
        node.className = std::string{className};
        graph.nodes.push_back(std::move(node));
    }

    // SS 4.4: the edges are the reach-spec array, not the `Paths` properties.
    // The array is the level's own edge list and visiting it once visits each
    // edge once; reading both and reconciling them would make this builder own
    // a disagreement that belongs to the content.
    std::vector<NavEdge> edges;
    edges.reserve(level.reachSpecs.size());
    for (const upkg::ReachSpec& spec : level.reachSpecs) {
        // Both endpoints are resolved even when the first fails, because
        // `discardedEndpoints` counts ENDPOINTS: a spec with two bad ones adds
        // two. INV-2.
        const std::optional<std::uint32_t> from = endpointNode(spec.start, nodeOfExport);
        const std::optional<std::uint32_t> to = endpointNode(spec.end, nodeOfExport);
        if (!from.has_value()) {
            ++graph.discardedEndpoints;
        }
        if (!to.has_value()) {
            ++graph.discardedEndpoints;
        }
        if (!from.has_value() || !to.has_value()) {
            continue;
        }
        NavEdge edge;
        edge.from = *from;
        edge.to = *to;
        edge.distance = spec.distance;
        edge.collisionRadius = spec.collisionRadius;
        edge.collisionHeight = spec.collisionHeight;
        edge.reachFlags = spec.reachFlags;
        edge.pruned = spec.pruned;
        edges.push_back(edge);
    }

    // Stable, so the file's own spec order survives inside each node's run.
    std::stable_sort(edges.begin(), edges.end(),
                     [](const NavEdge& left, const NavEdge& right) {
                         return left.from < right.from;
                     });
    graph.edges = std::move(edges);
    fillRuns(
        graph.nodes, graph.edges, [](const NavEdge& edge) { return edge.from; },
        [](NavNode& node, std::uint32_t first, std::uint32_t count) {
            node.firstEdge = first;
            node.edgeCount = count;
        });
    return graph;
}

Result<WiringGraph> buildWiringGraph(const Package& package) {
    WiringGraph graph;

    /// One `Event` an actor fires, before it is matched onto a tag.
    struct FiredEvent {
        std::uint32_t from = 0;
        std::string event;
    };
    std::vector<FiredEvent> fired;

    // One pass over the exports in table order, which is what leaves `nodes`
    // sorted by `exportIndex` for `nodeOf`.
    for (std::size_t index = 0; index < package.exports().size(); ++index) {
        const ExportEntry& actor = package.exports()[index];
        if (isClassExport(actor)) {
            continue;
        }
        UTA_TRY(const std::vector<upkg::Property> properties,
                upkg::readProperties(package, actor));

        // SS 4.3: an actor's OWN property list and nothing else. SS 2.1 tested
        // and killed the inherited-tag hypothesis, so no ancestry walk here and
        // no dependency on `effectiveDefaults`.
        std::string tag;
        bool carriesTag = false;
        std::vector<std::string> events;
        for (const upkg::Property& property : properties) {
            if (property.type != upkg::PropertyType::Name ||
                !std::holds_alternative<upkg::NameRef>(property.value)) {
                continue;
            }
            UTA_TRY(const std::string_view name, package.name(property.nameIndex));
            const bool isTag = name == TAG_PROPERTY;
            if (!isTag && name != EVENT_PROPERTY) {
                continue;
            }
            UTA_TRY(const std::string_view value,
                    package.name(std::get<upkg::NameRef>(property.value).index));
            if (isTag) {
                if (!carriesTag) {
                    tag = std::string{value};
                    carriesTag = true;
                }
            } else {
                events.emplace_back(value);
            }
        }
        if (!carriesTag && events.empty()) {
            continue;
        }

        const auto position = static_cast<std::uint32_t>(graph.nodes.size());
        WiringNode node;
        node.exportIndex = static_cast<std::uint32_t>(index);
        node.tag = std::move(tag);
        graph.nodes.push_back(std::move(node));
        for (std::string& event : events) {
            fired.push_back(FiredEvent{position, std::move(event)});
        }
    }

    // A multimap, because SS 2.1 measured that a tag is shared by several
    // actors far more often than not -- one switch opening a bank of movers. A
    // design resolving an event to a single target is wrong about most of the
    // library. INV-3.
    //
    // Keyed by view into `nodes`, which is complete and no longer growing. An
    // empty tag is SS 4.1's "the actor only fires" sentinel, so it indexes
    // nothing.
    std::multimap<std::string_view, std::uint32_t> byTag;
    for (std::size_t position = 0; position < graph.nodes.size(); ++position) {
        if (!graph.nodes[position].tag.empty()) {
            byTag.emplace(graph.nodes[position].tag, static_cast<std::uint32_t>(position));
        }
    }

    // SS 4.4: the match is EXACT on the name. Case-insensitive matching was
    // measured over the whole reference install and resolves the same events,
    // recovering none of the dangling ones.
    std::vector<WiringEdge> outgoing;
    for (const FiredEvent& event : fired) {
        const auto range = byTag.equal_range(event.event);
        if (range.first == range.second) {
            graph.dangling.push_back(DanglingEvent{event.from, event.event});
            continue;
        }
        for (auto target = range.first; target != range.second; ++target) {
            outgoing.push_back(WiringEdge{event.from, target->second, event.event});
        }
    }

    std::stable_sort(outgoing.begin(), outgoing.end(),
                     [](const WiringEdge& left, const WiringEdge& right) {
                         return left.from < right.from;
                     });
    graph.incoming = outgoing;
    std::stable_sort(graph.incoming.begin(), graph.incoming.end(),
                     [](const WiringEdge& left, const WiringEdge& right) {
                         return left.to < right.to;
                     });
    graph.edges = std::move(outgoing);

    fillRuns(
        graph.nodes, graph.edges, [](const WiringEdge& edge) { return edge.from; },
        [](WiringNode& node, std::uint32_t first, std::uint32_t count) {
            node.firstOutgoing = first;
            node.outgoingCount = count;
        });
    fillRuns(
        graph.nodes, graph.incoming, [](const WiringEdge& edge) { return edge.to; },
        [](WiringNode& node, std::uint32_t first, std::uint32_t count) {
            node.firstIncoming = first;
            node.incomingCount = count;
        });
    return graph;
}

} // namespace uta::unav
