// The start, the exits, the network's part, and the routes between --
// docs/specs/UTA-0121-bot-path-seeds.md SS 4.3, SS 4.6 and SS 4.7; INV-5 to
// INV-8 and INV-10.

#include "Seeds.h"

#include "Walkable.h"
#include "common/Json.h"
#include "ubake/Actors.h"
#include "ubake/Bake.h"
#include "ubake/Collision.h"
#include "ubake/Movers.h"
#include "unav/Build.h"
#include "upkg/Geometry.h"
#include "upkg/Level.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <numbers>
#include <optional>
#include <queue>
#include <sstream>

namespace uta::paths {
namespace {

using ubundle::ActorClass;
using ubundle::ActorPlacement;
using ubundle::PropertyRecord;
using ubundle::ValueKind;

/// SS 3 decision 8: a hop's longest, and how near another navigation point a
/// node may stand.
constexpr double LONGEST_HOP = 350;
constexpr double TOO_NEAR = 50;

constexpr std::array<double, 3> HEIGHTS = {-HALF_HEIGHT + STEP + 1, 0, HALF_HEIGHT - 1};

// ------------------------------------------------------------------ SS 4.6

template <class T>
Result<T> naming(Result<T> result, std::string_view mapName) {
    if (!result.has_value())
        return std::unexpected(result.error().withContext("reading " + std::string(mapName)));
    return result;
}

/// The class itself, or one of its ancestry: so a subclass the map declares is
/// found whatever package declares it (SS 4.6).
bool descendsFrom(const ActorClass& actorClass, std::string_view path) {
    return actorClass.path == path
           || std::find(actorClass.ancestry.begin(), actorClass.ancestry.end(), path)
                  != actorClass.ancestry.end();
}

/// The actor's own record, else its class default (UTA-0110 SS 4.6's order).
const PropertyRecord* resolved(std::string_view name, ValueKind kind, const ActorPlacement& actor,
                               const ActorClass& actorClass) {
    return ubake::detail::resolvedRecord(
        name, actor.properties, actorClass.defaults,
        [kind](const PropertyRecord& record) { return record.kind == kind; });
}

/// A Location neither the actor nor its class sets is Actor.uc's, the origin.
Vec3 locationOf(const ActorPlacement& actor, const ActorClass& actorClass) {
    const PropertyRecord* record = resolved("location", ValueKind::Vector, actor, actorClass);
    if (record == nullptr) return {};
    const auto& value = std::get<std::array<float, 3>>(record->value);
    return {value[0], value[1], value[2]};
}

/// A float neither the actor nor its class sets reads 0. On an install the
/// class's merged defaults always carry Actor.uc's collision size.
double floatOf(std::string_view name, const ActorPlacement& actor, const ActorClass& actorClass) {
    const PropertyRecord* record = resolved(name, ValueKind::Float, actor, actorClass);
    return record == nullptr ? 0 : std::get<float>(record->value);
}

const ActorPlacement* placementOf(const ubundle::Placements& placements, std::uint32_t exportIndex) {
    const auto found = std::lower_bound(
        placements.actors.begin(), placements.actors.end(), exportIndex,
        [](const ActorPlacement& actor, std::uint32_t index) { return actor.exportIndex < index; });
    return found != placements.actors.end() && found->exportIndex == exportIndex ? &*found : nullptr;
}

/// UTA-0119 SS 4.5: a pivot-space point placed at location + postScale (Y P
/// R q), with the exact sine and cosine of 2 pi angle / 65536.
Vec3 placed(const ubundle::MoverShape& shape, const std::array<float, 3>& q) {
    const auto radians = [](std::int32_t units) { return 2 * std::numbers::pi * units / 65536.0; };
    const double pitch = radians(shape.rotation[0]);
    const double yaw = radians(shape.rotation[1]);
    const double roll = radians(shape.rotation[2]);
    Vec3 v{q[0], std::cos(roll) * q[1] + std::sin(roll) * q[2],
           -std::sin(roll) * q[1] + std::cos(roll) * q[2]};
    v = {std::cos(pitch) * v.x - std::sin(pitch) * v.z, v.y,
         std::sin(pitch) * v.x + std::cos(pitch) * v.z};
    v = {std::cos(yaw) * v.x - std::sin(yaw) * v.y, std::sin(yaw) * v.x + std::cos(yaw) * v.y, v.z};
    return {shape.location[0] + shape.postScale[0] * v.x, shape.location[1] + shape.postScale[1] * v.y,
            shape.location[2] + shape.postScale[2] * v.z};
}

// ------------------------------------------------------------------ SS 4.7

/// Whether the segment from `a` to `b` meets `box`, by slabs.
bool meets(const Vec3& a, const Vec3& b, const Box& box) {
    const std::array<double, 3> start{a.x, a.y, a.z};
    const std::array<double, 3> step{b.x - a.x, b.y - a.y, b.z - a.z};
    const std::array<double, 3> low{box.min.x, box.min.y, box.min.z};
    const std::array<double, 3> high{box.max.x, box.max.y, box.max.z};
    double enter = 0;
    double leave = 1;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (step[axis] == 0) {
            if (start[axis] < low[axis] || start[axis] > high[axis]) return false;
            continue;
        }
        double t0 = (low[axis] - start[axis]) / step[axis];
        double t1 = (high[axis] - start[axis]) / step[axis];
        if (t0 > t1) std::swap(t0, t1);
        enter = std::max(enter, t0);
        leave = std::min(leave, t1);
        if (enter > leave) return false;
    }
    return true;
}

/// `box` grown by the body: R across, H up and down.
Box grown(const Box& box) {
    const Vec3 body{RADIUS, RADIUS, HALF_HEIGHT};
    return {box.min - body, box.max + body};
}

/// What a hop is checked against.
struct Hops {
    const Scene& scene;
    const WalkGraph& graph;
    const std::vector<bool>& moverSpot;

