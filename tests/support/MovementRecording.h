// Reads a recording of the original game's player movement, as
// tools/ut-record writes it: one file a run and frame rate, format 1.
//
// docs/specs/UTA-0456-movement-recorder.md § 4.5 is the format and § 4.7 this
// reader. UTA-0457 and UTA-0017 read recordings only through this, so a
// truncated or malformed one is refused here rather than read as a short run.
//
// Replaying a run: start from row 0's state, apply row k's input over row k's
// dt, and compare with row k+1's state.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/Error.h"

namespace uta::test {

/// One player input tick. The input columns are this tick's; the state
/// columns are as the previous tick left them.
struct RecordedTick {
    std::int64_t tick = 0;
    double time = 0, dt = 0;  // Level.TimeSeconds, and PlayerInput's DeltaTime (game seconds)
    std::string keys;         // the letters held, "-" for none
    double forward = 0, strafe = 0;  // aBaseY, aStrafe as the original's PlayerInput receives them
    std::array<double, 3> location{}, velocity{}, acceleration{};
    std::string physics, dodge, anim, base;  // PHYS_Walking, DODGE_None, the AnimSequence, the Base's name or None
};

struct MovementRecording {
    int version = 0;
    std::string run, map;
    double rate = 0;              // the frame-rate cap asked for, not the rate achieved
    std::array<double, 4> pose{};  // x, y, z, yaw
    std::map<std::string, double, std::less<>> pawn;  // the PAWN line's values, by name
    std::string pawnClass;
    std::vector<RecordedTick> ticks;
};

/// Refuses a version other than 1; a missing BEGIN, PAWN or END; an END count
/// that differs from the rows; a row with the wrong column count or a field
/// that does not parse; rows whose ticks are not 0, 1, 2 in order; and rows
/// whose times step by other than the later row's dt (INV-4).
[[nodiscard]] Result<MovementRecording> readMovementRecording(std::string_view text);

}  // namespace uta::test
