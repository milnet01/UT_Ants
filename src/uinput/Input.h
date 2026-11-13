// Devices to actions -- UTA-0018, docs/design.md § The parts. Keyboard, mouse
// and gamepad are three sources of one action set: each binding names an
// action, and what reads a Frame never learns which device asked for it.
//
// No SDL here: the ThreadSanitizer leg builds the unit tests without it
// (scripts/ci.sh), and the tests drive this with hand-made device state. Key
// and button numbers are SDL's own; apps/ut-ants/main.cpp asserts each one
// against SDL's header.
//
// The bindings are built in until the settings menu (UTA-0120) lets a player
// change them (user, 2026-11-10). Settings holds what that menu will set.

#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace uta::uinput {

/// What a binding can ask for. Those before Dodge are held, read every frame;
/// the rest happen once a press.
enum class Action : std::uint8_t {
    Forward,
    Back,
    Left,
    Right,
    Up,   ///< jumps when walking, rises when flying
    Down, ///< ducks when walking, sinks when flying
    Walk, ///< walks when walking, flies fast when flying
    Fire,
    AltFire,
    Dodge,
    Quit,
    Flashlight,
    ToggleWalking,
    FrameRate,
    FovNarrower,
    FovWider,
    SaveCamera,
    Capture,
    NextWeapon,
    PrevWeapon,
    Weapon1, ///< the number keys, in the keyboard's order: 1 to 9, then 0
    Weapon2,
    Weapon3,
    Weapon4,
    Weapon5,
    Weapon6,
    Weapon7,
    Weapon8,
    Weapon9,
    Weapon0,
    DropDummy, ///< UTA-0019 SS 4.8: stands a target dummy ahead
    Count
};
inline constexpr std::size_t ACTION_COUNT = static_cast<std::size_t>(Action::Count);

/// Whether a key's auto-repeat presses `action` again: only the field of
/// view's, which a held key sweeps.
[[nodiscard]] constexpr bool repeats(Action action) noexcept {
    return action == Action::FovNarrower || action == Action::FovWider;
}

/// A key by its USB HID usage, the number SDL calls a scancode. Only the keys
/// the default bindings use are named.
enum class Key : std::uint16_t {
    A = 4,
    D = 7,
    F = 9,
    G = 10,
    H = 11,
    P = 19,
    S = 22,
    T = 23,
    W = 26,
    N1 = 30, ///< the number row: 1 to 9, then 0
    N2 = 31,
    N3 = 32,
    N4 = 33,
    N5 = 34,
    N6 = 35,
    N7 = 36,
    N8 = 37,
    N9 = 38,
    N0 = 39,
    Escape = 41,
    Space = 44,
    LeftBracket = 47,
    RightBracket = 48,
    F12 = 69,
    LeftCtrl = 224,
    LeftShift = 225,
    RightCtrl = 228,
    RightShift = 229,
};

/// A mouse button, by SDL's number.
enum class MouseButton : std::uint8_t { Left = 1, Middle = 2, Right = 3 };

/// A gamepad button, in SDL's order: positions, not labels, so South is Cross
/// on a PlayStation pad and A on an Xbox one.
enum class PadButton : std::uint8_t {
    South,
    East,
    West,
    North,
    Back,
    Guide,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Count
};
inline constexpr std::size_t PAD_BUTTON_COUNT = static_cast<std::size_t>(PadButton::Count);

/// One gamepad's state. The sticks run from -1 to 1 with +Y down, as SDL
/// reports them; the triggers from 0 to 1. The left stick moves, the right
/// one looks, the right trigger is Up and the left one Down -- or Fire and
/// AltFire, with Settings::triggersFire.
struct PadState {
    float leftX = 0, leftY = 0;
    float rightX = 0, rightY = 0;
    float leftTrigger = 0, rightTrigger = 0;
    std::bitset<PAD_BUTTON_COUNT> held;
};

/// UT angle units (65536 to a turn) one pixel of mouse motion turns the view,
/// at a sensitivity of 1.
inline constexpr double MOUSE_UNITS_PER_PIXEL = 16;
/// UT angle units a second the right stick held fully over turns the view, at
/// a sensitivity of 1: half a turn.
inline constexpr double STICK_LOOK_RATE = 32768;