    /// A spot that is not a mover spot within a column's spacing of `p`
    /// horizontally and within a step of its height.
    bool floorUnder(const Vec3& p) const {
        const auto column = static_cast<std::int32_t>(std::lround((p.x - graph.origin.x) / COLUMN));
        const auto row = static_cast<std::int32_t>(std::lround((p.y - graph.origin.y) / COLUMN));
        for (std::int32_t c = column - 1; c <= column + 1; ++c)
            for (std::int32_t r = row - 1; r <= row + 1; ++r) {
                const WalkGraph::Range cell = graph.cell(c, r);
                for (std::uint32_t s = cell.begin; s < cell.end; ++s) {
                    const Vec3& centre = graph.spots[s].centre;
                    if (!moverSpot[s] && horizontal(centre, p) <= COLUMN
                        && std::abs(centre.z - p.z) <= STEP)
                        return true;
                }
            }
        return false;
    }

    /// SS 4.7's allowed hop.
    bool allowed(const Vec3& from, const Vec3& to) const {
        const Vec3 delta = to - from;
        const double span = length(delta);
        if (span > LONGEST_HOP) return false;
        for (const double height : HEIGHTS) {
            const Vec3 lift{0, 0, height};
            if (trace(scene.tree, from + lift, to + lift).fraction < 1) return false;
        }
        for (const Box& mover : scene.movers)
            if (meets(from, to, grown(mover))) return false;
        for (double along = 0; along < span; along += COLUMN)
            if (!floorUnder(from + delta * (along / span))) return false;
        return floorUnder(to);
    }
};

/// The shortest path, by distance, from any of `sources` to a spot `goal`
/// marks, over spots `removed` does not mark; empty when there is none.
std::vector<std::uint32_t> shortestPath(const WalkGraph& graph, const std::vector<std::uint32_t>& sources,
                                        const std::vector<bool>& goal, const std::vector<bool>& removed) {
    constexpr double NONE = std::numeric_limits<double>::infinity();
    std::vector<double> distance(graph.spots.size(), NONE);
    std::vector<std::uint32_t> previous(graph.spots.size(), std::numeric_limits<std::uint32_t>::max());
    using Entry = std::pair<double, std::uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
    for (const std::uint32_t source : sources) {
        if (removed[source] || distance[source] == 0) continue;
        distance[source] = 0;
        queue.emplace(0, source);
    }
    while (!queue.empty()) {
        const auto [reached, at] = queue.top();
        queue.pop();
        if (reached > distance[at]) continue;
        if (goal[at]) {
            std::vector<std::uint32_t> path;
            for (std::uint32_t step = at; step != std::numeric_limits<std::uint32_t>::max();
                 step = previous[step])
                path.push_back(step);
            std::reverse(path.begin(), path.end());
            return path;
        }
        for (const std::uint32_t next : graph.joins[at]) {
            if (removed[next]) continue;
            const double through = reached + length(graph.spots[next].centre - graph.spots[at].centre);
            if (through < distance[next]) {
                distance[next] = through;
                previous[next] = at;
                queue.emplace(through, next);
            }
        }
    }
    return {};
}

/// SS 4.7's nodes along a found path, appended to `nodes`.
void chain(const Hops& hops, const std::vector<std::uint32_t>& path, std::vector<Vec3>& nodes) {
    const auto spot = [&](std::size_t k) { return hops.graph.spots[path[k]].centre; };
    // The navigation point or node already proposed nearest `p`, within 50.
    const auto nearPoint = [&](const Vec3& p) -> std::optional<Vec3> {
        std::optional<Vec3> nearest;
        double best = TOO_NEAR;
        const std::array<const std::vector<Vec3>*, 2> lists = {&hops.scene.network, &nodes};
        for (const std::vector<Vec3>* points : lists)
            for (const Vec3& point : *points)
                if (const double distance = length(point - p); distance <= best) {
                    best = distance;
                    nearest = point;
                }
        return nearest;
    };

    Vec3 last = spot(0);
    for (std::size_t at = 0; at + 1 < path.size();) {
        // The furthest spot along the path with an allowed hop from the
        // chain's last point; where none has one, the next, so the chain moves.
        std::size_t next = at + 1;
        for (std::size_t k = path.size() - 1; k > at; --k)
            if (hops.allowed(last, spot(k))) {
                next = k;
                break;
            }
        Vec3 taken = spot(next);
        bool proposed = true;
        if (const std::optional<Vec3> point = nearPoint(taken)) {
            if (hops.allowed(last, *point)) {
                taken = *point;
                proposed = false;
            } else {
                // The furthest spot with an allowed hop and no such point
                // within 50; failing that, the spot itself.
                for (std::size_t k = path.size() - 1; k > at; --k)
                    if (hops.allowed(last, spot(k)) && !nearPoint(spot(k))) {
                        next = k;
                        taken = spot(k);
                        break;
                    }
            }
        }
        if (proposed) nodes.push_back(taken);
        last = taken;
        at = next;
    }
}

/// Every node the network reaches from `from`, over its edges forward or, with
/// `backward`, against them.
std::vector<bool> reach(const Scene& scene, std::size_t from, bool backward) {
    std::vector<bool> reached(scene.network.size(), false);
    std::vector<std::size_t> pending{from};
    reached[from] = true;
    while (!pending.empty()) {
        const std::size_t at = pending.back();
        pending.pop_back();
        for (const auto& [tail, head] : scene.edges) {
            const std::size_t there = backward ? tail : head;
            if ((backward ? head : tail) != at || there >= reached.size() || reached[there]) continue;
            reached[there] = true;
            pending.push_back(there);
        }
    }
    return reached;
}

std::optional<std::size_t> nearestNode(const Scene& scene, const Vec3& to) {
    std::optional<std::size_t> nearest;
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < scene.network.size(); ++i)
        if (const double distance = length(scene.network[i] - to); distance < best) {
            best = distance;
            nearest = i;
        }
    return nearest;
}

