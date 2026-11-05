// UTA-0341: props draw -- docs/specs/UTA-0341-mesh-decorations.md SS 4.6,
// INV-11 and INV-12.
//
// THE SCENE: a grey floor 40 units below the eye, seen along +X by a default
// Camera, lit by even probes; and a red cube, a prop, standing on it 150
// units ahead.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
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
constexpr Rgba RED{201, 31, 31, 255};

uta::ubundle::Geometry floorGeometry() {
    uta::ubundle::Geometry geometry;
    for (const auto& [x, y] : {std::pair{40.0F, -150.0F}, std::pair{300.0F, -150.0F}, std::pair{300.0F, 150.0F},
                               std::pair{40.0F, 150.0F}}) {
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

/// A cube of side 32 standing on the floor at `x` ahead, one vertex per
/// corner of each side, wound as GEOM is: (b - a) x (c - a) out.
uta::ubundle::Geometry cubeAt(float x) {
    uta::ubundle::Geometry geometry;
    const std::array<float, 3> centre = {x, 0, FLOOR_Z + 16};
    const std::array<std::array<float, 3>, 6> normals = {
        {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}}};
    for (const auto& n : normals) {
        // Two axes across the side, chosen so u x v = n.
        const std::array<float, 3> u = n[0] != 0 ? std::array<float, 3>{0, n[0], 0}
                                       : n[1] != 0 ? std::array<float, 3>{0, 0, n[1]}
                                                   : std::array<float, 3>{n[2], 0, 0};
        const std::array<float, 3> v = {n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0]};
        const auto first = static_cast<std::uint32_t>(geometry.vertices.size());
        for (const auto& [a, b] : {std::pair{-1.0F, -1.0F}, std::pair{1.0F, -1.0F}, std::pair{1.0F, 1.0F},
                                   std::pair{-1.0F, 1.0F}}) {
            uta::ubundle::GeometryVertex vertex;
            for (std::size_t k = 0; k < 3; ++k) {
                vertex.position[k] = centre[k] + 16 * (n[k] + a * u[k] + b * v[k]);
                vertex.normal[k] = n[k];
            }
            geometry.vertices.push_back(vertex);
        }
        for (const std::uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) geometry.indices.push_back(first + k);
    }
    geometry.batches = {uta::ubundle::GeometryBatch{"prop", 0, 0, static_cast<std::uint32_t>(geometry.indices.size()), {}}};
    return geometry;
}

uta::ubundle::Bundle room(bool withProp) {
    uta::ubundle::Bundle bundle = bundleOf(floorGeometry());
    addSolidMaterial(bundle, "floor", GREY);
    addSolidMaterial(bundle, "prop", RED);
    addEvenProbes(bundle, {0, -200, -100}, {400, 200, 100}, 0.5f);
    if (withProp) bundle.props = cubeAt(150);
    uta::ubundle::applyProps(bundle);
    return bundle;
}

Config configAt(Tier tier) {
    Config config;
    config.width = SIZE;
    config.height = SIZE;
    config.linearOutput = true;
    config.tier = tier;
    config.hazeScale = 0; // UTA-0015: no haze over the far floor
    return config;
}

std::vector<std::byte> drawOnce(Renderer& renderer, const uta::ubundle::Bundle& bundle) {
    renderer.pinLightSeconds(0);
    requireOk(renderer.draw(bundle, Camera{}));
    auto pixels = renderer.readback();
    if (!pixels.has_value()) FAIL(pixels.error().message());
    return std::move(*pixels);
}

std::size_t differing(const std::vector<std::byte>& a, const std::vector<std::byte>& b) {
    REQUIRE(a.size() == b.size());
    std::size_t count = 0;
    for (std::uint32_t y = 0; y < SIZE; ++y)
        for (std::uint32_t x = 0; x < SIZE; ++x) count += pixelAt(a, SIZE, x, y) != pixelAt(b, SIZE, x, y) ? 1 : 0;
    return count;
}

} // namespace

TEST_CASE("UTA-0341 INV-11: props draw at every tier", "[device][props]") {
    for (const Tier tier : {Tier::Low, Tier::High}) {
        Renderer withProp = requireRenderer(configAt(tier));
        Renderer without = requireRenderer(configAt(tier));
        const auto drawn = drawOnce(withProp, room(true));
        const auto bare = drawOnce(without, room(false));
        // The cube, 32 units across at 150 ahead, covers some 27 pixels square.
        CHECK(differing(drawn, bare) > 200);
    }
}

TEST_CASE("UTA-0341 INV-12: a changed PROP re-uploads", "[device][props]") {
    Renderer renderer = requireRenderer(configAt(Tier::High));
    uta::ubundle::Bundle bundle = room(true);
    const auto before = drawOnce(renderer, bundle);
    // The same bundle, edited in place: the cube moves 40 units to the side.
    for (auto& vertex : bundle.movers->back().geometry.vertices) vertex.position[1] += 40;
    const auto after = drawOnce(renderer, bundle);
    CHECK(differing(before, after) > 200);
}
