// The ut-dump command line, as a function -- UTA-0012, and UTA-0136's
// navigation graph.
//
// Compiled into the ut-dump binary and into the unit tests, so the command
// line is tested without starting a process.

#pragma once

#include <ostream>
#include <span>
#include <string_view>

namespace uta::dump {

/// `args` excludes the program name. Writes one JSON document to `out` and
/// warnings to `err`. Returns 0 on a run, 2 when the arguments were wrong. A
/// package that does not open is reported inside the document, not by the
/// return value.
[[nodiscard]] int runCli(std::span<const std::string_view> args, std::ostream& out,
                         std::ostream& err);

} // namespace uta::dump
