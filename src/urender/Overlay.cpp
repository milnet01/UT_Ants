// UTA-0452: the overlay's layout. Overlay.h says what draws it.

#include "urender/Overlay.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace uta::urender {

namespace {

constexpr std::uint32_t GLYPH_WIDTH = 5;
constexpr std::uint32_t GLYPH_HEIGHT = 7;
/// Font pixels between one line's glyphs and the next's.
constexpr std::uint32_t LINE_GAP = 2;

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
    {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}, // A
    {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110}, // B
    {0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110}, // C
    {0b11100, 0b10010, 0b10001, 0b10001, 0b10001, 0b10010, 0b11100}, // D
    {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111}, // E
    {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000}, // F
    {0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111}, // G
    {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}, // H
    {0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}, // I
    {0b00111, 0b00010, 0b00010, 0b00010, 0b00010, 0b10010, 0b01100}, // J
    {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001}, // K
    {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111}, // L
    {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001}, // M
    {0b10001, 0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001}, // N
    {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}, // O
    {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000}, // P
    {0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101}, // Q
    {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001}, // R
    {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}, // S
    {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}, // T
    {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}, // U
    {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100}, // V
    {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010}, // W
    {0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001}, // X
    {0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100}, // Y
    {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111}, // Z
    {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000}, // space
    {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b01100, 0b01100}, // .
    {0b00000, 0b01100, 0b01100, 0b00000, 0b01100, 0b01100, 0b00000}, // :
    {0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000}, // -
    {0b00000, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b00000}, // /
    {0b11000, 0b11001, 0b00010, 0b00100, 0b01000, 0b10011, 0b00011}, // %
}};

/// The crosshair, in overlay pixels (the text's own scale): each arm's
/// length, its gap from the centre, its thickness, and the black edge around it.
constexpr std::uint32_t ARM_LENGTH = 8;
constexpr std::uint32_t ARM_GAP = 3;
constexpr std::uint32_t ARM_THICKNESS = 1;

/// The text's lines, split at '\n'.
std::vector<std::string_view> linesOf(std::string_view text) {
    std::vector<std::string_view> lines;
    for (std::size_t start = 0;;) {
        const std::size_t end = text.find('\n', start);
        lines.push_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos) return lines;
        start = end + 1;
    }
}

/// `c`'s glyph, or npos for a space-like character.
std::size_t glyphOf(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return OVERLAY_CHARACTERS.find(c);
}

