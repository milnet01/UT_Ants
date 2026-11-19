// ut-export's camera path -- UTA-0476. Path.h says what binds it.

#include "Path.h"

#include "JsonNumbers.h"
#include "common/Json.h"
#include "ubake/Actors.h"
#include "ubake/Install.h"
#include "unav/Reach.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

namespace uta::tools::exporter {

namespace {

/// Half a UT player's collision cylinder as a box: radius 17, half-height 39
/// (unav::PawnMoves' defaults), in UT units.
constexpr double PLAYER_RADIUS = 17, PLAYER_HALF_HEIGHT = 39;
/// How far behind its walker the camera trails, in seconds of the walker's
/// own route, and the least and most it may stand from it, in UT units.
constexpr double TRAIL_SECONDS = 0.75, NEAREST = 100, FURTHEST = 600, RAISE = 48;
/// EXRAY-0001 SS 4.6's camera: UT99's default horizontal field, 90 degrees.
constexpr double XFOV = std::numbers::pi / 2, ZNEAR = 0.05;

struct Stop {
    Vec3 at;          ///< UT units
    double time = 0;  ///< seconds from the walker's start
};

double horizontalLength(const Vec3& v) { return std::sqrt(v[0] * v[0] + v[1] * v[1]); }

/// The actor PLAC holds for an export, or null.
const ubundle::ActorPlacement* placementOf(const ubundle::Bundle& bundle, std::uint32_t exportIndex) {
    const auto& actors = bundle.placements->actors;
    const auto at = std::lower_bound(actors.begin(), actors.end(), exportIndex,
                                     [](const ubundle::ActorPlacement& a, std::uint32_t e) { return a.exportIndex < e; });
    return at != actors.end() && at->exportIndex == exportIndex ? &*at : nullptr;
}

/// Each navigation point's stored Location, by node position; none where the
/// actor has no placement or the value is not finite.
std::vector<std::optional<Vec3>> nodeLocations(const ubundle::Bundle& bundle) {
    std::vector<std::optional<Vec3>> out;
    for (const unav::NavNode& node : bundle.nav->nodes) {
        std::optional<Vec3> location;
        if (const ubundle::ActorPlacement* at = placementOf(bundle, node.exportIndex)) {
            const auto& defaults = bundle.placements->classes[at->classIndex].defaults;
            if (const auto* record =
                    ubake::detail::resolvedRecord("location", ubundle::ValueKind::Vector, at->properties, defaults)) {
                const auto& v = std::get<std::array<float, 3>>(record->value);
                if (std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2])) location = Vec3{v[0], v[1], v[2]};
            }
        }
        out.push_back(location);
    }
    return out;
}

/// A depth-first tour from `start` over the edges a UT player may take,
/// walking back along each edge when a branch is done. A teleporter or lift
/// (R_SPECIAL) takes no time either way.
std::vector<Stop> tourFrom(const ubundle::Bundle& bundle, const std::vector<std::optional<Vec3>>& at,
                           std::uint32_t start, double speed) {
    const unav::PawnMoves player;
    std::vector<bool> seen(at.size(), false);
    struct Frame {
        std::uint32_t node;
        std::size_t next = 0;  ///< the next edge to try
        bool special = false;  ///< how it was reached
    };
    std::vector<Frame> stack{{start}};
    seen[start] = true;
    std::vector<Stop> stops{{*at[start], 0}};
    const auto arrive = [&](std::uint32_t node, bool special) {
        const Vec3& from = stops.back().at;
        const Vec3& to = *at[node];
        const Vec3 d{to[0] - from[0], to[1] - from[1], to[2] - from[2]};
        const double length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        stops.push_back({to, stops.back().time + (special ? 0 : length / speed)});
    };
    while (!stack.empty()) {
        Frame& top = stack.back();
        const auto edges = unav::edgesFrom(*bundle.nav, top.node);
        bool advanced = false;
        while (top.next < edges.size()) {
            const unav::NavEdge& edge = edges[top.next++];
            if (seen[edge.to] || !at[edge.to] || !unav::mayTraverse(edge, player)) continue;
            const bool special = (edge.reachFlags & unav::R_SPECIAL) != 0;
            seen[edge.to] = true;
            arrive(edge.to, special);
            stack.push_back({edge.to, 0, special});
            advanced = true;
            break;
        }
        if (advanced) continue;
        const bool special = top.special;
        stack.pop_back();
        if (!stack.empty()) arrive(stack.back().node, special);
    }
    return stops;
}

struct Pose {
    Vec3 at;
    Vec3 heading; ///< unit, horizontal
};

