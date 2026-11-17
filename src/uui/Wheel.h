// The weapon wheel's pointing and its ring -- docs/specs/UTA-0020-weapon-wheel.md
// SS 4.6. ugame says what is in the wheel and which picks it honours; this
// says which item a pointer is on and where each is drawn (design rule 13).
//
// Held down, the wheel is open: the right stick past STICK_POINTS sets the
// pointer's direction, and mouse look moves it. Let go, a highlighted weapon
// is picked.

#pragma once

#include "uinput/Input.h"
#include "urender/Overlay.h"

#include <span>
#include <string>
#include <vector>

namespace uta::uui {

inline constexpr double STICK_POINTS = 0.5;  ///< stick travel past which the stick sets the pointer's direction
inline constexpr double PICKS_FROM = 0.35;   ///< pointer length past which it highlights
inline constexpr double MOUSE_REACH = 1600;  ///< UT angle units of mouse look from the centre to the rim

/// The ring's radius and the pointer's reach at the rim, as shares of the
/// image's height: the prototype's, which the user played (2026-11-17).
inline constexpr double RING_RADIUS = 0.28;
inline constexpr double POINTER_REACH = 0.14;

class Wheel {
public:
    /// Opens on `weapons` (from ugame::wheelWeapons), the pointer at the centre, nothing highlighted.
    void open(std::vector<int> weapons);
    /// One frame's pointing: the right stick past STICK_POINTS sets the
    /// pointer's direction at full length; otherwise the frame's look moves
    /// it, clamped to the rim. Past PICKS_FROM, the nearest item to the
    /// pointer's direction is highlighted. A shorter pointer keeps the
    /// highlight, so a flicked stick let go still picks.
    void point(const uinput::Frame& frame);
    /// Closes; the highlighted weapon, or -1 for none.
    [[nodiscard]] int close();

    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    /// A weapon index; -1 for none.
    [[nodiscard]] int highlighted() const noexcept;
    /// The weapons, clockwise from the top.
    [[nodiscard]] const std::vector<int>& weapons() const noexcept { return weapons_; }
    /// The pointer, its length at most 1, +down.
    [[nodiscard]] double pointRight() const noexcept { return right_; }
    [[nodiscard]] double pointDown() const noexcept { return down_; }

private:
    bool open_ = false;
    std::vector<int> weapons_;
    int item_ = -1; ///< an index into weapons_; -1 for none
    double right_ = 0, down_ = 0;
};

/// The open ring as overlay labels: item i of n at i/n of a turn clockwise
/// from the top, RING_RADIUS from the centre, `names[weapon]` its text, the
/// highlighted one framed; then the pointer. Nothing while the wheel is shut.
[[nodiscard]] std::vector<urender::OverlayLabel> layOut(const Wheel& wheel, std::span<const std::string> names);

} // namespace uta::uui
