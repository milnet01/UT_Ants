// UTA-0452: the on-screen text overlay -- the viewer's frame-rate counter.
//
// Device-free: this lays the text out as rectangles, and Swapchain::recordBlit
// copies solid colour into them on the presented image. No shader and no
// pipeline draw it, and readback reads the colour target, which it never
// touches, so a capture or a ut-shot image never shows it. Pure black and
// pure white read the same encoded or not, so the sRGB transfer has nothing
// to do here (UTA-0014 INV-10).
//
// The font is built in: five by seven pixels, the characters the counter
// needs and no others. A character it lacks is drawn as a space.

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace uta::urender {

struct OverlayRect {
    std::uint32_t x = 0, y = 0, width = 0, height = 0;
};

struct OverlayLayout {
    /// The opaque black box behind the text. Zero-sized when there is nothing to draw.
    OverlayRect backing;
    /// The text's lit pixels, one rectangle per horizontal run of a glyph row,
    /// each inside `backing` and none overlapping another.
    std::vector<OverlayRect> lit;

    [[nodiscard]] bool empty() const noexcept { return backing.width == 0 || backing.height == 0; }
    /// Texels in `backing`, which no rectangle of the layout exceeds: the
    /// size of each colour's run in the buffer the copies read from.
    [[nodiscard]] std::uint64_t pixelCount() const noexcept {
        return static_cast<std::uint64_t>(backing.width) * backing.height;
    }
};

/// The characters the built-in font draws.
inline constexpr std::string_view OVERLAY_CHARACTERS = "0123456789. FPSM";

/// `text` laid out in the top-left corner of a `width` x `height` image, its
/// pixel size growing with the height so it reads the same on any screen.
/// Empty when `text` is empty or the box would not fit the image.
[[nodiscard]] OverlayLayout layOutOverlay(std::string_view text, std::uint32_t width, std::uint32_t height);

} // namespace uta::urender