/// Where a walker is at `time`, holding its last stop once the tour ends.
Pose poseAt(const std::vector<Stop>& stops, double time) {
    const auto later = std::upper_bound(stops.begin(), stops.end(), time,
                                        [](double t, const Stop& s) { return t < s.time; });
    Pose pose{stops.back().at, {1, 0, 0}};
    std::size_t leg = static_cast<std::size_t>(later - stops.begin());
    if (later != stops.end()) {
        const Stop& a = *(later - 1);
        const Stop& b = *later;
        const double share = (time - a.time) / (b.time - a.time);
        for (int k = 0; k < 3; ++k) pose.at[k] = a.at[k] + share * (b.at[k] - a.at[k]);
    } else {
        leg = stops.size() - 1;
    }
    // Face along the latest leg that goes somewhere across the floor.
    for (std::size_t i = std::min(leg, stops.size() - 1); i > 0; --i) {
        const Vec3 d{stops[i].at[0] - stops[i - 1].at[0], stops[i].at[1] - stops[i - 1].at[1], 0};
        const double length = horizontalLength(d);
        if (length > 1e-6 && stops[i].time > stops[i - 1].time) {
            pose.heading = {d[0] / length, d[1] / length, 0};
            break;
        }
    }
    return pose;
}

/// A glTF rotation turning a walker to face `heading` (UT axes).
Quat facing(const Vec3& heading) {
    const double c = heading[0], s = heading[1];
    return quaternionOf(toGltf(Mat3{{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}}));
}

/// A glTF camera at `eye` looking at `target`, both glTF axes: down its own
/// -Z with +Y up (glTF 2.0 SS 3.10).
Quat lookAt(const Vec3& eye, const Vec3& target) {
    Vec3 f{target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    const double fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (!(fl > 1e-9)) return {0, 0, 0, 1};
    for (double& part : f) part /= fl;
    Vec3 right{-f[2], 0, f[0]}; // f x +Y
    double rl = std::sqrt(right[0] * right[0] + right[2] * right[2]);
    if (!(rl > 1e-9)) right = {1, 0, 0}, rl = 1;
    for (double& part : right) part /= rl;
    const Vec3 up{right[1] * f[2] - right[2] * f[1], right[2] * f[0] - right[0] * f[2],
                  right[0] * f[1] - right[1] * f[0]};
    Mat3 m{};
    for (int r = 0; r < 3; ++r) m[r] = {right[r], up[r], -f[r]};
    return quaternionOf(m);
}

/// The chase camera's eye, UT axes: where the walker was TRAIL_SECONDS ago,
/// kept between NEAREST and FURTHEST from it, raised by RAISE. A trail point is
/// somewhere the walker stood, so it is rarely inside a wall.
Vec3 chaseEye(const std::vector<Stop>& stops, double time) {
    const Pose now = poseAt(stops, time);
    Vec3 eye = poseAt(stops, std::max(0.0, time - TRAIL_SECONDS)).at;
    const double apart = horizontalLength({eye[0] - now.at[0], eye[1] - now.at[1], 0});
    if (apart < NEAREST || apart > FURTHEST)
        eye = {now.at[0] - NEAREST * now.heading[0], now.at[1] - NEAREST * now.heading[1], now.at[2]};
    eye[2] += RAISE;
    return eye;
}

// ---------------------------------------------------------------- movers

/// UT99's Mover defaults, for a placement and class that say nothing, and the
/// length of its KeyPos and KeyRot arrays.
constexpr double MOVE_TIME = 1, STAY_OPEN_TIME = 4;
constexpr std::uint8_t NUM_KEYS = 2, MAX_KEYS = 8;

/// A mover's pose as a difference from where the scene file has it.
struct Shift {
    Vec3 move{}; ///< UT units
    Vec3 turn{}; ///< pitch, yaw, roll; 65536 to a turn

    [[nodiscard]] bool none() const { return move == Vec3{} && turn == Vec3{}; }
};

/// One of the scene's moving nodes and the keys it moves between.
struct MoverTrack {
    const ubundle::MoverShape* shape = nullptr;
    bool inWorldSpace = false;  ///< buildScene's node is at the origin: it moves, and cannot turn
    std::vector<Shift> keys;    ///< each less the key the map placed it at
    double moveTime = 0;        ///< seconds between two keys
    double stayTime = 0;        ///< seconds at each end
    bool glides = true;         ///< MV_GlideByTime: eased at both ends of a move
};

/// The movers `scene` holds as nodes that have a second key to go to.
std::vector<MoverTrack> moverTracks(const ubundle::Bundle& bundle, const Scene& scene) {
    std::vector<MoverTrack> out;
    if (!bundle.movers) return out;
    for (const ubundle::MoverShape& mover : *bundle.movers) {
        const std::string id = "mover:" + std::to_string(mover.exportIndex);
        if (std::ranges::none_of(scene.nodes, [&](const Node& n) { return n.id == id && n.motion == "moving"; }))
            continue;
        const ubundle::ActorPlacement* actor = placementOf(bundle, mover.exportIndex);
        if (actor == nullptr) continue;
        const auto& defaults = bundle.placements->classes[actor->classIndex].defaults;
        const auto seconds = [&](std::string_view name, double fallback) {
            const auto* record =
                ubake::detail::resolvedRecord(name, ubundle::ValueKind::Float, actor->properties, defaults);
            const double value = record != nullptr ? std::get<float>(record->value) : fallback;
            return std::isfinite(value) && value > 0 ? value : 0.0;
        };
        const auto byte = [&](std::string_view name, std::uint8_t fallback) {
            const auto* record =
                ubake::detail::resolvedRecord(name, ubundle::ValueKind::Byte, actor->properties, defaults);
            return record != nullptr ? std::get<std::uint8_t>(record->value) : fallback;
        };
        MoverTrack track{&mover, !trsCarries(mover), {}, seconds("movetime", MOVE_TIME),
                         seconds("stayopentime", STAY_OPEN_TIME), byte("moverglidetype", 1) != 0};
        track.keys.resize(std::clamp<std::uint8_t>(byte("numkeys", NUM_KEYS), 1, MAX_KEYS));
        for (const ubundle::PropertyRecord& record : actor->properties) {
            if (record.arrayIndex >= track.keys.size()) continue;
            const std::string name = ubake::detail::fold(record.name);
            if (name == "keypos" && record.kind == ubundle::ValueKind::Vector) {
                const auto& v = std::get<std::array<float, 3>>(record.value);
                if (std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]))
                    track.keys[record.arrayIndex].move = {v[0], v[1], v[2]};
            } else if (name == "keyrot" && record.kind == ubundle::ValueKind::Rotator) {
                const auto& r = std::get<std::array<std::int32_t, 3>>(record.value);
                track.keys[record.arrayIndex].turn = {double(r[0]), double(r[1]), double(r[2])};
            }
        }
        // UT99's Mover.PostBeginPlay: the map placed it at key KeyNum.
        const std::uint8_t placedAt = byte("keynum", 0);
        const Shift placed = placedAt < track.keys.size() ? track.keys[placedAt] : Shift{};
        for (Shift& key : track.keys)
            for (int k = 0; k < 3; ++k) key.move[k] -= placed.move[k], key.turn[k] -= placed.turn[k];
        if (std::ranges::all_of(track.keys, &Shift::none)) continue;
        out.push_back(std::move(track));
    }
    return out;
}