// ------------------------------------------------------------------ SS 4.3

/// A float's shortest round-trip decimal (SS 4.3's numbers).
std::string number(double value) {
    std::array<char, 32> text{};
    const auto written = std::to_chars(text.data(), text.data() + text.size(), static_cast<float>(value));
    return std::string(text.data(), written.ptr);
}

std::string_view routeName(Route route) {
    switch (route) {
    case Route::Found: return "found";
    case Route::Mover: return "mover";
    case Route::None: return "none";
    }
    return "none"; // unreachable: every enumerator is named above
}

void writePoint(std::ostream& out, const Vec3& p) {
    out << "{\"x\": " << number(p.x) << ", \"y\": " << number(p.y) << ", \"z\": " << number(p.z);
}

} // namespace

Result<Scene> sceneOf(const upkg::Package& map, std::string_view mapName,
                      const upkg::PackageResolver& resolver) {
    // The level and its tree, the network and the actors, as the bake reads them.
    UTA_TRY(const upkg::ExportEntry* const levelExport, ubake::detail::findLevel(map, mapName));
    UTA_TRY(const upkg::Level level, naming(upkg::readLevel(map, *levelExport), mapName));
    UTA_TRY(const upkg::ExportEntry* const modelExport, ubake::detail::findModel(map, level, mapName));
    UTA_TRY(const upkg::Model model, naming(upkg::readModel(map, *modelExport), mapName));
    Scene scene;
    UTA_TRY(scene.tree, naming(ubake::buildCollision(model), mapName));
    UTA_TRY(const unav::NavGraph nav, naming(unav::buildNavGraph(map, level, resolver), mapName));
    UTA_TRY(const ubake::Actors actors, naming(ubake::buildActors(map, mapName, level, resolver), mapName));
    const ubundle::Placements& placements = actors.placements;

    // The start and the exits, in the level's actor order.
    bool started = false;
    for (const upkg::ObjectReference slot : level.actors) {
        const ActorPlacement* actor = placementOf(placements, slot.index());
        if (actor == nullptr) continue;
        const ActorClass& actorClass = placements.classes[actor->classIndex];
        if (!started && descendsFrom(actorClass, "engine.playerstart")) {
            scene.start = locationOf(*actor, actorClass);
            started = true;
        }
        if (descendsFrom(actorClass, "monsterhunt.monsterend"))
            scene.exits.push_back(Cylinder{locationOf(*actor, actorClass),
                                           floatOf("collisionradius", *actor, actorClass),
                                           floatOf("collisionheight", *actor, actorClass)});
    }
    if (!started) return fail(ErrorCode::MalformedData, "reading " + std::string(mapName) + ": the map has no PlayerStart");
    if (scene.exits.empty())
        return fail(ErrorCode::MalformedData, "reading " + std::string(mapName) + ": the map has no MonsterEnd");

    // The network, each node at its actor's Location. A navigation point that
    // is not one of the level's actors is not in its world, and is dropped
    // with its edges.
    std::vector<std::size_t> position(nav.nodes.size(), nav.nodes.size());
    for (std::size_t i = 0; i < nav.nodes.size(); ++i) {
        const ActorPlacement* actor = placementOf(placements, nav.nodes[i].exportIndex);
        if (actor == nullptr) continue;
        position[i] = scene.network.size();
        scene.network.push_back(locationOf(*actor, placements.classes[actor->classIndex]));
    }
    for (const unav::NavEdge& edge : nav.edges)
        if (position[edge.from] < nav.nodes.size() && position[edge.to] < nav.nodes.size())
            scene.edges.emplace_back(position[edge.from], position[edge.to]);

    // Each mover's box: its tree placed by its MOVR shape (UTA-0111 SS 4.5).
    // The shape's materials are not wanted, so none is looked up.
    const ubake::MaterialLookup noMaterial = [](upkg::ObjectReference, bool) -> const ubake::SurfaceMaterial* {
        return nullptr;
    };
    UTA_TRY(const std::vector<ubake::MoverSite> movers, naming(ubake::findMovers(map, placements), mapName));
    for (const ubake::MoverSite& mover : movers) {
        auto moverModel = upkg::readModel(map, *mover.model);
        if (!moverModel.has_value())
            return std::unexpected(moverModel.error()
                                       .withContext("reading mover " + placements.actors[mover.placement].path)
                                       .withContext("reading " + std::string(mapName)));
        UTA_TRY(const ubundle::MoverShape shape,
                naming(ubake::buildMover(mover, *moverModel, placements, noMaterial), mapName));
        UTA_TRY(const ubundle::MoverCollision tree,
                naming(ubake::buildMoverCollision(mover, *moverModel, placements), mapName));
        if (tree.tree.points.empty()) continue;
        Box box{placed(shape, tree.tree.points[0]), placed(shape, tree.tree.points[0])};
        for (const auto& point : tree.tree.points) {
            const Vec3 p = placed(shape, point);
            box.min = {std::min(box.min.x, p.x), std::min(box.min.y, p.y), std::min(box.min.z, p.z)};
            box.max = {std::max(box.max.x, p.x), std::max(box.max.y, p.y), std::max(box.max.z, p.z)};
        }
        scene.movers.push_back(box);
    }
    return scene;
}

