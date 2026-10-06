// The ut-bound-scan command line, as a function -- UTA-0328.
//
// Compiled into the ut-bound-scan binary and into the unit tests, so the
// command line is tested without starting a process.
//
// The output is the hand-off agreed with UT_MonsterHunt for its map checker
// (GAME-0205): one JSON object per line, each naming the map by its file name
// and the MD5 of its bytes.

#pragma once

#include <ostream>
#include <span>
#include <string_view>

namespace uta::boundscan {

/// `args` excludes the program name: an optional `--tolerance <units>`
/// (default 2), then one or more maps or folders; a folder stands for the
/// `.unr` files directly inside it, in name order.
///
/// Standard output, per map, one line per finding, then one summary line:
///
///   {"kind":"miss","map":..,"md5":..,"node":..,"brush":..,"texture":..,"bound":..,"excess":..}
///   {"kind":"inverted"|"invalid", ..the same fields but excess.., "valid":0|1}
///   {"kind":"summary","map":..,"md5":..,"nodes":..,"checked":..,"misses":..,
///    "inverted":..,"invalid":..,"worst":..}
///
/// A map that cannot be read gives one `{"kind":"refused","map":..,"reason":..}`
/// line instead, with `md5` when the bytes were read. `brush` and `texture` are
/// "-" where the node has no surface or the surface names none.
///
/// Returns 0 when every map was scanned, 1 when any was refused, and 2 when
/// the arguments were wrong.
[[nodiscard]] int runCli(std::span<const std::string_view> args, std::ostream& out,
                         std::ostream& err);

} // namespace uta::boundscan
