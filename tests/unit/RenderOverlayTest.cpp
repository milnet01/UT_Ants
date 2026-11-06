// UTA-0452: the overlay's layout, device-free (src/urender/Overlay.h).
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "urender/Overlay.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using uta::urender::layOutOverlay;
using uta::urender::OverlayLayout;
using uta::urender::OverlayRect;

namespace {

std::uint64_t litArea(const OverlayLayout& layout) {
    std::uint64_t area = 0;
    for (const OverlayRect& rect : layout.lit) area += std::uint64_t{rect.width} * rect.height;
    return area;
}

bool inside(const OverlayRect& inner, const OverlayRect& outer) {
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.width <= outer.x + outer.width
           && inner.y + inner.height <= outer.y + outer.height;
}

bool overlap(const OverlayRect& a, const OverlayRect& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

} // namespace

TEST_CASE("UTA-0452: the digit 1 lights its ten font pixels at the screen's scale", "[render]") {
    // 1440 lines: three image pixels per font pixel.
    const OverlayLayout layout = layOutOverlay("1", 2560, 1440);
    REQUIRE_FALSE(layout.empty());
    CHECK(litArea(layout) == 10u * 3 * 3);
    CHECK(layout.backing.height == 7u * 3 + 4 * 3);
}

TEST_CASE("UTA-0452: a space and a character without a glyph light nothing but take room", "[render]") {
    const OverlayLayout spaced = layOutOverlay(" ", 1280, 720);
    const OverlayLayout unknown = layOutOverlay("q", 1280, 720);
    CHECK(spaced.lit.empty());
    CHECK(unknown.lit.empty());
    CHECK(unknown.backing.width == spaced.backing.width);
    CHECK_FALSE(spaced.empty());
}

TEST_CASE("UTA-0452: every lit rectangle sits in the box and none overlaps another", "[render]") {
    const OverlayLayout layout = layOutOverlay("0123456789. FPSM", 3840, 2160);
    REQUIRE_FALSE(layout.lit.empty());
    for (std::size_t i = 0; i < layout.lit.size(); ++i) {
        CHECK(inside(layout.lit[i], layout.backing));
        for (std::size_t j = i + 1; j < layout.lit.size(); ++j) CHECK_FALSE(overlap(layout.lit[i], layout.lit[j]));
    }
    CHECK(litArea(layout) <= layout.pixelCount());
}

TEST_CASE("UTA-0452: empty text or a screen too small for the box draws nothing", "[render]") {
    CHECK(layOutOverlay("", 3840, 2160).empty());
    CHECK(layOutOverlay("60 FPS 16.7 MS", 40, 720).empty());
    CHECK(layOutOverlay("60 FPS 16.7 MS", 1280, 10).empty());
    CHECK(layOutOverlay("60 FPS 16.7 MS", 1280, 10).lit.empty());
}