Proposal propose(const Scene& scene, bool partitioned) {
    const WalkGraph graph = walkGraph(scene.tree);
    Proposal proposal;

    // A spot whose body box overlaps a mover's box is a mover spot.
    std::vector<bool> moverSpot(graph.spots.size(), false);
    const Vec3 body{RADIUS, RADIUS, HALF_HEIGHT};
    for (std::size_t s = 0; s < graph.spots.size(); ++s) {
        const Vec3 low = graph.spots[s].centre - body;
        const Vec3 high = graph.spots[s].centre + body;
        for (const Box& mover : scene.movers)
            if (low.x <= mover.max.x && high.x >= mover.min.x && low.y <= mover.max.y
                && high.y >= mover.min.y && low.z <= mover.max.z && high.z >= mover.min.z)
                moverSpot[s] = true;
    }
    const std::vector<bool> keepAll(graph.spots.size(), false);

    // The start part, what the network reaches from the node nearest the
    // start, and the spots its nodes and the start are placed at.
    std::vector<std::optional<std::uint32_t>> placedNode;
    for (const Vec3& node : scene.network) placedNode.push_back(place(graph, node));
    const std::optional<std::size_t> startNode = nearestNode(scene, scene.start);
    const std::vector<bool> startPart =
        startNode ? reach(scene, *startNode, false) : std::vector<bool>(scene.network.size(), false);
    std::vector<std::uint32_t> sources;
    for (std::size_t i = 0; i < scene.network.size(); ++i)
        if (startPart[i] && placedNode[i]) sources.push_back(*placedNode[i]);
    if (const auto startSpot = place(graph, scene.start)) sources.push_back(*startSpot);

    const Hops hops{scene, graph, moverSpot};
    for (const Cylinder& exit : scene.exits) {
        std::vector<bool> goal(graph.spots.size(), false);
        for (std::size_t s = 0; s < graph.spots.size(); ++s) {
            const Vec3& centre = graph.spots[s].centre;
            goal[s] = horizontal(centre, exit.centre) <= exit.radius + RADIUS
                      && std::abs(centre.z - exit.centre.z) <= exit.height + HALF_HEIGHT;
        }
        // On a PARTITIONED map, also the spot of any node outside the start
        // part from which the network reaches the node nearest the exit.
        if (partitioned)
            if (const std::optional<std::size_t> exitNode = nearestNode(scene, exit.centre)) {
                const std::vector<bool> reaches = reach(scene, *exitNode, true);
                for (std::size_t i = 0; i < scene.network.size(); ++i)
                    if (reaches[i] && !startPart[i] && placedNode[i]) goal[*placedNode[i]] = true;
            }

        if (const auto path = shortestPath(graph, sources, goal, moverSpot); !path.empty()) {
            proposal.routes.push_back(Route::Found);
            chain(hops, path, proposal.nodes);
        } else if (!shortestPath(graph, sources, goal, keepAll).empty()) {
            proposal.routes.push_back(Route::Mover);
        } else {
            proposal.routes.push_back(Route::None);
        }
    }
    return proposal;
}

