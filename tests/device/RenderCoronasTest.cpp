// UTA-0321: the glow UT99 draws about a bCorona light; UTA-0511: drawn as
// lens glare, dimmed as a whole by how much of the lamp shows.
//
// EACH CASE COMPARES A FRAME WITH THE CORONA AGAINST THE SAME FRAME WITHOUT IT.
// The light also lights whatever surface the case has, so only the difference
// is the corona's.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <numeric>
#include <string>
#include <vector>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;

namespace {

constexpr std::uint32_t WIDTH = 128;
constexpr std::uint32_t HEIGHT = 128;
constexpr double PINNED = 1.0;

/// What stands between the eye and the light: nothing, a square across the
/// whole view, or one whose top edge runs through the light's centre.
enum class Wall : std::uint8_t { None, Whole, LowerHalf };

/// A steady white light `distance` along +X from a default Camera, flagged
/// bCorona when `corona`, its DrawScale `scale` and LightBrightness
/// `brightness`; and `wall`, a dark square 300 units out.
uta::ubundle::Bundle coronaScene(float distance, bool corona, float scale = 0.1f, Wall wall = Wall::None,
                                 std::uint8_t brightness = 200) {
    uta::ubundle::Geometry geometry;
    if (wall == Wall::Whole) addSquare(geometry, 300.0f, 0.0f, 0.0f, 400.0f, "wall", 0);
    if (wall == Wall::LowerHalf) addSquare(geometry, 300.0f, 0.0f, -200.0f, 200.0f, "wall", 0);
    uta::ubundle::Bundle bundle = bundleOf(geometry);
    if (wall != Wall::None) addSolidMaterial(bundle, "wall", Rgba{1, 1, 1, 255});
    uta::ubundle::Light light;
    light.exportIndex = 1;
    light.location = {distance, 0.0f, 0.0f};
    light.type = 1;
    light.brightness = brightness;
    light.saturation = 255;
    light.radius = 8;
    light.corona = corona;
    bundle.lights = std::vector<uta::ubundle::Light>{light};
    uta::ubundle::ActorClass lightClass;
    lightClass.path = "engine.light";
    lightClass.resolved = true;
    uta::ubundle::ActorPlacement placed;
    placed.exportIndex = 1;
    placed.path = "mylevel.light1";
    placed.properties.push_back({"DrawScale", 0, uta::ubundle::ValueKind::Float, scale});
    bundle.placements = uta::ubundle::Placements{{lightClass}, {placed}};
    return bundle;
}

/// coronaScene's light wearing, as its Skin, a solid white picture the bundle
/// carries as a material (UTA-0510).
uta::ubundle::Bundle pictureScene(float distance, bool corona) {
    uta::ubundle::Bundle bundle = coronaScene(distance, corona);
    addSolidMaterial(bundle, "genfx.flare", Rgba{255, 255, 255, 255});
    bundle.placements->actors[0].properties.push_back(
        {"Skin", 0, uta::ubundle::ValueKind::Object, std::string{"genfx.flare"}});
    return bundle;
}

Config config() {
    Config out;
    out.width = WIDTH;
    out.height = HEIGHT;
    out.linearOutput = true;
    return out;
}

std::vector<std::byte> read(Renderer& renderer, const uta::ubundle::Bundle& bundle, Renderer::Target target) {
    renderer.pinLightSeconds(PINNED);
    requireOk(renderer.draw(bundle, Camera{}));
    auto bytes = renderer.readback(target);
    REQUIRE(bytes.has_value());
    return *bytes;
}

/// Per pixel, row by row from the top, the light the corona added: the
/// colour channels' sum with it less without it, never below 0.
std::vector<int> added(Renderer& renderer, const uta::ubundle::Bundle& with, const uta::ubundle::Bundle& without) {
    (void)read(renderer, without, Renderer::Target::Colour); // the first frame is not comparable
    const std::vector<std::byte> base = read(renderer, without, Renderer::Target::Colour);
    const std::vector<std::byte> glow = read(renderer, with, Renderer::Target::Colour);
    REQUIRE(base.size() == glow.size());
    std::vector<int> out;
    for (std::size_t i = 0; i < base.size(); i += 4) {
        int sum = 0;
        for (std::size_t c = 0; c < 3; ++c)
            sum += std::to_integer<int>(glow[i + c]) - std::to_integer<int>(base[i + c]);
        out.push_back(std::max(sum, 0));
    }
    return out;
}

/// How many colour pixels the corona brightened.
std::size_t glowing(Renderer& renderer, const uta::ubundle::Bundle& with, const uta::ubundle::Bundle& without) {
    const std::vector<int> light = added(renderer, with, without);
    return static_cast<std::size_t>(std::ranges::count_if(light, [](int v) { return v > 0; }));
}

/// The corona's whole added light.
long total(const std::vector<int>& light) { return std::accumulate(light.begin(), light.end(), 0L); }

} // namespace

TEST_CASE("UTA-0321: a corona light in view glows and a plain light does not", "[device][coronas]") {
    removeDisplay();
    auto renderer = requireRenderer(config());
    const std::size_t glow = glowing(renderer, coronaScene(800.0f, true), coronaScene(800.0f, false));
    INFO("pixels the corona brightened: " << glow);
    CHECK(glow > 50);
    CHECK(glow < WIDTH * HEIGHT / 4);
}

