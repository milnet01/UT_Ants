// UTA-0163: the level's sky, the parts that need no device.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "urender/Sky.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>

using Catch::Matchers::WithinAbs;
namespace urender = uta::urender;
namespace ubundle = uta::ubundle;

namespace {

std::array<double, 4> transform(const urender::gpu::Mat4& m, const std::array<double, 4>& p) {
    std::array<double, 4> out{};
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column) out[row] += m[column * 4 + row] * p[column];
    return out;
}

ubundle::PropertyRecord locationOf(std::array<float, 3> where) {
    return {.name = "Location", .kind = ubundle::ValueKind::Vector, .value = where};
}

ubundle::PropertyRecord highDetail(bool on) {
    return {.name = "bHighDetail", .kind = ubundle::ValueKind::Bool, .value = on};
}

/// A bundle whose placements are `actors`, each of class 0 (a SkyZoneInfo
/// subclass, by ancestry) or class 1 (a plain ZoneInfo).
ubundle::Bundle levelWith(std::vector<ubundle::ActorPlacement> actors) {
    ubundle::Placements placements;
    placements.classes = {
        {.path = "mymod.cloudsky", .ancestry = {"engine.skyzoneinfo", "engine.zoneinfo", "engine.info"}},
        {.path = "engine.zoneinfo", .ancestry = {"engine.info"}}};
    for (std::size_t i = 0; i < actors.size(); ++i) actors[i].exportIndex = static_cast<std::uint32_t>(i + 1);
    placements.actors = std::move(actors);
    ubundle::Bundle bundle;
    bundle.placements = std::move(placements);
    return bundle;
}

} // namespace

TEST_CASE("UTA-0163: a level with no SkyZoneInfo has no sky", "[render]") {
    CHECK_FALSE(urender::skyViewOf(ubundle::Bundle{}).has_value());
    const auto bundle = levelWith({{.classIndex = 1, .properties = {locationOf({1, 2, 3})}}});
    CHECK_FALSE(urender::skyViewOf(bundle).has_value());
}

TEST_CASE("UTA-0163: the sky is the last SkyZoneInfo and then the last in high detail", "[render]") {
    SECTION("none in high detail: the last one") {
        const auto bundle = levelWith({{.classIndex = 0, .properties = {locationOf({1, 0, 0})}},
                                       {.classIndex = 1, .properties = {locationOf({9, 9, 9})}},
                                       {.classIndex = 0, .properties = {locationOf({2, 0, 0})}}});
        const auto sky = urender::skyViewOf(bundle);
        REQUIRE(sky.has_value());
        CHECK(sky->location == std::array<float, 3>{2, 0, 0});
    }
    SECTION("one in high detail: it wins over a later one") {
        const auto bundle = levelWith({{.classIndex = 0, .properties = {locationOf({1, 0, 0}), highDetail(true)}},
                                       {.classIndex = 0, .properties = {locationOf({2, 0, 0})}}});
        const auto sky = urender::skyViewOf(bundle);
        REQUIRE(sky.has_value());
        CHECK(sky->location == std::array<float, 3>{1, 0, 0});
    }
    SECTION("the class default counts when the actor does not set it") {
        auto bundle = levelWith({{.classIndex = 0, .properties = {locationOf({1, 0, 0})}},
                                 {.classIndex = 0, .properties = {locationOf({2, 0, 0}), highDetail(false)}}});
        bundle.placements->classes[0].defaults = {highDetail(true)};
        const auto sky = urender::skyViewOf(bundle);
        REQUIRE(sky.has_value());
        CHECK(sky->location == std::array<float, 3>{1, 0, 0});
    }
}

