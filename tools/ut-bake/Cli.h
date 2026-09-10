// The ut-bake command line, as a function -- docs/specs/UTA-0011-map-baker.md
// SS 4.8.
//
// Compiled into the ut-bake binary and into the unit tests, so the command
// line is tested without starting a process (SS 4.1).
//
// Standard output is exactly one JSON object; standard error carries anything
// meant for a person. `verdict`, `path`, `ok`, `problems` and the exit code are
// what UTA-0016 binds to, and every field is part of the command line's output
// shape, which docs/standards/versioning-overrides.md SS Breaking surfaces
// lists.

#pragma once

#include <cstdint>
#include <ostream>
#include <span>
#include <string_view>

namespace uta::ubake {

/// `args` excludes the program name. Returns the exit code: 0 when the install
/// checked out or the bake was written or found cached, 1 when the install
/// did not check out or the bake was refused or over budget, 2 when the
/// arguments were wrong.
[[nodiscard]] int runCli(std::span<const std::string_view> args, std::ostream& out,
                         std::ostream& err);

namespace detail {

/// `runCli` with the bake's budget given -- a test seam.
[[nodiscard]] int runCli(std::span<const std::string_view> args, std::ostream& out,
                         std::ostream& err, std::uint64_t budgetBytes);

} // namespace detail

} // namespace uta::ubake
