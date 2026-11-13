// UTA-0452: the overlay's layout, device-free (src/urender/Overlay.h).
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "urender/Overlay.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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
    // UTA-0019 gave "q" a glyph (Q's); "#" still has none.
    const OverlayLayout unknown = layOutOverlay("#", 1280, 720);
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

TEST_CASE("UTA-0019: every letter and the new punctuation light pixels", "[render]") {
    for (const char c : std::string_view("ABCDEFGHIJKLMNOPQRSTUVWXYZ.:-/%0123456789")) {
        INFO("character " << c);
        CHECK_FALSE(layOutOverlay(std::string(1, c), 1280, 720).lit.empty());
    }
    CHECK(layOutOverlay("#", 1280, 720).lit.empty());
}

TEST_CASE("UTA-0019: no two letters share a glyph", "[render]") {
    // A glyph copied into the wrong row draws one letter as another.
    std::vector<std::vector<std::uint32_t>> seen;
    for (const char c : std::string_view("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:-/%")) {
        const OverlayLayout layout = layOutOverlay(std::string(1, c), 1280, 720);
        std::vector<std::uint32_t> shape;
        for (const OverlayRect& rect : layout.lit) shape.insert(shape.end(), {rect.x, rect.y, rect.width});
        INFO("character " << c);
        CHECK(std::find(seen.begin(), seen.end(), shape) == seen.end());
        seen.push_back(shape);
    }
}

TEST_CASE("UTA-0019: a lower-case letter draws as its upper case", "[render]") {
    const OverlayLayout lower = layOutOverlay("rocket 25", 1920, 1080);
    const OverlayLayout upper = layOutOverlay("ROCKET 25", 1920, 1080);
    REQUIRE_FALSE(upper.lit.empty());
    REQUIRE(lower.lit.size() == upper.lit.size());
    for (std::size_t i = 0; i < upper.lit.size(); ++i) {
        CHECK(lower.lit[i].x == upper.lit[i].x);
        CHECK(lower.lit[i].y == upper.lit[i].y);
        CHECK(lower.lit[i].width == upper.lit[i].width);
    }
}

TEST_CASE("UTA-0019: a newline starts a second line below the first in one box", "[render]") {
    // 720 lines: two image pixels per font pixel; a line is 7 of them and a gap of 2.
    const OverlayLayout one = layOutOverlay("AMMO 50", 1280, 720);
    const OverlayLayout two = layOutOverlay("AMMO 50\nHEALTH 100", 1280, 720);
    REQUIRE_FALSE(two.empty());
    CHECK(two.backing.x == one.backing.x);
    CHECK(two.backing.y == one.backing.y);
    CHECK(two.backing.height == one.backing.height + 9u * 2);
    // The box is as wide as the longer line.
    CHECK(two.backing.width == layOutOverlay("HEALTH 100", 1280, 720).backing.width);
    // The first line draws exactly as alone; the rest lie below it.
    REQUIRE(two.lit.size() > one.lit.size());
    std::uint32_t firstBottom = 0;
    for (std::size_t i = 0; i < one.lit.size(); ++i) {
        CHECK(two.lit[i].x == one.lit[i].x);
        CHECK(two.lit[i].y == one.lit[i].y);
        firstBottom = std::max(firstBottom, one.lit[i].y + one.lit[i].height);
    }
    for (std::size_t i = one.lit.size(); i < two.lit.size(); ++i) {
        CHECK(two.lit[i].y >= firstBottom + 2u * 2);
        CHECK(inside(two.lit[i], two.backing));
    }
}

TEST_CASE("UTA-0019: the crosshair is four white arms about the centre each in a black edge", "[render]") {
    const std::uint32_t width = 2560, height = 1440;
    const OverlayLayout layout = layOutOverlay("", width, height, true);
    REQUIRE_FALSE(layout.empty());
    REQUIRE(layout.lit.size() == 4);
    REQUIRE(layout.edges.size() == 4);
    // Centred: the arms' bounds are symmetric about the centre pixel.
    std::uint32_t left = width, right = 0, top = height, bottom = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const OverlayRect& arm = layout.lit[i];
        CHECK(inside(arm, layout.edges[i]));
        CHECK(layout.edges[i].width > arm.width);
        CHECK(layout.edges[i].height > arm.height);
        left = std::min(left, arm.x);
        right = std::max(right, arm.x + arm.width);
        top = std::min(top, arm.y);
        bottom = std::max(bottom, arm.y + arm.height);
        for (std::size_t j = i + 1; j < 4; ++j) {
            CHECK_FALSE(overlap(layout.edges[i], layout.edges[j]));
            CHECK_FALSE(overlap(layout.lit[i], layout.lit[j]));
        }
        CHECK(std::uint64_t{layout.edges[i].width} * layout.edges[i].height <= layout.pixelCount());
    }
    CHECK(left + right == width + 1);
    CHECK(top + bottom == height + 1);
    // Clearly visible: at 1440 lines it spans tens of pixels and its arms are several thick.
    CHECK(right - left >= 60);
    CHECK(std::min(layout.lit[0].height, layout.lit[2].width) >= 3);
    // Sized from the height, as the text is.
    const OverlayLayout larger = layOutOverlay("", 3840, 2160, true);
    REQUIRE(larger.lit.size() == 4);
    CHECK(larger.lit[0].width > layout.lit[0].width);
}

TEST_CASE("UTA-0019: the crosshair and the text share one layout without touching", "[render]") {
    const OverlayLayout layout = layOutOverlay("ROCKET LAUNCHER\nAMMO 48\nHEALTH 100", 1920, 1080, true);
    REQUIRE(layout.edges.size() == 4);
    REQUIRE(layout.lit.size() > 4);
    for (const OverlayRect& edge : layout.edges) CHECK_FALSE(overlap(edge, layout.backing));
    // The buffer the copies read holds the largest rectangle of either colour.
    for (const OverlayRect& rect : layout.lit)
        CHECK(std::uint64_t{rect.width} * rect.height <= layout.pixelCount());
    CHECK(std::uint64_t{layout.backing.width} * layout.backing.height <= layout.pixelCount());
    // A crosshair alone is not empty, nor is one left out when the image is too small.
    CHECK(layOutOverlay("", 20, 20, true).edges.empty());
    CHECK(layOutOverlay("", 20, 20, true).empty());
    CHECK(layOutOverlay("FPS", 1280, 720, false).edges.empty());
}

TEST_CASE("UTA-0019: the crosshair gives way to text that reaches it", "[render]") {
    // Ten lines of thirty at 360 lines reach past the centre of a 640-wide
    // image; black over black in one copy would overlap, which a copy forbids.
    std::string text;
    for (int line = 0; line < 10; ++line) text += "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123\n";
    const OverlayLayout layout = layOutOverlay(text, 640, 360, true);
    REQUIRE_FALSE(layout.empty());
    CHECK(layout.edges.empty());
    for (const OverlayRect& rect : layout.lit) CHECK(inside(rect, layout.backing));
}

TEST_CASE("UTA-0452: empty text or a screen too small for the box draws nothing", "[render]") {
    CHECK(layOutOverlay("", 3840, 2160).empty());
    CHECK(layOutOverlay("60 FPS 16.7 MS", 40, 720).empty());
    CHECK(layOutOverlay("60 FPS 16.7 MS", 1280, 10).empty());
    CHECK(layOutOverlay("60 FPS 16.7 MS", 1280, 10).lit.empty());
}
