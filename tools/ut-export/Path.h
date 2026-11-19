// ut-export's camera path: walkers touring a map, for ExperiRays' H5 --
// UTA-0476.
//
// The file is EXRAY-0001 SS 4.6's camera-path JSON. What it holds was agreed
// with ExperiRays, 2026-11-17: walkers, each a UT player's box, walk a
// depth-first tour of the map's navigation graph from different PlayerStarts,
// so they pass through every zone the graph reaches; the camera chases the
// first. They are the path's actors, not scene nodes: SS 4.6's slot for what
// the scene file does not hold.
//
// The scene's moving nodes move here too (SS 4.6's "nodes"). UT_Ants plays no
// mover yet, so each one runs its own keys on a loop: closed, out through its
// keys, open, and back, timed by its MoveTime and StayOpenTime.

#pragma once

#include "Scene.h"
#include "core/Error.h"
#include "ubundle/Bundle.h"

#include <cstddef>
#include <string>

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
    std::size_t movingMovers = 0;  ///< scene nodes with keys to move between
    std::size_t moverEntries = 0;  ///< over every frame: one a mover away from rest
    /// Frames in which a mover buildScene put in world space is turned from
    /// rest. Its node can carry the move but not the turn, so the turn is lost.
    std::size_t unturnedEntries = 0;
};

/// The camera-path JSON for `scene`, which buildScene made from `bundle`. A
/// frame lists each of the scene's movers that is away from rest then, and no
/// other. Refuses a bundle with no NAVG or PLAC, or none of whose navigation
/// points is placed.
[[nodiscard]] Result<std::string> writeCameraPath(const ubundle::Bundle& bundle, const Scene& scene,
                                                  const PathOptions& options, PathReport& report);

} // namespace uta::tools::exporter
