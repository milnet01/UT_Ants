// UTA-0263: camera-facing flames -- docs/specs/UTA-0263-shader-flames.md
// SS 4.4, INV-6 to INV-8.
//
// A FLAME'S PIXELS ARE READ FROM THE EMISSION TARGET. Bloom runs on every tier
// and spreads a glow over the colour image, so colour cannot say where the
// flame itself was drawn; the emission target is bloom's source and holds the
// flame alone. The scene has no surface, so every emitting pixel is flame.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstring>
#include <vector>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;

namespace {

constexpr std::uint32_t WIDTH = 128;
constexpr std::uint32_t HEIGHT = 128;
constexpr double PINNED = 1.0;

/// One flame 32 wide and 64 tall, its foot at (100, 0, -32), and its material
/// a flame look from black to bright orange, so a change of heat changes the
/// pixel. A default Camera faces it along +X.
uta::ubundle::Bundle flameRoom() {
    uta::ubundle::Bundle bundle = bundleOf(uta::ubundle::Geometry{});
    addSolidMaterial(bundle, "fire", Rgba{255, 255, 255, 255});
    uta::ubundle::FlameLook look;
    for (std::size_t i = 0; i < look.ramp.size(); ++i) {
        const float hot = static_cast<float>(i) / 7.0f;
        look.ramp[i] = {hot, 0.5f * hot, 0.1f * hot};
    }
    (*bundle.materials)[0].flame = look;
    uta::ubundle::Flame flame;
    flame.material = 0;
    flame.base = {100.0f, 0.0f, -32.0f};
    flame.width = 32;
    flame.height = 64;
    flame.seed = 1;
    bundle.flames = std::vector<uta::ubundle::Flame>{flame};
    return bundle;
}

/// Seen from beside the flame, looking along +Y: along the plane of a sheet
/// standing across Y, which is the plane a flame drawn in place would lie in.
Camera edgeOn() {
    Camera camera;
    camera.location = {100.0f, -100.0f, 0.0f};
    camera.rotation = {0, 16384, 0}; // a quarter turn of yaw: +Y
    return camera;
}

Config config() {
    Config out;
    out.width = WIDTH;
    out.height = HEIGHT;
    out.linearOutput = true;
    return out;
}

/// The emission target's floats after a draw of `bundle` from `camera` at
/// `seconds`, four a pixel.
std::vector<float> emissionAt(Renderer& renderer, const uta::ubundle::Bundle& bundle, const Camera& camera,
                              double seconds) {
    renderer.pinLightSeconds(seconds);
    requireOk(renderer.draw(bundle, camera));
    auto bytes = renderer.readback(Renderer::Target::Emission);
    REQUIRE(bytes.has_value());
    REQUIRE(bytes->size() == std::size_t{WIDTH} * HEIGHT * 4 * sizeof(float));
    std::vector<float> floats(bytes->size() / sizeof(float));
    std::memcpy(floats.data(), bytes->data(), bytes->size());
    return floats;
}

/// How many pixels emit anything.
std::size_t lit(const std::vector<float>& emission) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < emission.size(); i += 4)
        count += emission[i] > 0 || emission[i + 1] > 0 || emission[i + 2] > 0;
    return count;
}

} // namespace

TEST_CASE("UTA-0263 INV-6: a camera-facing flame is not paper-thin edge on", "[device][flames]") {
    removeDisplay();
    auto renderer = requireRenderer(config());
    const uta::ubundle::Bundle bundle = flameRoom();
    (void)emissionAt(renderer, bundle, Camera{}, PINNED); // the first frame is not comparable
    const std::size_t faceOn = lit(emissionAt(renderer, bundle, Camera{}, PINNED));
    const std::size_t edge = lit(emissionAt(renderer, bundle, edgeOn(), PINNED));
    INFO("pixels lit face on " << faceOn << ", edge on " << edge);
    REQUIRE(faceOn > 50);
    CHECK(edge * 2 >= faceOn);
}

TEST_CASE("UTA-0263 INV-7: a flame reaches the bloom through the emission target", "[device][flames]") {
    // Drawn in the translucent pass, which binds colour only, a flame would
    // show in colour and leave this target dark.
    removeDisplay();
    auto renderer = requireRenderer(config());
    const uta::ubundle::Bundle bundle = flameRoom();
    const std::vector<float> emission = emissionAt(renderer, bundle, Camera{}, PINNED);
    CHECK(lit(emission) > 50);
}

TEST_CASE("UTA-0263 INV-8: a pinned flame is still and a moving one moves", "[device][flames]") {
    removeDisplay();
    auto renderer = requireRenderer(config());
    const uta::ubundle::Bundle bundle = flameRoom();
    (void)emissionAt(renderer, bundle, Camera{}, PINNED);
    const std::vector<float> first = emissionAt(renderer, bundle, Camera{}, PINNED);
    const std::vector<float> again = emissionAt(renderer, bundle, Camera{}, PINNED);
    const std::vector<float> later = emissionAt(renderer, bundle, Camera{}, PINNED + 0.25);
    CHECK(first == again);

    // Over the pixels either frame lit, a quarter second moves a good share.
    std::size_t flame = 0;
    std::size_t moved = 0;
    for (std::size_t i = 0; i < first.size(); i += 4) {
        const bool here = first[i] > 0 || later[i] > 0;
        flame += here;
        moved += here && (first[i] != later[i] || first[i + 1] != later[i + 1] || first[i + 2] != later[i + 2]);
    }
    INFO("flame pixels " << flame << ", moved " << moved);
    REQUIRE(flame > 50);
    CHECK(moved * 4 >= flame);
}

TEST_CASE("UTA-0263: a surface with a flame look draws moving flame as emission", "[device][flames]") {
    // SS 4.4: a GEOM batch whose material has a flame look is drawn with the
    // flame shader instead of its picture. Its plain white picture emits
    // nothing, so any emission here is the flame's.
    removeDisplay();
    auto renderer = requireRenderer(config());
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, 60, "fire", 0);
    uta::ubundle::Bundle bundle = flameRoom();
    bundle.geometry = std::move(geometry);
    bundle.flames.reset();
    (void)emissionAt(renderer, bundle, Camera{}, PINNED);
    const std::vector<float> now = emissionAt(renderer, bundle, Camera{}, PINNED);
    const std::vector<float> later = emissionAt(renderer, bundle, Camera{}, PINNED + 0.25);
    INFO("surface pixels emitting " << lit(now));
    CHECK(lit(now) > 50);
    CHECK(now != later);
}