/// Where `track` is at `time` on its loop: closed for stayTime, out through
/// its keys at moveTime each, open for stayTime, and back the same way.
Shift shiftAt(const MoverTrack& track, double time) {
    const double steps = static_cast<double>(track.keys.size() - 1);
    const double travel = steps * track.moveTime, loop = 2 * (travel + track.stayTime);
    if (!(loop > 0)) return {};
    const double phase = std::fmod(time, loop);
    double along = 0; // in keys: 0 closed, `steps` open
    if (phase < track.stayTime) along = 0;
    else if (phase < track.stayTime + travel) along = (phase - track.stayTime) / track.moveTime;
    else if (phase < 2 * track.stayTime + travel) along = steps;
    else along = steps - (phase - 2 * track.stayTime - travel) / track.moveTime;
    along = std::clamp(along, 0.0, steps);
    const std::size_t from = std::min(static_cast<std::size_t>(along), track.keys.size() - 2);
    double share = along - static_cast<double>(from);
    if (track.glides) share = share * share * (3 - 2 * share);
    Shift out;
    for (int k = 0; k < 3; ++k) {
        out.move[k] = track.keys[from].move[k] + share * (track.keys[from + 1].move[k] - track.keys[from].move[k]);
        out.turn[k] = track.keys[from].turn[k] + share * (track.keys[from + 1].turn[k] - track.keys[from].turn[k]);
    }
    return out;
}