TEST_CASE("UTA-0321: a corona adds nothing to the emission target", "[device][coronas]") {
    // It is the glow itself; bloom spreading it would draw it twice.
    removeDisplay();
    auto renderer = requireRenderer(config());
    (void)read(renderer, coronaScene(800.0f, false), Renderer::Target::Emission);
    const std::vector<std::byte> without = read(renderer, coronaScene(800.0f, false), Renderer::Target::Emission);
    const std::vector<std::byte> with = read(renderer, coronaScene(800.0f, true), Renderer::Target::Emission);
    CHECK(with == without);
}

TEST_CASE("UTA-0321: a corona keeps its size on the screen at any distance", "[device][coronas]") {
    removeDisplay();
    auto renderer = requireRenderer(config());
    const std::size_t near = glowing(renderer, coronaScene(700.0f, true), coronaScene(700.0f, false));
    const std::size_t far = glowing(renderer, coronaScene(1400.0f, true), coronaScene(1400.0f, false));
    INFO("pixels brightened at 700 units " << near << ", at 1400 " << far);
    REQUIRE(near > 50);
    // Drawn at a fixed size in the world, the far one would cover a quarter.
    CHECK(std::abs(static_cast<long>(near) - static_cast<long>(far)) * 10 <= static_cast<long>(near));
}

TEST_CASE("UTA-0321: a corona grows with its light's DrawScale", "[device][coronas]") {
    removeDisplay();
    auto renderer = requireRenderer(config());
    const std::size_t small = glowing(renderer, coronaScene(800.0f, true, 0.1f), coronaScene(800.0f, false, 0.1f));
    const std::size_t large = glowing(renderer, coronaScene(800.0f, true, 0.2f), coronaScene(800.0f, false, 0.2f));
    INFO("pixels brightened at DrawScale 0.1 " << small << ", at 0.2 " << large);
    REQUIRE(small > 50);
    CHECK(large > 3 * small); // twice the radius, four times the area
}

TEST_CASE("UTA-0321: a wall between the eye and a corona light hides its corona", "[device][coronas]") {
    removeDisplay();
    auto renderer = requireRenderer(config());
    const std::size_t hidden = glowing(renderer, coronaScene(800.0f, true, 0.1f, Wall::Whole),
                                       coronaScene(800.0f, false, 0.1f, Wall::Whole));
    CHECK(hidden == 0);
}

TEST_CASE("UTA-0511: a half-hidden corona light dims its whole glow with no hard edge", "[device][coronas]") {
    removeDisplay();
    auto renderer = requireRenderer(config());
    const std::vector<int> open = added(renderer, coronaScene(800.0f, true), coronaScene(800.0f, false));
    const std::vector<int> half = added(renderer, coronaScene(800.0f, true, 0.1f, Wall::LowerHalf),
                                        coronaScene(800.0f, false, 0.1f, Wall::LowerHalf));
    // The glow over the wall, the lower half of the view, well below the edge.
    long overWall = 0;
    for (std::size_t row = HEIGHT / 2 + 3; row < HEIGHT; ++row)
        for (std::size_t column = 0; column < WIDTH; ++column) overWall += half[row * WIDTH + column];
    INFO("light added: unhidden " << total(open) << ", half hidden " << total(half) << ", over the wall " << overWall);
    REQUIRE(total(open) > 0);
    // Depth-tested per pixel, the wall would cut the glow off at its edge.
    CHECK(overWall > 0);
    CHECK(total(half) * 4 > total(open));     // dimmed, not hidden
    CHECK(total(half) * 4 < total(open) * 3); // and dimmed by about the hidden share
}

TEST_CASE("UTA-0511: a brighter corona light glows brighter", "[device][coronas]") {
    removeDisplay();
    auto renderer = requireRenderer(config());
    const long dim = total(added(renderer, coronaScene(800.0f, true, 0.1f, Wall::None, 60),
                                 coronaScene(800.0f, false, 0.1f, Wall::None, 60)));
    const long bright = total(added(renderer, coronaScene(800.0f, true, 0.1f, Wall::None, 200),
                                    coronaScene(800.0f, false, 0.1f, Wall::None, 200)));
    INFO("light added at LightBrightness 60 " << dim << ", at 200 " << bright);
    REQUIRE(dim > 0);
    CHECK(dim * 5 < bright * 4);
}

TEST_CASE("UTA-0511: a corona stays whole close to the eye", "[device][coronas]") {
    // The original fades it from 560 units in; lens glare does not.
    removeDisplay();
    auto renderer = requireRenderer(config());
    const std::size_t far = glowing(renderer, coronaScene(800.0f, true), coronaScene(800.0f, false));
    const std::size_t near = glowing(renderer, coronaScene(80.0f, true), coronaScene(80.0f, false));
    INFO("pixels brightened at 800 units " << far << ", at 80 " << near);
    REQUIRE(far > 50);
    CHECK(std::abs(static_cast<long>(near) - static_cast<long>(far)) * 10 <= static_cast<long>(far));
}

TEST_CASE("UTA-0510: a corona whose Skin is a material draws that picture", "[device][coronas]") {
    // A solid picture fills the whole square; the round glow only a disc in
    // it, about pi/4 of the square.
    removeDisplay();
    auto renderer = requireRenderer(config());
    const std::size_t round = glowing(renderer, coronaScene(800.0f, true), coronaScene(800.0f, false));
    const std::size_t picture = glowing(renderer, pictureScene(800.0f, true), pictureScene(800.0f, false));
    INFO("pixels brightened by the round glow " << round << ", by the picture " << picture);
    REQUIRE(round > 50);
    CHECK(picture * 100 > round * 115);
}
