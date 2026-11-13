// UTA-0452: the on-screen overlay -- the viewer's frame-rate counter, and
// since UTA-0019 SS 4.8 the weapon's name, its ammo, the player's health and a
// crosshair.
//
// Device-free: this lays the overlay out as rectangles, and
// Swapchain::recordBlit copies solid colour into them on the presented image.
// No shader and no pipeline draw it, and readback reads the colour target,
// which it never touches, so a capture or a ut-shot image never shows it. Pure
// black and pure white read the same encoded or not, so the sRGB transfer has
// nothing to do here (UTA-0014 INV-10).
//
// The font is built in: five by seven pixels, upper-case letters, digits and
// a little punctuation. A lower-case letter draws as its upper case, and a
// character the font lacks as a space. '\n' starts a new line.

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace uta::urender {

struct OverlayRect {
    std::uint32_t x = 0, y = 0, width = 0, height = 0;
};

/// Two copies draw it: every black rectangle (`backing`, then `edges`), then
/// every white one (`lit`). No two rectangles of one colour overlap, since
/// one copy's regions may not.
struct OverlayLayout {
    /// The opaque black box behind the text. Zero-sized when there is no text.
    OverlayRect backing;
    /// White: the text's lit pixels, one rectangle per horizontal run of a
    /// glyph row, each inside `backing`; then the crosshair's arms.
    std::vector<OverlayRect> lit;
    /// Black: the crosshair's edge, one rectangle around each arm. Empty with
    /// no crosshair.
    std::vector<OverlayRect> edges;

    [[nodiscard]] bool empty() const noexcept { return (backing.width == 0 || backing.height == 0) && edges.empty(); }
    /// Texels in the largest rectangle of the layout: the size of each
    /// colour's run in the buffer the copies read from.
    [[nodiscard]] std::uint64_t pixelCount() const noexcept;
};

/// The characters the built-in font draws. A lower-case letter draws as its upper case.
inline constexpr std::string_view OVERLAY_CHARACTERS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ .:-/%";

/// `text` laid out in the top-left corner of a `width` x `height` image, one
/// line per '\n', its pixel size growing with the height so it reads the same
/// on any screen; and with `crosshair`, a crosshair at the image's centre,
/// sized the same way. The text is left out when its box would not fit the
/// image; the crosshair when it would not fit, or would touch the text's box.
[[nodiscard]] OverlayLayout layOutOverlay(std::string_view text, std::uint32_t width, std::uint32_t height,
                                          bool crosshair = false);

} // namespace uta::urender