bool overlaps(const OverlayRect& a, const OverlayRect& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

/// One line of glyphs with its top-left at (`left`, `top`), as white runs.
void layOutLine(OverlayLayout& layout, std::string_view line, std::uint32_t left, std::uint32_t top,
                std::uint32_t scale) {
    const std::uint32_t advance = (GLYPH_WIDTH + 1) * scale;
    for (std::uint32_t c = 0; c < line.size(); ++c) {
        const std::size_t glyph = glyphOf(line[c]);
        if (glyph == std::string_view::npos) continue; // drawn as a space
        for (std::uint32_t row = 0; row < GLYPH_HEIGHT; ++row) {
            const std::uint8_t bits = GLYPHS[glyph][row];
            for (std::uint32_t column = 0; column < GLYPH_WIDTH;) {
                if ((bits & (0b10000u >> column)) == 0) {
                    ++column;
                    continue;
                }
                const std::uint32_t first = column;
                while (column < GLYPH_WIDTH && (bits & (0b10000u >> column)) != 0) ++column;
                layout.lit.push_back({.x = left + c * advance + first * scale,
                                      .y = top + row * scale,
                                      .width = (column - first) * scale,
                                      .height = scale});
            }
        }
    }
}

void layOutText(OverlayLayout& layout, std::string_view text, std::uint32_t width, std::uint32_t height,
                std::uint32_t scale) {
    if (text.empty()) return;
    const std::uint32_t pad = 2 * scale;
    const std::uint32_t advance = (GLYPH_WIDTH + 1) * scale;
    const std::uint32_t lineAdvance = (GLYPH_HEIGHT + LINE_GAP) * scale;
    const std::vector<std::string_view> lines = linesOf(text);
    std::size_t longest = 0;
    for (const std::string_view line : lines) longest = std::max(longest, line.size());
    const auto columns = static_cast<std::uint32_t>(longest);
    const auto rows = static_cast<std::uint32_t>(lines.size());

    const OverlayRect backing{.x = pad,
                              .y = pad,
                              .width = (columns == 0 ? 0 : columns * advance - scale) + 2 * pad,
                              .height = rows * lineAdvance - LINE_GAP * scale + 2 * pad};
    if (backing.x + backing.width > width || backing.y + backing.height > height) return;
    layout.backing = backing;

    for (std::uint32_t r = 0; r < rows; ++r)
        layOutLine(layout, lines[r], backing.x + pad, backing.y + pad + r * lineAdvance, scale);
}

/// Four arms about the image's centre, apart so no two edges touch, each a
/// white bar inside a black one.
void layOutCrosshair(OverlayLayout& layout, std::uint32_t width, std::uint32_t height, std::uint32_t scale) {
    const std::uint32_t length = ARM_LENGTH * scale;
    const std::uint32_t gap = ARM_GAP * scale;
    const std::uint32_t thickness = ARM_THICKNESS * scale;
    const std::uint32_t edge = std::max<std::uint32_t>(1, scale / 2);
    const std::uint32_t reach = gap + length + edge;
    const std::uint32_t cx = width / 2, cy = height / 2;
    if (cx < reach || cy < reach || cx + 1 + reach > width || cy + 1 + reach > height) return;
    // Symmetric about the centre pixel (cx, cy): each arm starts `gap` pixels
    // past it, and a bar's `thickness` rows (or columns) are centred on it.
    const std::uint32_t across = thickness / 2;
    const std::array<OverlayRect, 4> arms{{
        {.x = cx + 1 + gap, .y = cy - across, .width = length, .height = thickness},      // right
        {.x = cx - gap - length, .y = cy - across, .width = length, .height = thickness}, // left
        {.x = cx - across, .y = cy + 1 + gap, .width = thickness, .height = length},      // down
        {.x = cx - across, .y = cy - gap - length, .width = thickness, .height = length}, // up
    }};
    std::array<OverlayRect, 4> edges{};
    for (std::size_t i = 0; i < arms.size(); ++i) {
        edges[i] = {.x = arms[i].x - edge,
                    .y = arms[i].y - edge,
                    .width = arms[i].width + 2 * edge,
                    .height = arms[i].height + 2 * edge};
        if (layout.backing.width != 0 && overlaps(edges[i], layout.backing)) return; // the text wins
    }
    layout.edges.assign(edges.begin(), edges.end());
    layout.lit.insert(layout.lit.end(), arms.begin(), arms.end());
}

/// UTA-0020 SS 4.6: each label on its own black box, left out where the box
/// would leave the image or touch a black rectangle already laid out.
void layOutLabels(OverlayLayout& layout, std::span<const OverlayLabel> labels, std::uint32_t width,
                  std::uint32_t height, std::uint32_t scale) {
    const std::uint32_t advance = (GLYPH_WIDTH + 1) * scale;
    for (const OverlayLabel& label : labels) {
        const bool pointer = label.text.empty();
        const auto columns = static_cast<std::uint32_t>(label.text.size());
        // The pointer is a square two font pixels across in a one-pixel edge.
        const std::uint32_t textWidth = pointer ? 2 * scale : columns * advance - scale;
        const std::uint32_t textHeight = pointer ? 2 * scale : GLYPH_HEIGHT * scale;
        const std::uint32_t pad = pointer ? scale : 3 * scale;
        const std::uint32_t boxWidth = textWidth + 2 * pad, boxHeight = textHeight + 2 * pad;
        const double cx = width / 2.0 + label.x * height, cy = height / 2.0 + label.y * height;
        if (!(cx >= boxWidth / 2.0 && cy >= boxHeight / 2.0 && cx + boxWidth / 2.0 <= width
              && cy + boxHeight / 2.0 <= height))
            continue; // the negated form also leaves out a NaN position
        const OverlayRect box{.x = static_cast<std::uint32_t>(cx - boxWidth / 2.0),
                              .y = static_cast<std::uint32_t>(cy - boxHeight / 2.0),
                              .width = boxWidth,
                              .height = boxHeight};
        if (layout.backing.width != 0 && overlaps(box, layout.backing)) continue;
        if (std::ranges::any_of(layout.edges, [&box](const OverlayRect& edge) { return overlaps(box, edge); }))
            continue;
        layout.edges.push_back(box);
        const std::uint32_t left = box.x + pad, top = box.y + pad;
        if (pointer) {
            layout.lit.push_back({.x = left, .y = top, .width = textWidth, .height = textHeight});
            continue;
        }
        if (label.highlighted) { // a frame one font pixel wide, one in from the box's edge
            const std::uint32_t x0 = box.x + scale, y0 = box.y + scale;
            const std::uint32_t w = box.width - 2 * scale, h = box.height - 2 * scale;
            layout.lit.push_back({.x = x0, .y = y0, .width = w, .height = scale});
            layout.lit.push_back({.x = x0, .y = y0 + h - scale, .width = w, .height = scale});
            layout.lit.push_back({.x = x0, .y = y0 + scale, .width = scale, .height = h - 2 * scale});
            layout.lit.push_back({.x = x0 + w - scale, .y = y0 + scale, .width = scale, .height = h - 2 * scale});
        }
        layOutLine(layout, label.text, left, top, scale);
    }
}

} // namespace

std::uint64_t OverlayLayout::pixelCount() const noexcept {
    const auto area = [](const OverlayRect& rect) { return std::uint64_t{rect.width} * rect.height; };
    std::uint64_t largest = area(backing);
    for (const OverlayRect& rect : edges) largest = std::max(largest, area(rect));
    for (const OverlayRect& rect : lit) largest = std::max(largest, area(rect));
    return largest;
}

OverlayLayout layOutOverlay(std::string_view text, std::uint32_t width, std::uint32_t height, bool crosshair,
                            std::span<const OverlayLabel> labels) {
    OverlayLayout layout;
    // Two image pixels per font pixel at 720 lines, three at 1440, four at 2160.
    const std::uint32_t scale = std::max<std::uint32_t>(2, height / 480);
    layOutText(layout, text, width, height, scale);
    if (crosshair) layOutCrosshair(layout, width, height, scale);
    layOutLabels(layout, labels, width, height, scale);
    return layout;
}

} // namespace uta::urender
