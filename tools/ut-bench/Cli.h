// The ut-bench command line, as a function --
// docs/specs/UTA-0129-benchmark-tool.md SS 4.3.
//
// Bakes each map several times and says which step of the bake took the time,
// beside the machine and the build the figures were measured on. A developer's
// tool: its output carries a schema number, and is not one of
// docs/standards/versioning-overrides.md's breaking surfaces.
//
// Compiled into the ut-bench binary and into the unit tests, so the command
// line is tested without starting a process, as ut-bake's is.

#pragma once

#include "core/Timing.h"

#include <cstdint>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace uta::bench {

/// What compiled this program. main.cpp fills it from the build; a caller
/// that cannot say gets "unknown", which is what the output then carries.
struct BuildInfo {
    std::string compiler = "unknown";  ///< its id and version
    std::string buildType = "unknown";
    std::string sanitizer = "none";    ///< UTA_SANITIZE, or "none"
    std::string commit = "unknown";    ///< WriteBuildCommit.cmake's value
};

/// `args` excludes the program name. Returns the exit code: 0 when every map
/// was written or over budget and every run of a map gave one bundle, 1 when a
/// map was refused or its runs differed, 2 when the arguments were wrong.
[[nodiscard]] int runCli(std::span<const std::string_view> args, std::ostream& out, std::ostream& err,
                         const BuildInfo& build = {});

namespace detail {

/// Warns on `err` when the build or the machine makes the figures ones not to
/// compare, and returns the one-minute load average where there is one.
std::optional<double> conditions(const BuildInfo& build, std::ostream& err);

/// The `machine` and `build` members, as both workloads print them.
void writeMachineAndBuild(std::ostream& out, const BuildInfo& build, std::optional<double> load);

/// One run of one map, as summarise reads it.
struct Run {
    double total = 0;
    std::string bundleSha256; ///< empty when nothing was written
    std::vector<Phase> phases;
};

struct Spread {
    double min = 0, median = 0, max = 0;
};

/// The smallest, the median and the largest of `values`, in any order given.
/// The median of an even number is the mean of the middle two. Zeros when
/// there are none.
[[nodiscard]] Spread spreadOf(std::vector<double> values);

struct PhaseSummary {
    std::string name;
    unsigned depth = 0;
    std::uint64_t calls = 0; ///< the first run's that has the phase
    Spread seconds;          ///< over the runs that have the phase
    double share = 0;        ///< its minimum over the total's minimum
};

struct MapSummary {
    Spread total, unattributed;
    std::vector<PhaseSummary> phases; ///< in the first run's order
    bool identical = true;            ///< every run's bundle hashes as the first's
};

/// SS 4.3's figures for one map. `runs` is not empty.
[[nodiscard]] MapSummary summarise(const std::vector<Run>& runs);

} // namespace detail

} // namespace uta::bench
