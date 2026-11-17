// The weapon wheel's pointing and its ring -- Wheel.h.

#include "uui/Wheel.h"

#include <cmath>
#include <cstddef>
#include <numbers>
#include <utility>

namespace uta::uui {

void Wheel::open(std::vector<int> weapons) {
    open_ = true;
    weapons_ = std::move(weapons);
    item_ = -1;
    right_ = down_ = 0;
}

void Wheel::point(const uinput::Frame& frame) {
    if (!open_) return;
    if (const double stick = std::hypot(frame.pointRight, frame.pointDown); stick > STICK_POINTS) {
        right_ = frame.pointRight / stick;
        down_ = frame.pointDown / stick;
    } else {
        right_ += frame.lookRight / MOUSE_REACH;
        down_ -= frame.lookUp / MOUSE_REACH;
        if (const double length = std::hypot(right_, down_); length > 1) {
            right_ /= length;
            down_ /= length;
        }
    }
    const auto n = static_cast<int>(weapons_.size());
    if (n == 0 || std::hypot(right_, down_) <= PICKS_FROM) return;
    // Turns clockwise from the top: up is -down, and right is a quarter turn on.
    const double turn = std::atan2(right_, -down_) / (2 * std::numbers::pi);
    item_ = (static_cast<int>(std::lround(turn * n)) % n + n) % n;
}

int Wheel::close() {
    const int picked = highlighted();
    open_ = false;
    item_ = -1;
    return picked;
}

int Wheel::highlighted() const noexcept { return item_ < 0 ? -1 : weapons_[static_cast<std::size_t>(item_)]; }

std::vector<urender::OverlayLabel> layOut(const Wheel& wheel, std::span<const std::string> names) {
    std::vector<urender::OverlayLabel> labels;
    if (!wheel.isOpen()) return labels;
    const std::vector<int>& weapons = wheel.weapons();
    const auto n = static_cast<double>(weapons.size());
    for (std::size_t i = 0; i < weapons.size(); ++i) {
        const double angle = 2 * std::numbers::pi * static_cast<double>(i) / n;
        const auto weapon = static_cast<std::size_t>(weapons[i]);
        if (weapon >= names.size()) continue; // empty text would draw as the pointer
        labels.push_back({.text = names[weapon],
                          .x = RING_RADIUS * std::sin(angle),
                          .y = -RING_RADIUS * std::cos(angle),
                          .highlighted = weapons[i] == wheel.highlighted()});
    }
    labels.push_back({.x = POINTER_REACH * wheel.pointRight(), .y = POINTER_REACH * wheel.pointDown()});
    return labels;
}

} // namespace uta::uui
