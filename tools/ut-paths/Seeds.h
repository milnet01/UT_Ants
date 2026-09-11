// The start, the exits, the network's part, and the routes between --
// docs/specs/UTA-0121-bot-path-seeds.md SS 4.6 and SS 4.7.
//
// SCOPE: this proposes positions. UT's editor links them, and UT_MonsterHunt's
// census re-run is what measures whether they help (that spec's SS 10).

#pragma once

#include "Trace.h"
#include "core/Error.h"
#include "ubundle/Bundle.h"
#include "upkg/Class.h"
#include "upkg/Package.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace uta::paths {

/// UT's CollisionRadius and CollisionHeight, the height a half-height.
struct Cylinder {
    Vec3 centre{};
    double radius = 0, height = 0;
};

struct Box {
    Vec3 min{}, max{};
};

/// One map, read and placed: what SS 4.6 and the movers' boxes produce.
struct Scene {
    ubundle::CollisionTree tree;                            ///< the level's
    std::vector<Vec3> network;                              ///< each navigation point's Location
    std::vector<std::pair<std::size_t, std::size_t>> edges; ///< into `network`, from then to; SS 3 decision 10's only
    Vec3 start{};
    std::vector<Cylinder> exits;                            ///< Location and collision size
    std::vector<Box> movers;                                ///< world boxes
};

enum class Route { Found, Mover, None };

struct Proposal {
    std::vector<Route> routes; ///< one per exit, in `Scene::exits` order
    std::vector<Vec3> nodes;
};

/// SS 4.6, and SS 4.7's movers, from a map whose Level and Model the bake
/// would read. The bake's own refusals come back as it gives them;
/// MalformedData, naming the map, when it has no PlayerStart or no MonsterEnd.
[[nodiscard]] Result<Scene> sceneOf(const upkg::Package& map, std::string_view mapName,
                                    const upkg::PackageResolver& resolver);

/// `partitioned` is whether the map's census group is PARTITIONED (SS 4.7).
[[nodiscard]] Proposal propose(const Scene& scene, bool partitioned);

/// SS 4.3's file.
[[nodiscard]] std::string toJson(std::string_view map, std::string_view md5, std::string_view group,
                                 const Scene& scene, const Proposal& proposal);

} // namespace uta::paths
