// Devices to actions -- Input.h.

#include "uinput/Input.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace uta::uinput {
namespace {

std::size_t index(Action action) noexcept {
    return static_cast<std::size_t>(action);
}

/// How far past `zone` `travel` is, rescaled to run from 0 to 1.
double pastDeadZone(double travel, double zone) noexcept {
    return std::clamp((travel - zone) / (1.0 - zone), 0.0, 1.0);
}

/// A stick's direction, its length past the dead zone raised to `power`.
std::array<double, 2> stick(float x, float y, double zone, int power) noexcept {
    const double length = std::hypot(x, y);
    if (length == 0) return {};
    const double scale = std::pow(pastDeadZone(length, zone), power) / length;
    return {x * scale, y * scale};
}

} // namespace

Bindings defaultBindings() {
    return Bindings{
        .keys = {{Key::W, Action::Forward},
                 {Key::S, Action::Back},
                 {Key::A, Action::Left},
                 {Key::D, Action::Right},
                 {Key::Space, Action::Up},
                 {Key::LeftCtrl, Action::Down},
                 {Key::RightCtrl, Action::Down},
                 {Key::LeftShift, Action::Walk},
                 {Key::RightShift, Action::Walk},
                 {Key::Escape, Action::Quit},
                 {Key::F, Action::Flashlight},
                 {Key::G, Action::ToggleWalking},
                 {Key::H, Action::FrameRate},
                 {Key::LeftBracket, Action::FovNarrower},
                 {Key::RightBracket, Action::FovWider},
                 {Key::P, Action::SaveCamera},
                 {Key::F12, Action::Capture},
                 {Key::N1, Action::Weapon1},
                 {Key::N2, Action::Weapon2},
                 {Key::N3, Action::Weapon3},
                 {Key::N4, Action::Weapon4},
                 {Key::N5, Action::Weapon5},
                 {Key::N6, Action::Weapon6},
                 {Key::N7, Action::Weapon7},
                 {Key::N8, Action::Weapon8},
                 {Key::N9, Action::Weapon9},
                 {Key::N0, Action::Weapon0},
                 {Key::T, Action::DropDummy}},
        .mouseButtons = {{MouseButton::Left, Action::Fire}, {MouseButton::Right, Action::AltFire}},
        .padButtons = {{PadButton::South, Action::Up},
                       {PadButton::RightShoulder, Action::Up},
                       {PadButton::LeftShoulder, Action::Down},
                       {PadButton::LeftStick, Action::Walk},
                       {PadButton::East, Action::Dodge},
                       {PadButton::Start, Action::Quit}, // UTA-0183: Options, or Menu on an Xbox pad
                       {PadButton::North, Action::Flashlight},
                       {PadButton::RightStick, Action::ToggleWalking},
                       {PadButton::Back, Action::SaveCamera}, // Share or Create, or View on an Xbox pad
                       {PadButton::West, Action::Capture},
                       {PadButton::DpadRight, Action::NextWeapon},
                       {PadButton::DpadLeft, Action::PrevWeapon},
                       {PadButton::DpadUp, Action::DropDummy}},
    };
}

Input::Input(Bindings bindings, Settings settings) : bindings_(std::move(bindings)), settings_(settings) {}

void Input::keyDown(Key key, bool repeat) noexcept {
    for (const auto& [bound, action] : bindings_.keys) {
        if (bound == key && index(action) >= index(Action::Dodge) && (!repeat || repeats(action)))
            pending_.pressed.set(index(action));
    }
}

void Input::padButtonDown(PadButton button) noexcept {
    for (const auto& [bound, action] : bindings_.padButtons) {
        if (bound == button && index(action) >= index(Action::Dodge)) pending_.pressed.set(index(action));
    }
}

void Input::mouseMoved(float right, float down) noexcept {
    const double units = MOUSE_UNITS_PER_PIXEL * settings_.mouseSensitivity;
    pending_.lookRight += right * units;
    pending_.lookUp -= down * units; // SDL's y grows downward
}

void Input::mouseButton(MouseButton button, bool down) noexcept {
    const auto at = static_cast<std::size_t>(button);
    if (at >= mouseHeld_.size()) return;
    mouseHeld_.set(at, down);
    if (!down) return;
    for (const auto& [bound, action] : bindings_.mouseButtons) {
        if (bound == button && index(action) >= index(Action::Dodge)) pending_.pressed.set(index(action));
    }
}

void Input::mouseWheel(float away) noexcept {
    if (away != 0) pending_.pressed.set(index(away > 0 ? Action::NextWeapon : Action::PrevWeapon));
}

Frame Input::frame(std::span<const bool> keys, std::span<const PadState> pads, double seconds) noexcept {
    Frame out = pending_;
    pending_ = {};

    // Held actions as a set first, so two keys for one action count once.
    std::bitset<ACTION_COUNT> held;
    for (const auto& [key, action] : bindings_.keys) {
        const auto at = static_cast<std::size_t>(key);
        if (at < keys.size() && keys[at]) held.set(index(action));
    }
    for (const auto& [button, action] : bindings_.mouseButtons) {
        if (mouseHeld_.test(static_cast<std::size_t>(button))) held.set(index(action));
    }
    double forward = 0, right = 0, up = 0;
    const double lookPerSecond = STICK_LOOK_RATE * settings_.stickLookSensitivity;
    for (const PadState& pad : pads) {
        for (const auto& [button, action] : bindings_.padButtons) {
            if (pad.held.test(static_cast<std::size_t>(button))) held.set(index(action));
        }
        const auto move = stick(pad.leftX, pad.leftY, settings_.stickDeadZone, settings_.moveCurve);
        right += move[0];
        forward -= move[1]; // SDL's y grows downward
        const double rightTrigger = pastDeadZone(pad.rightTrigger, settings_.triggerDeadZone);
        const double leftTrigger = pastDeadZone(pad.leftTrigger, settings_.triggerDeadZone);
        if (settings_.triggersFire) {
            if (rightTrigger > 0) held.set(index(Action::Fire));
            if (leftTrigger > 0) held.set(index(Action::AltFire));
        } else {
            up += rightTrigger - leftTrigger;
        }
        const auto look = stick(pad.rightX, pad.rightY, settings_.stickDeadZone, settings_.lookCurve);
        out.lookRight += look[0] * lookPerSecond * seconds;
        out.lookUp -= look[1] * lookPerSecond * seconds;
    }
    const auto on = [&held](Action action) { return held.test(index(action)) ? 1.0 : 0.0; };
    forward += on(Action::Forward) - on(Action::Back);
    right += on(Action::Right) - on(Action::Left);
    up += on(Action::Up) - on(Action::Down);
    out.forward = static_cast<float>(std::clamp(forward, -1.0, 1.0));
    out.right = static_cast<float>(std::clamp(right, -1.0, 1.0));
    out.up = static_cast<float>(std::clamp(up, -1.0, 1.0));
    out.walk = held.test(index(Action::Walk));
    out.fire = held.test(index(Action::Fire));
    out.altFire = held.test(index(Action::AltFire));
    return out;
}

} // namespace uta::uinput
