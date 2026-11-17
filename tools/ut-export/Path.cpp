// ut-export's camera path -- UTA-0476. Path.h says what binds it.

#include "Path.h"

#include "JsonNumbers.h"
#include "Scene.h"
#include "common/Json.h"
#include "ubake/Actors.h"
#include "unav/Reach.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <sstream>
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

/// Each navigation point's stored Location, by node position; none where the
/// actor has no placement or the value is not finite.
std::vector<std::optional<Vec3>> nodeLocations(const ubundle::Bundle& bundle) {
    const auto& actors = bundle.placements->actors;
    std::vector<std::optional<Vec3>> out;
    for (const unav::NavNode& node : bundle.nav->nodes) {
        const auto at = std::lower_bound(actors.begin(), actors.end(), node.exportIndex,
                                         [](const ubundle::ActorPlacement& a, std::uint32_t e) { return a.exportIndex < e; });
        std::optional<Vec3> location;
        if (at != actors.end() && at->exportIndex == node.exportIndex) {
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

} // namespace

Result<std::string> writeCameraPath(const ubundle::Bundle& bundle, std::string_view sceneKey,
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

    const auto id = [](std::size_t k) { return "walker:" + std::to_string(k); };
    std::ostringstream o;
    o << "{\"xr_camera_path\":1,\"scene_keys\":[";
    writeJsonString(o, sceneKey);
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
          << ",\"rotation\":" << list(lookAt(eye, target)) << "},\"actors\":{";
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
