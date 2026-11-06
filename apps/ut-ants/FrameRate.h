// UTA-0452: the viewer's frame-rate counter -- the numbers, not the drawing.
// Renderer::setOverlayText draws the text this produces.

#pragma once

#include <string>

namespace uta::client {

/// Frames counted over a window of wall time, so the number holds still long
/// enough to read rather than changing every frame.
class FrameRate {
public:
    /// How often the text changes, in seconds.
    static constexpr double WINDOW_SECONDS = 0.5;

    /// Count one frame that took `seconds` of wall time, start to start.
    /// True when the window closed and text() changed.
    bool add(double seconds);

    /// "60 FPS 16.7 MS": frames a second, and the mean frame's milliseconds.
    /// Empty until the first window closes.
    [[nodiscard]] const std::string& text() const noexcept { return text_; }

private:
    double elapsed_ = 0;
    int frames_ = 0;
    std::string text_;
};

} // namespace uta::client
