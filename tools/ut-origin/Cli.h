// The ut-origin command line, as a function -- UTA-0013's third quarantine
// check.
//
// Compiled into the ut-origin binary and into the unit tests, so the command
// line is tested without starting a process.
//
// FAIL CLOSED, per docs/specs/UTA-0008-bundle-container-and-origin.md SS 4.5:
// anything other than a readable header carrying Origin::Authored is "not
// authored" -- a missing or short file, a bad magic, an unsupported version,
// an unrecognised origin byte.

#pragma once

#include <ostream>
#include <span>
#include <string_view>

namespace uta::origin {

/// `args` excludes the program name and is one path. Standard output is one
/// word: `authored`, `derived` or `unreadable`; standard error says why a file
/// was unreadable. Returns 0 for `authored`, 1 for anything else, and 2 when
/// the arguments were wrong.
[[nodiscard]] int runCli(std::span<const std::string_view> args, std::ostream& out,
                         std::ostream& err);

} // namespace uta::origin
