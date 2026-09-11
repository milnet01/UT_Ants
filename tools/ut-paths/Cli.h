// The ut-paths command line, as a function --
// docs/specs/UTA-0121-bot-path-seeds.md SS 4.2.
//
// Compiled into the ut-paths binary and into the unit tests, as ut-bake's is,
// so the command line is tested without starting a process. Standard output is
// exactly one JSON object, also written to <out>/ut-paths-summary.json;
// standard error is for people.

#pragma once

#include <ostream>
#include <span>
#include <string_view>

namespace uta::paths {

/// `args` excludes the program name. Returns the exit code: 0 when every map
/// was written or skipped, 1 when any was refused, 2 when the arguments were
/// wrong.
[[nodiscard]] int runCli(std::span<const std::string_view> args, std::ostream& out,
                         std::ostream& err);

} // namespace uta::paths
