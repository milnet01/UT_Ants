// The simulation's fixed tick -- docs/specs/UTA-0017-movement-rules.md SS 4.7.
// Wall time goes in; whole ticks and how far the draw is past the last one
// come out. Every tick is the same length, so a body moves the same at every
// frame rate (INV-1). uworld reads no clock: the caller measures the frame.

#pragma once

namespace uta::uworld {

/// Ticks a wall-clock second: the original's own rate at 85 fps (SS 3 item 2).
inline constexpr int TICKS_PER_SECOND = 85;

/// UT99 deathmatch's default speed, Hardcore mode's TimeDilation (SS 3 item 3).
inline constexpr double DEFAULT_GAME_SPEED = 1.1;

/// The most ticks one advance runs: a longer stall drops the rest rather than
/// freezing the game while it catches up.
inline constexpr int MAX_TICKS_PER_ADVANCE = 8;

class FixedTick {
public:
    explicit FixedTick(double gameSpeed = DEFAULT_GAME_SPEED) noexcept;

    /// Adds `wallSeconds` at `scale` game speed and returns the whole ticks
    /// now due, at most MAX_TICKS_PER_ADVANCE. A negative or non-finite time
    /// adds nothing. A scale outside (0, 1], or not finite, counts as 1: a
    /// scale of 0 would run no tick, so no command could end it (UTA-0020
    /// SS 4.4). dt() is the same at any scale.
    int advance(double wallSeconds, double scale = 1) noexcept;

    /// One tick in game seconds: gameSpeed / TICKS_PER_SECOND.
    [[nodiscard]] double dt() const noexcept { return dt_; }

    /// 0 to 1: how far the draw is past the last tick, towards the next.
    [[nodiscard]] double blend() const noexcept { return owed_; }

private:
    double dt_;
    double owed_ = 0; ///< ticks owed but not yet run, below 1 between calls
};

} // namespace uta::uworld