TEST_CASE("UTA-0163: each sky face looks along its own axis with up at the top", "[render]") {
    constexpr std::array<std::array<double, 3>, 6> AXES{
        {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    for (std::uint32_t face = 0; face < 6; ++face) {
        CAPTURE(face);
        const urender::gpu::ShadowFace sample = urender::skyFaceSample(face);
        const auto& axis = AXES[face];
        const auto centre = transform(sample.viewProj, {axis[0] * 100, axis[1] * 100, axis[2] * 100, 1});
        REQUIRE(centre[3] > 0);
        CHECK_THAT(centre[0] / centre[3], WithinAbs(0, 1e-4));
        CHECK_THAT(centre[1] / centre[3], WithinAbs(0, 1e-4));
        // 45 degrees off the axis is the square's edge: NDC 1.
        if (face < 4) {
            const auto above = transform(sample.viewProj, {axis[0] * 100, axis[1] * 100, 100, 1});
            CHECK_THAT(above[1] / above[3], WithinAbs(-1, 1e-4)); // Vulkan's +Y is down
        }
        // The face's rectangle is its cell of the three-by-two texture.
        CHECK_THAT(sample.atlasRect[0], WithinAbs(static_cast<float>(face % 3) / 3, 1e-6));
        CHECK_THAT(sample.atlasRect[1], WithinAbs(static_cast<float>(face / 3) / 2, 1e-6));
        CHECK_THAT(sample.atlasRect[2], WithinAbs(1.0 / 3, 1e-6));
        CHECK_THAT(sample.atlasRect[3], WithinAbs(0.5, 1e-6));
    }
}

TEST_CASE("UTA-0163: a face camera's shorter side spans 90 degrees", "[render]") {
    const urender::SkyView sky{{5, 6, 7}};
    SECTION("wide") {
        const urender::Camera camera = urender::skyFaceCamera(sky, 2, 1920, 1080);
        CHECK(camera.location == sky.location);
        CHECK(camera.rotation == urender::skyFaceRotation(2));
        CHECK_THAT(camera.verticalFovDegrees, WithinAbs(90, 1e-3));
    }
    SECTION("tall") {
        const urender::Camera camera = urender::skyFaceCamera(sky, 0, 1000, 2000);
        CHECK_THAT(camera.verticalFovDegrees, WithinAbs(2 * std::atan(2.0) * 180 / 3.14159265358979, 1e-3));
    }
}

namespace {

/// One BSP node on the plane z = 0: zone 2 above it, zone 1 below.
uta::umap::RoomMap skyAboveRooms() {
    uta::umap::RoomMap rooms;
    uta::umap::RoomMap::Node node;
    node.normal = {0.0F, 0.0F, 1.0F};
    node.iZone[0] = 1;
    node.iZone[1] = 2;
    rooms.nodes = {node};
    rooms.rooms.resize(2);
    rooms.rooms[0].zoneIndex = 1;
    rooms.rooms[1].zoneIndex = 2;
    rooms.roomForZone = {uta::umap::NO_ROOM, 0, 1};
    return rooms;
}

constexpr std::uint32_t PF_TRANSLUCENT = 0x4, PF_MASKED = 0x2, PF_MODULATED = 0x40, PF_FAKE_BACKDROP = 0x80;

/// A square sheet 2000 wide at height `z`, in `zone`, its texture repeating
/// every 256 units from an offset of `u0` along x.
void addSheet(ubundle::Geometry& geometry, float z, std::uint8_t zone, float u0, std::uint32_t flags,
              std::array<float, 2> panRate) {
    const auto first = static_cast<std::uint32_t>(geometry.vertices.size());
    for (const auto& [x, y] : std::array<std::array<float, 2>, 4>{{{-1000, -1000}, {1000, -1000}, {1000, 1000}, {-1000, 1000}}}) {
        ubundle::GeometryVertex vertex;
        vertex.position = {x, y, z};
        vertex.u = x / 256 + u0;
        vertex.v = y / 256;
        vertex.zone = zone;
        geometry.vertices.push_back(vertex);
    }
    const auto firstIndex = static_cast<std::uint32_t>(geometry.indices.size());
    for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) geometry.indices.push_back(first + i);
    geometry.batches.push_back({.material = "clouds", .polyFlags = flags, .firstIndex = firstIndex, .indexCount = 6,
                                .panRate = panRate});
}

/// A level whose SkyZoneInfo stands at z = 100, in zone 2, with `geometry`.
ubundle::Bundle skyLevel(ubundle::Geometry geometry) {
    ubundle::Bundle bundle = levelWith({{.classIndex = 0, .properties = {locationOf({0, 0, 100})}}});
    bundle.rooms = skyAboveRooms();
    bundle.zones = std::vector<ubundle::Zone>(3);
    bundle.geometry = std::move(geometry);
    return bundle;
}

} // namespace

TEST_CASE("UTA-0337: a panned translucent sheet in the sky zone is a layer", "[render]") {
    ubundle::Geometry geometry;
    addSheet(geometry, 500, 2, 0.25F, PF_TRANSLUCENT, {0.5F, 0});
    const urender::SkyLayers sky = urender::skyLayersOf(skyLevel(geometry));
    CHECK(int(sky.zone) == 2);
    REQUIRE(sky.layers.size() == 1);
    const urender::SkyLayer& layer = sky.layers[0];
    CHECK(layer.firstIndex == 0);
    CHECK(layer.material == "clouds");
    CHECK(layer.panRate == std::array<float, 2>{0.5F, 0});
    CHECK(layer.kind == urender::gpu::SKY_LAYER_TRANSLUCENT);
    // Either winding: the plane is z = 500, its normal along z.
    CHECK_THAT(std::abs(layer.plane[2]), WithinAbs(1, 1e-6));
    CHECK_THAT(layer.plane[3] * layer.plane[2], WithinAbs(500, 1e-3));
    // The mapping gives back each corner's texture coordinates.
    for (const ubundle::GeometryVertex& vertex : geometry.vertices) {
        const auto& p = vertex.position;
        CHECK_THAT(layer.u[0] * p[0] + layer.u[1] * p[1] + layer.u[2] * p[2] + layer.u[3], WithinAbs(vertex.u, 1e-4));
        CHECK_THAT(layer.v[0] * p[0] + layer.v[1] * p[1] + layer.v[2] * p[2] + layer.v[3], WithinAbs(vertex.v, 1e-4));
    }
}

TEST_CASE("UTA-0337: only a panned sheet wholly in the sky zone and not modulated is a layer", "[render]") {
    ubundle::Geometry geometry;
    addSheet(geometry, 500, 1, 0, PF_TRANSLUCENT, {0.5F, 0});                  // out of the sky zone
    addSheet(geometry, 510, 2, 0, PF_TRANSLUCENT, {0, 0});                     // not panned
    addSheet(geometry, 520, 2, 0, PF_MODULATED, {0.5F, 0});                    // modulated
    addSheet(geometry, 530, 2, 0, PF_TRANSLUCENT | PF_FAKE_BACKDROP, {0.5F, 0}); // a window onto the sky
    addSheet(geometry, 540, 2, 0, PF_TRANSLUCENT, {0.5F, 0});                  // a corner both triangles share out of it
    geometry.vertices[geometry.vertices.size() - 4].zone = 1;
    CHECK(urender::skyLayersOf(skyLevel(geometry)).layers.empty());
}

TEST_CASE("UTA-0337: no sky view or no zone found gives no layers", "[render]") {
    ubundle::Geometry geometry;
    addSheet(geometry, 500, 2, 0, PF_TRANSLUCENT, {0.5F, 0});
    ubundle::Bundle noSky = skyLevel(geometry);
    noSky.placements->actors.clear();
    CHECK(urender::skyLayersOf(noSky).layers.empty());
    // zoneAt's 0 for no room is not taken for zone 0, whose sheet this is.
    ubundle::Geometry zoneZero;
    addSheet(zoneZero, 500, 0, 0, PF_TRANSLUCENT, {0.5F, 0});
    ubundle::Bundle noRoom = skyLevel(zoneZero);
    noRoom.rooms->roomForZone[2] = uta::umap::NO_ROOM;
    const urender::SkyLayers sky = urender::skyLayersOf(noRoom);
    CHECK(int(sky.zone) == 0);
    CHECK(sky.layers.empty());
}

TEST_CASE("UTA-0337: one plane and mapping is one layer whatever its whole repeats", "[render]") {
    ubundle::Geometry geometry;
    addSheet(geometry, 500, 2, 0.25F, PF_TRANSLUCENT, {0.5F, 0});
    // The second triangle's corners shifted by three whole repeats along u.
    for (const std::uint32_t i : {4u, 5u}) {
        ubundle::GeometryVertex vertex = geometry.vertices[geometry.indices[i]];
        vertex.u += 3;
        geometry.indices[i] = static_cast<std::uint32_t>(geometry.vertices.size());
        geometry.vertices.push_back(vertex);
    }
    geometry.indices[3] = static_cast<std::uint32_t>(geometry.vertices.size());
    ubundle::GeometryVertex corner = geometry.vertices[0];
    corner.u += 3;
    geometry.vertices.push_back(corner);
    CHECK(urender::skyLayersOf(skyLevel(geometry)).layers.size() == 1);

    SECTION("but half a repeat apart they are two") {
        for (std::size_t i = 4; i < geometry.vertices.size(); ++i) geometry.vertices[i].u += 0.5F;
        CHECK(urender::skyLayersOf(skyLevel(geometry)).layers.size() == 2);
    }
}

TEST_CASE("UTA-0337: layers are ordered farthest from the sky's eye first and keep their kind", "[render]") {
    ubundle::Geometry geometry;
    addSheet(geometry, 300, 2, 0, PF_MASKED, {0.5F, 0});
    addSheet(geometry, 900, 2, 0, 0, {0.5F, 0});
    addSheet(geometry, 600, 2, 0, PF_TRANSLUCENT, {0.5F, 0});
    const urender::SkyLayers sky = urender::skyLayersOf(skyLevel(geometry));
    REQUIRE(sky.layers.size() == 3);
    CHECK_THAT(sky.layers[0].plane[3] * sky.layers[0].plane[2], WithinAbs(900, 1e-3));
    CHECK(sky.layers[0].kind == urender::gpu::SKY_LAYER_OPAQUE);
    CHECK_THAT(sky.layers[1].plane[3] * sky.layers[1].plane[2], WithinAbs(600, 1e-3));
    CHECK(sky.layers[1].kind == urender::gpu::SKY_LAYER_TRANSLUCENT);
    CHECK_THAT(sky.layers[2].plane[3] * sky.layers[2].plane[2], WithinAbs(300, 1e-3));
    CHECK(sky.layers[2].kind == urender::gpu::SKY_LAYER_MASKED);
}

TEST_CASE("UTA-0337: a batch whose sheets pass the capacity is left out whole", "[render]") {
    ubundle::Geometry geometry;
    addSheet(geometry, 300, 2, 0, PF_TRANSLUCENT, {0.5F, 0});
    // One batch of capacity sheets: with the first, one too many.
    ubundle::Geometry many;
    for (std::uint32_t i = 0; i < urender::gpu::SKY_LAYER_CAPACITY; ++i)
        addSheet(many, 400 + 100 * static_cast<float>(i), 2, 0, PF_TRANSLUCENT, {0.25F, 0});
    const auto offset = static_cast<std::uint32_t>(geometry.vertices.size());
    const auto firstIndex = static_cast<std::uint32_t>(geometry.indices.size());
    geometry.vertices.insert(geometry.vertices.end(), many.vertices.begin(), many.vertices.end());
    for (const std::uint32_t index : many.indices) geometry.indices.push_back(offset + index);
    geometry.batches.push_back({.material = "more", .polyFlags = PF_TRANSLUCENT, .firstIndex = firstIndex,
                                .indexCount = static_cast<std::uint32_t>(many.indices.size()), .panRate = {0.25F, 0}});
    const urender::SkyLayers sky = urender::skyLayersOf(skyLevel(geometry));
    REQUIRE(sky.layers.size() == 1);
    CHECK(sky.layers[0].material == "clouds");
}
