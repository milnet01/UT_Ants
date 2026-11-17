// ut-export's camera path: walkers touring a map, for ExperiRays' H5 --
// UTA-0476.
//
// The file is EXRAY-0001 SS 4.6's camera-path JSON. What it holds was agreed
// with ExperiRays, 2026-11-17: walkers, each a UT player's box, walk a
// depth-first tour of the map's navigation graph from different PlayerStarts,
// so they pass through every zone the graph reaches; the camera chases the
// first. They are the path's actors, not scene nodes: SS 4.6's slot for what
// the scene file does not hold.

#pragma once

#include "core/Error.h"
#include "ubundle/Bundle.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace uta::tools::exporter {

struct PathOptions {
    unsigned walkers = 4;
    double seconds = 120;          ///< the path's length
    double framesPerSecond = 30;
    double speed = 400;            ///< UT units a second: a UT player's run
};

struct PathReport {
    std::size_t locatedNodes = 0;  ///< navigation points with a finite Location
    std::size_t walkers = 0;
    std::size_t frames = 0;
    std::size_t tourStops = 0;     ///< over every walker, backtracking included
};

/// The camera-path JSON for `bundle`'s scene, keyed to `sceneKey`. Refuses a
/// bundle with no NAVG or PLAC, or none of whose navigation points is placed.
[[nodiscard]] Result<std::string> writeCameraPath(const ubundle::Bundle& bundle, std::string_view sceneKey,
                                                  const PathOptions& options, PathReport& report);

} // namespace uta::tools::exporter
