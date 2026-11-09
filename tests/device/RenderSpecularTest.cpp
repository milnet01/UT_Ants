// UTA-0298: specular highlights, from High since UTA-0451. Each case draws at
// Ultra with Config::specular set and unset, so nothing else differs. A floor
// is glossy, and draws with them, while it is a metal or its roughness reaches
// below umat's default (UTA-0451, urender::GLOSSY_BELOW).
//
// THE SCENE: a floor 40 units below the eye, from 40 to 400 units ahead, seen
// along +X by a default Camera, lit by one light 60 units above it at 240
// ahead and by even probes. The light's mirror image falls on the floor 96
// units ahead, below the middle of the image.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace uta::test::render;
using uta::urender::Camera;
using uta::urender::Config;
using uta::urender::Renderer;
using uta::urender::Tier;

namespace {

constexpr std::uint32_t SIZE = 128;
constexpr float FLOOR_Z = -40;
/// Every channel odd, as bc7Solid needs.
constexpr Rgba GREY{127, 127, 127, 255};

uta::ubundle::Geometry floorGeometry() {
    uta::ubundle::Geometry geometry;
    for (const auto& [x, y] : {std::pair{40.0F, -200.0F}, std::pair{400.0F, -200.0F}, std::pair{400.0F, 200.0F},
                               std::pair{40.0F, 200.0F}}) {
        uta::ubundle::GeometryVertex vertex;
        vertex.position = {x, y, FLOOR_Z};
        vertex.normal = {0, 0, 1};
        vertex.u = x / 64;
        vertex.v = y / 64;
        geometry.vertices.push_back(vertex);
    }
    geometry.indices = {0, 1, 2, 0, 2, 3};
    geometry.batches = {uta::ubundle::GeometryBatch{"floor", 0, 0, 6, {}}};
    return geometry;
}

/// The floor wearing GREY with every texel's roughness `rough`, of 255.
uta::ubundle::Bundle room(std::uint8_t rough, bool metal = false) {
    uta::ubundle::Bundle bundle = bundleOf(floorGeometry());
    addSolidMaterial(bundle, "floor", GREY);
    bundle.materials->back().metallic = metal;
    uta::ubundle::CompressedTexture map;
    map.name = "floor:rough";
    map.format = uta::ubundle::BlockFormat::BC4;
    map.width = map.height = 4;
    map.sourceWidth = map.sourceHeight = 4;
    map.mipCount = 1;
    // One BC4 block: both endpoints the value, every index 0.
    map.blocks.assign(8, std::byte{0});
    map.blocks[0] = map.blocks[1] = std::byte{rough};
    bundle.textures->push_back(std::move(map));
    bundle.lights.emplace();
    bundle.lights->push_back(steadyLight({240, 0, FLOOR_Z + 60}, 160, 20));
    addEvenProbes(bundle, {0, -300, -100}, {500, 300, 100}, 0.3f);
    return bundle;
}

/// The bundle at Ultra, with highlights on or off.
std::vector<std::byte> frameOf(const uta::ubundle::Bundle& bundle, bool specular) {
    Config config;
    config.width = SIZE;
    config.height = SIZE;
    config.linearOutput = true;
    config.tier = Tier::Ultra;
    config.specular = specular;
    config.hazeScale = 0; // UTA-0015: no haze over the far floor
    Renderer renderer = requireRenderer(config);
    renderer.pinLightSeconds(0);
    requireOk(renderer.draw(bundle, Camera{}));
    auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    return std::move(*pixels);
}

/// The brightest pixel's green, and the sum of every pixel's.
std::pair<unsigned, std::uint64_t> brightness(const std::vector<std::byte>& frame) {
    unsigned peak = 0;
    std::uint64_t sum = 0;
    for (std::uint32_t y = 0; y < SIZE; ++y)
        for (std::uint32_t x = 0; x < SIZE; ++x) {
            const unsigned green = pixelAt(frame, SIZE, x, y).g;
            peak = std::max(peak, green);
            sum += green;
        }
    return {peak, sum};
}

} // namespace

TEST_CASE("UTA-0298: a smooth floor shows the light's highlight", "[device][specular]") {
    const auto off = brightness(frameOf(room(20), false));
    const auto on = brightness(frameOf(room(20), true));
    INFO("peak off " << off.first << ", on " << on.first);
    CHECK(on.first > off.first + 20);
}

TEST_CASE("UTA-0298: a rough floor keeps its light overall with highlights", "[device][specular]") {
    // Energy kept: what the highlight adds, the diffuse gives up, and the
    // probes' light comes back whole. 0.04% darker on lavapipe, 2026-11-06,
    // at roughness 240; a diffuse that gives up no Fresnel share drew 0.41%
    // brighter. UTA-0451: 140, as 240 is no longer glossy and would draw
    // without highlights either way.
    const auto off = brightness(frameOf(room(140), false));
    const auto on = brightness(frameOf(room(140), true));
    INFO("sum off " << off.second << ", on " << on.second);
    CHECK(on.second != off.second); // the floor drew with highlights at all
    CHECK(on.second <= off.second + off.second / 500);
    CHECK(on.second >= off.second - off.second / 100);
}

TEST_CASE("UTA-0298: a metal floor takes the probes' light as its reflection", "[device][specular]") {
    // With no diffuse, a metal away from its highlight is lit by the probes'
    // light through its own Fresnel term, not left black and not tinted a
    // second time: 3.8% darker than without highlights on lavapipe,
    // 2026-11-06. Shaded as a non-metal, with a diffuse, it drew 0.76% darker.
    const auto off = brightness(frameOf(room(240, true), false));
    const auto on = brightness(frameOf(room(240, true), true));
    INFO("sum off " << off.second << ", on " << on.second);
    CHECK(on.second < off.second - off.second / 100);
    CHECK(on.second > off.second * 8 / 10);
}

TEST_CASE("UTA-0451: a floor at the default roughness draws without highlights", "[device][specular]") {
    // umat's default reaches 159 at its lowest; above GLOSSY_BELOW the floor
    // takes the pipeline without highlights, so switching them on changes
    // no pixel.
    CHECK(frameOf(room(160), false) == frameOf(room(160), true));
}