/// What a player may tune. UTA-0120's menu sets these (user, 2026-11-10: the
/// right stick's sensitivity beside the mouse's).
struct Settings {
    /// A stick's travel, 0 to 1, ignored around its centre so a resting stick
    /// does not drift. The zone is round, so a diagonal starts where a
    /// straight push does.
    float stickDeadZone = 0.2f;
    /// A trigger's first stretch, likewise ignored.
    float triggerDeadZone = 0.2f;
    /// The move grows in step with the stick past its dead zone...
    int moveCurve = 1;
    /// ...and the look with its square, so a small push turns slowly enough
    /// to aim.
    int lookCurve = 2;
    double mouseSensitivity = 1;      ///< times MOUSE_UNITS_PER_PIXEL
    double stickLookSensitivity = 1;  ///< times STICK_LOOK_RATE
    /// The right trigger fires and the left one alt-fires, in place of Up and
    /// Down: the viewer sets it while walking (UTA-0019 SS 4.8).
    bool triggersFire = false;
};

/// Which key, mouse button and pad button asks for which action. One key may
/// serve one action; an action may have any number of keys and buttons.
struct Bindings {
    std::vector<std::pair<Key, Action>> keys;
    std::vector<std::pair<MouseButton, Action>> mouseButtons;
    std::vector<std::pair<PadButton, Action>> padButtons;
};

/// The built-in bindings: W, A, S and D or the left stick move; Space, Cross
/// or the right trigger or shoulder are Up; Ctrl or the left trigger or
/// shoulder are Down; Shift or pressing the left stick walks; Circle dodges;
/// the left and right mouse buttons fire and alt-fire; the number row picks a
/// weapon and the wheel or the D-pad's left and right steps through them; T
/// or the D-pad's up drops a dummy; and the viewer's keys as
/// apps/ut-ants/Cli.cpp's help lists them.
[[nodiscard]] Bindings defaultBindings();

/// One frame's actions, from every device at once.
struct Frame {
    float forward = 0; ///< -1 to 1; negative is back
    float right = 0;   ///< -1 to 1; negative is left
    float up = 0;      ///< -1 to 1; negative is Down
    double lookRight = 0; ///< UT angle units to turn the view right
    double lookUp = 0;    ///< UT angle units to tip the view up
    bool walk = false;
    bool fire = false, altFire = false;
    std::bitset<ACTION_COUNT> pressed; ///< the once-a-press actions this frame

    [[nodiscard]] bool wasPressed(Action action) const noexcept {
        return pressed.test(static_cast<std::size_t>(action));
    }
};

/// Gathers a frame's events, then reads the held state into a Frame.
class Input {
public:
    /// The default bindings and settings.
    Input() : Input(defaultBindings(), Settings{}) {}
    Input(Bindings bindings, Settings settings);

    [[nodiscard]] const Settings& settings() const noexcept { return settings_; }
    void setSettings(const Settings& settings) noexcept { settings_ = settings; }

    /// A key went down; `repeat` says it is the keyboard's auto-repeat.
    void keyDown(Key key, bool repeat) noexcept;
    void padButtonDown(PadButton button) noexcept;
    /// Mouse motion in pixels, +Y down as SDL reports it.
    void mouseMoved(float right, float down) noexcept;
    /// A mouse button went down or came up. Its held state is kept here: SDL
    /// has no table of it to hand `frame`, as it has of the keys.
    void mouseButton(MouseButton button, bool down) noexcept;
    /// The wheel turned: away from the player is NextWeapon, towards is
    /// PrevWeapon, as UT99 binds it.
    void mouseWheel(float away) noexcept;

    /// This frame: the events since the last one, then `keys` held (indexed
    /// by Key's number; shorter than a key's number means not held) and every
    /// pad's state held for `seconds`. The axes are clamped to -1 to 1.
    [[nodiscard]] Frame frame(std::span<const bool> keys, std::span<const PadState> pads, double seconds) noexcept;

private:
    Bindings bindings_;
    Settings settings_;
    Frame pending_; ///< what the events since the last frame added
    std::bitset<4> mouseHeld_; ///< by MouseButton's number
};

} // namespace uta::uinput
