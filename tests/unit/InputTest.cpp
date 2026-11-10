// UTA-0018: uinput, devices to one action set. Keys, buttons and sticks are
// hand-made state, as the client would read it from SDL. UTA-0167's gamepad
// tests moved here from ClientFlyCameraTest.cpp with the code they test.
//
// NO TEST NAME CONTAINS A COMMA -- BakeCliTest.cpp says why.

#include "uinput/Input.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

using Catch::Matchers::WithinAbs;
using uta::uinput::Action;
using uta::uinput::Frame;
using uta::uinput::Input;
using uta::uinput::Key;
using uta::uinput::PadButton;
using uta::uinput::PadState;

namespace {

constexpr float DEAD_ZONE = uta::uinput::Settings{}.stickDeadZone;

/// A keyboard with `held` down.
std::array<bool, 512> keyboard(std::initializer_list<Key> held) {
    std::array<bool, 512> keys{};
    for (const Key key : held) keys[static_cast<std::size_t>(key)] = true;
    return keys;
}

/// One frame of `pad` alone, held for `seconds`.
Frame padFrame(const PadState& pad, double seconds = 1, const uta::uinput::Settings& settings = {}) {
    Input input(uta::uinput::defaultBindings(), settings);
    const std::vector<PadState> pads{pad};
    return input.frame({}, pads, seconds);
}

PadState holding(PadButton button) {
    PadState pad;
    pad.held.set(static_cast<std::size_t>(button));
    return pad;
}

} // namespace

TEST_CASE("UTA-0018: a key and a pad button ask for the same action", "[uinput]") {
    Input input;
    const auto keys = keyboard({Key::Space});
    CHECK(input.frame(keys, {}, 1).up == 1);
    const std::vector<PadState> cross{holding(PadButton::South)};
    CHECK(input.frame({}, cross, 1).up == 1);

    input.keyDown(Key::F, false);
    CHECK(input.frame({}, {}, 1).wasPressed(Action::Flashlight));
    input.padButtonDown(PadButton::North);
    CHECK(input.frame({}, {}, 1).wasPressed(Action::Flashlight));
}

TEST_CASE("UTA-0018: a press lasts one frame and a repeat presses only what sweeps", "[uinput]") {
    Input input;
    input.keyDown(Key::G, false);
    CHECK(input.frame({}, {}, 1).wasPressed(Action::ToggleWalking));
    CHECK_FALSE(input.frame({}, {}, 1).wasPressed(Action::ToggleWalking));

    input.keyDown(Key::G, true); // held down: toggling again would flicker
    CHECK_FALSE(input.frame({}, {}, 1).wasPressed(Action::ToggleWalking));
    input.keyDown(Key::RightBracket, true); // held down: the field of view sweeps
    CHECK(input.frame({}, {}, 1).wasPressed(Action::FovWider));

    // A held action is never a press, however it arrives.
    input.keyDown(Key::W, false);
    CHECK(input.frame({}, {}, 1).pressed.none());
}

TEST_CASE("UTA-0018: two keys for one action count once and the axes stay within one", "[uinput]") {
    Input input;
    const auto bothCtrls = keyboard({Key::LeftCtrl, Key::RightCtrl});
    CHECK(input.frame(bothCtrls, {}, 1).up == -1);

    PadState pushed;
    pushed.leftY = -1; // forward on the stick too
    const std::vector<PadState> pads{pushed};
    CHECK(input.frame(keyboard({Key::W}), pads, 1).forward == 1);

    CHECK(input.frame(keyboard({Key::W, Key::S}), {}, 1).forward == 0);
}

