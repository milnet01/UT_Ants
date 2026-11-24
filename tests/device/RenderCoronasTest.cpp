// UTA-0321: the glow UT99 draws about a bCorona light.
//
// EACH CASE COMPARES A FRAME WITH THE CORONA AGAINST THE SAME FRAME WITHOUT IT.
// The light also lights whatever surface the case has, so only the difference
// is the corona's.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdlib>
#include <vector>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;

namespace {

constexpr std::uint32_t WIDTH = 128;
constexpr std::uint32_t HEIGHT = 128;
constexpr double PINNED = 1.0;

/// A steady white light `distance` along +X from a default Camera, flagged
/// bCorona when `corona`, its DrawScale `scale`; and, when `wall`, a dark
/// square across the view 300 units out.
uta::ubundle::Bundle coronaScene(float distance, bool corona, float scale = 0.1f, bool wall = false) {
    uta::ubundle::Geometry geometry;
    if (wall) addSquare(geometry, 300.0f, 0.0f, 0.0f, 400.0f, "wall", 0);
    uta::ubundle::Bundle bundle = bundleOf(geometry);
    if (wall) addSolidMaterial(bundle, "wall", Rgba{1, 1, 1, 255});
    uta::ubundle::Light light;
    light.exportIndex = 1;
    light.location = {distance, 0.0f, 0.0f};
    light.type = 1;
    light.brightness = 200;
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

/// How many colour pixels the corona brightened: those brighter with it than
/// without, in any channel.
std::size_t glowing(Renderer& renderer, const uta::ubundle::Bundle& with, const uta::ubundle::Bundle& without) {
    (void)read(renderer, without, Renderer::Target::Colour); // the first frame is not comparable
    const std::vector<std::byte> base = read(renderer, without, Renderer::Target::Colour);
    const std::vector<std::byte> glow = read(renderer, with, Renderer::Target::Colour);
    REQUIRE(base.size() == glow.size());
    std::size_t count = 0;
    for (std::size_t i = 0; i < base.size(); i += 4) {
        bool brighter = false;
        for (std::size_t c = 0; c < 3; ++c)
            brighter = brighter || std::to_integer<int>(glow[i + c]) > std::to_integer<int>(base[i + c]);
        count += brighter;
    }
    return count;
}

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
    const std::size_t hidden =
        glowing(renderer, coronaScene(800.0f, true, 0.1f, true), coronaScene(800.0f, false, 0.1f, true));
    CHECK(hidden == 0);
}

TEST_CASE("UTA-0321: a corona fades away close to the eye", "[device][coronas]") {
    removeDisplay();
    auto renderer = requireRenderer(config());
    CHECK(glowing(renderer, coronaScene(80.0f, true), coronaScene(80.0f, false)) == 0);
}
