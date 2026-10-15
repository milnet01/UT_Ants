// The ut-dump command line, as a function -- UTA-0012, and UTA-0136's
// navigation graph.
//
// Compiled into the ut-dump binary and into the unit tests, so the command
// line is tested without starting a process.

#pragma once

#include "upkg/Geometry.h"

#include <cstddef>
#include <ostream>
#include <span>
#include <string_view>

namespace uta::dump {

/// UTA-0360: how much of a map's own lighting the editor built, as
/// `--lighting` writes it. A surface's lightmap is an index that resolves into
/// `lightMap`; its light actors are that entry's `iLightActors` other than -1.
struct LightingCounts {
    std::size_t surfaces = 0;
    std::size_t withLightMap = 0;
    std::size_t withLightActors = 0;
    std::size_t lightMaps = 0;
    std::size_t lightBitsBytes = 0;
    std::size_t lightActorRefs = 0;
};

[[nodiscard]] LightingCounts countLighting(const upkg::Model& model);

/// `args` excludes the program name. Writes one JSON document to `out` and
/// warnings to `err`. Returns 0 on a run, 2 when the arguments were wrong. A
/// package that does not open is reported inside the document, not by the
/// return value.
[[nodiscard]] int runCli(std::span<const std::string_view> args, std::ostream& out,
                         std::ostream& err);

} // namespace uta::dump