/// SS 4.6's "nodes" member for one frame, with its comma, or nothing where
/// every mover is at rest. An entry replaces the node's world value, so a
/// mover in world space takes its move alone.
std::string moverEntries(const std::vector<MoverTrack>& tracks, double time, PathReport& report) {
    std::string entries;
    for (const MoverTrack& track : tracks) {
        const Shift shift = shiftAt(track, time);
        if (shift.none()) continue;
        const ubundle::MoverShape& mover = *track.shape;
        std::string entry;
        if (track.inWorldSpace) {
            if (shift.turn != Vec3{}) ++report.unturnedEntries;
            if (shift.move == Vec3{}) continue;
            entry = "\"translation\":" + list(toGltf(shift.move));
        } else {
            std::array<std::int32_t, 3> rotation{};
            Vec3 location{};
            for (std::size_t k = 0; k < 3; ++k) {
                location[k] = mover.location[k] + shift.move[k];
                rotation[k] = mover.rotation[k] + static_cast<std::int32_t>(std::llround(shift.turn[k]));
            }
            entry = "\"translation\":" + list(toGltf(location)) +
                    ",\"rotation\":" + list(quaternionOf(toGltf(rotationOf(rotation))));
        }
        entries += (entries.empty() ? "" : ",") + ("\"mover:" + std::to_string(mover.exportIndex) + "\":{" + entry + "}");
        ++report.moverEntries;
    }
    return entries.empty() ? entries : "\"nodes\":{" + entries + "},";
}

} // namespace

Result<std::string> writeCameraPath(const ubundle::Bundle& bundle, const Scene& scene,
                                    const PathOptions& options, PathReport& report) {
    if (!bundle.nav || !bundle.placements)
        return fail(ErrorCode::InvalidArgument, "the bundle has no NAVG or no PLAC section");
    const std::vector<std::optional<Vec3>> at = nodeLocations(bundle);
    std::vector<std::uint32_t> starts, placed;
    for (std::uint32_t i = 0; i < at.size(); ++i) {
        if (!at[i]) continue;
        placed.push_back(i);
        if (bundle.nav->nodes[i].className == "PlayerStart") starts.push_back(i);
    }
    report.locatedNodes = placed.size();
    if (placed.empty()) return fail(ErrorCode::InvalidArgument, "no navigation point has a Location");
    const std::vector<std::uint32_t>& pool = starts.empty() ? placed : starts;
    const std::size_t walkers = std::min<std::size_t>(std::max(1u, options.walkers), pool.size());

    std::vector<std::vector<Stop>> tours;
    for (std::size_t k = 0; k < walkers; ++k) {
        tours.push_back(tourFrom(bundle, at, pool[k * pool.size() / walkers], options.speed));
        report.tourStops += tours.back().size();
    }
    report.walkers = walkers;
    report.frames = static_cast<std::size_t>(std::floor(options.seconds * options.framesPerSecond)) + 1;

    const std::vector<MoverTrack> movers = moverTracks(bundle, scene);
    report.movingMovers = movers.size();

    const auto id = [](std::size_t k) { return "walker:" + std::to_string(k); };
    std::ostringstream o;
    o << "{\"xr_camera_path\":1,\"scene_keys\":[";
    writeJsonString(o, scene.sceneKey);
    o << "],\"camera\":{\"xfov\":" << number(XFOV) << ",\"znear\":" << number(ZNEAR) << "},\"actors\":[";
    const Vec3 half = toGltf(Vec3{PLAYER_RADIUS, PLAYER_RADIUS, PLAYER_HALF_HEIGHT});
    const Vec3 low{-half[0], -half[1], -half[2]};
    for (std::size_t k = 0; k < walkers; ++k)
        o << (k ? "," : "") << "{\"id\":\"" << id(k) << "\",\"from\":0,\"rest\":{\"translation\":"
          << list(toGltf(tours[k].front().at)) << "},\"shape\":{\"bounds\":{\"min\":" << list(low)
          << ",\"max\":" << list(half) << "}}}";
    o << "],\"frames\":[";
    for (std::size_t f = 0; f < report.frames; ++f) {
        const double time = static_cast<double>(f) / options.framesPerSecond;
        const Vec3 target = toGltf(poseAt(tours[0], time).at);
        const Vec3 eye = toGltf(chaseEye(tours[0], time));
        o << (f ? "," : "") << "\n{\"time\":" << number(time) << ",\"camera\":{\"translation\":" << list(eye)
          << ",\"rotation\":" << list(lookAt(eye, target)) << "}," << moverEntries(movers, time, report)
          << "\"actors\":{";
        for (std::size_t k = 0; k < walkers; ++k) {
            const Pose pose = poseAt(tours[k], time);
            o << (k ? "," : "") << '"' << id(k) << "\":{\"translation\":" << list(toGltf(pose.at))
              << ",\"rotation\":" << list(facing(pose.heading)) << '}';
        }
        o << "}}";
    }
    o << "]}\n";
    return o.str();
}

} // namespace uta::tools::exporter