std::string toJson(std::string_view map, std::string_view md5, std::string_view group,
                   const Scene& scene, const Proposal& proposal) {
    const auto any = [&](Route route) {
        return std::find(proposal.routes.begin(), proposal.routes.end(), route) != proposal.routes.end();
    };
    std::ostringstream out;
    out << "{\n  \"schema\": 1,\n  \"map\": ";
    tools::writeJsonString(out, map);
    out << ",\n  \"md5\": ";
    tools::writeJsonString(out, md5);
    out << ",\n  \"group\": ";
    tools::writeJsonString(out, group);
    out << ",\n  \"moverOnly\": " << (!any(Route::Found) && any(Route::Mover) ? "true" : "false");

    out << ",\n  \"exits\": [";
    for (std::size_t i = 0; i < scene.exits.size(); ++i) {
        out << (i == 0 ? "\n    " : ",\n    ");
        writePoint(out, scene.exits[i].centre);
        out << ", \"route\": \""
            << routeName(i < proposal.routes.size() ? proposal.routes[i] : Route::None) << "\"}";
    }
    out << (scene.exits.empty() ? "]" : "\n  ]");

    out << ",\n  \"nodes\": [";
    for (std::size_t i = 0; i < proposal.nodes.size(); ++i) {
        out << (i == 0 ? "\n    " : ",\n    ");
        writePoint(out, proposal.nodes[i]);
        out << '}';
    }
    out << (proposal.nodes.empty() ? "]" : "\n  ]") << "\n}\n";
    return out.str();
}

} // namespace uta::paths