TEST_CASE("UTA-0018: the mouse and the right stick each turn by their own sensitivity", "[uinput]") {
    Input input(uta::uinput::defaultBindings(), {.mouseSensitivity = 2, .stickLookSensitivity = 0.5});
    input.mouseMoved(10, -5); // right, and up: SDL's y grows downward
    const Frame mouse = input.frame({}, {}, 1);
    CHECK(mouse.lookRight == 10 * uta::uinput::MOUSE_UNITS_PER_PIXEL * 2);
    CHECK(mouse.lookUp == 5 * uta::uinput::MOUSE_UNITS_PER_PIXEL * 2);

    PadState right;
    right.rightX = 1;
    const Frame stick = padFrame(right, 0.25, input.settings());
    CHECK_THAT(stick.lookRight, WithinAbs(uta::uinput::STICK_LOOK_RATE * 0.5 * 0.25, 1e-6));
}

TEST_CASE("UTA-0167: a stick resting inside the dead zone moves nothing", "[uinput]") {
    const float inside = DEAD_ZONE * 0.9f;
    const Frame frame = padFrame(PadState{.leftX = inside, .rightY = -inside, .rightTrigger = inside});
    CHECK(frame.forward == 0);
    CHECK(frame.right == 0);
    CHECK(frame.up == 0);
    CHECK(frame.lookRight == 0);
    CHECK(frame.lookUp == 0);
}

TEST_CASE("UTA-0167: the left stick moves as W A S and D do", "[uinput]") {
    const Frame forward = padFrame(PadState{.leftY = -1}); // pushed away from the player
    CHECK_THAT(forward.forward, WithinAbs(1, 1e-6));
    CHECK_THAT(forward.right, WithinAbs(0, 1e-6));
    CHECK_THAT(padFrame(PadState{.leftX = 1}).right, WithinAbs(1, 1e-6));

    // Past the dead zone the move grows from nothing rather than jumping.
    CHECK_THAT(padFrame(PadState{.leftY = (1 + DEAD_ZONE) / 2}).forward, WithinAbs(-0.5, 1e-6));

    // A diagonal pushed into the stick's square corner is no longer than straight.
    const Frame corner = padFrame(PadState{.leftX = 1, .leftY = -1});
    CHECK_THAT(std::hypot(corner.forward, corner.right), WithinAbs(1, 1e-6));
}

TEST_CASE("UTA-0167: the right stick turns at STICK_LOOK_RATE and slower with its square", "[uinput]") {
    const Frame turn = padFrame(PadState{.rightX = 1}, 0.25);
    CHECK_THAT(turn.lookRight, WithinAbs(uta::uinput::STICK_LOOK_RATE * 0.25, 1e-6));
    const Frame tip = padFrame(PadState{.rightY = -1}, 0.25); // pushed away: the view tips up
    CHECK_THAT(tip.lookUp, WithinAbs(uta::uinput::STICK_LOOK_RATE * 0.25, 1e-6));

    // Halfway past the dead zone: a quarter of the rate, four times as long.
    const Frame half = padFrame(PadState{.rightX = (1 + DEAD_ZONE) / 2}, 1);
    CHECK_THAT(half.lookRight, WithinAbs(turn.lookRight, 1e-3)); // the float half-push rounds
}

TEST_CASE("UTA-0167: the triggers and shoulders are Up and Down and the stick press walks", "[uinput]") {
    CHECK_THAT(padFrame(PadState{.rightTrigger = 1}).up, WithinAbs(1, 1e-6));
    CHECK_THAT(padFrame(PadState{.leftTrigger = 1, .rightTrigger = 1}).up, WithinAbs(0, 1e-6));
    CHECK(padFrame(holding(PadButton::RightShoulder)).up == 1);
    CHECK(padFrame(holding(PadButton::LeftShoulder)).up == -1);
    CHECK(padFrame(holding(PadButton::LeftStick)).walk);

    // A pad at rest leaves the keyboard's input as it was.
    Input input;
    const std::vector<PadState> rest{PadState{}};
    const Frame keys = input.frame(keyboard({Key::W, Key::LeftCtrl, Key::LeftShift}), rest, 1);
    CHECK(keys.forward == 1);
    CHECK(keys.up == -1);
    CHECK(keys.walk);
}
