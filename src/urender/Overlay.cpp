// UTA-0452: the overlay's layout. Overlay.h says what draws it.

#include "urender/Overlay.h"

#include <algorithm>
#include <array>

namespace uta::urender {

namespace {

constexpr std::uint32_t GLYPH_WIDTH = 5;
constexpr std::uint32_t GLYPH_HEIGHT = 7;

/// One row per entry, the leftmost pixel in bit 4. Ordered as OVERLAY_CHARACTERS.
constexpr std::array<std::array<std::uint8_t, GLYPH_HEIGHT>, OVERLAY_CHARACTERS.size()> GLYPHS{{
    {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110}, // 0
    {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}, // 1
    {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111}, // 2
    {0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110}, // 3
    {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}, // 4
    {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110}, // 5
    {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110}, // 6
    {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}, // 7
    {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}, // 8
    {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}, // 9
    {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b01100, 0b01100}, // .
    {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000}, // space
    {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000}, // F
    {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000}, // P
    {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}, // S
    {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001}, // M
}};

} // namespace

OverlayLayout layOutOverlay(std::string_view text, std::uint32_t width, std::uint32_t height) {
    OverlayLayout layout;
    if (text.empty()) return layout;

    // Two image pixels per font pixel at 720 lines, three at 1440, four at 2160.
    const std::uint32_t scale = std::max<std::uint32_t>(2, height / 480);
    const std::uint32_t pad = 2 * scale;
    const std::uint32_t advance = (GLYPH_WIDTH + 1) * scale;
    const auto count = static_cast<std::uint32_t>(text.size());

    const OverlayRect backing{.x = pad,
                              .y = pad,
                              .width = (count * advance - scale) + 2 * pad,
                              .height = GLYPH_HEIGHT * scale + 2 * pad};
    if (backing.x + backing.width > width || backing.y + backing.height > height) return layout;
    layout.backing = backing;

    for (std::uint32_t c = 0; c < count; ++c) {
        const std::size_t glyph = OVERLAY_CHARACTERS.find(text[c]);
        if (glyph == std::string_view::npos) continue; // drawn as a space
        const std::uint32_t left = backing.x + pad + c * advance;
        for (std::uint32_t row = 0; row < GLYPH_HEIGHT; ++row) {
            const std::uint8_t bits = GLYPHS[glyph][row];
            for (std::uint32_t column = 0; column < GLYPH_WIDTH;) {
                if ((bits & (0b10000u >> column)) == 0) {
                    ++column;
                    continue;
                }
                const std::uint32_t first = column;
                while (column < GLYPH_WIDTH && (bits & (0b10000u >> column)) != 0) ++column;
                layout.lit.push_back({.x = left + first * scale,
                                      .y = backing.y + pad + row * scale,
                                      .width = (column - first) * scale,
                                      .height = scale});
            }
        }
    }
    return layout;
}

} // namespace uta::urender
