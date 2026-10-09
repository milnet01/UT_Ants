// UTA-0256's container cases: the LAMP section, LPRB's added-lamp cubes and
// SMSK's lamp light range -- INV-3.
//
// docs/specs/UTA-0256-added-lamps.md SS 4.2.
//
// THE GOLDEN ARRAY IS AUTHORED FROM SS 4.2, never produced by `write`, for
// BundleTextureTest.cpp's reason: a transposition present in both the reader
// and the writer round-trips perfectly.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "ubundle/Bundle.h"

#include "Bytes.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using uta::ErrorCode;
using uta::testing::Bytes;
using uta::ubundle::AddedLamp;
using uta::ubundle::Bundle;
using uta::ubundle::Flame;
using uta::ubundle::FlameLook;
using uta::ubundle::Geometry;
using uta::ubundle::GeometryBatch;
using uta::ubundle::GeometryVertex;
using uta::ubundle::Light;
using uta::ubundle::LightProbe;
using uta::ubundle::LightProbes;
using uta::ubundle::MaterialRecord;
using uta::ubundle::Origin;
using uta::ubundle::read;
using uta::ubundle::write;

namespace {

/// MATS with "a", no flame look, then "b", a flame whose ramp is all zero --
/// BundleFlamesTest.cpp's.
Bytes matsPayload() {
    Bytes out;
    out.u32(2);
    out.str("a");
    out.u8(0); // metallic
    out.u8(0); // parallaxDepth
    out.u8(0); // flame: none
    out.u8(0); // liquid: none
    out.u8(0); // fire: none
    out.u8(0); // tile kind: Fixed
    for (int i = 0; i < 32; ++i) out.u8(0);
    out.str("b");
    out.u8(0);
    out.u8(0);
    out.u8(1); // flame: a look follows
    for (int i = 0; i < 24; ++i) out.f32(0.0f);
    out.u8(0);
    out.u8(0);
    out.u8(0);
    for (int i = 0; i < 32; ++i) out.u8(0);
    return out;
}

std::vector<MaterialRecord> materials() {
    return {MaterialRecord{"a", false, 0, std::nullopt}, MaterialRecord{"b", false, 0, FlameLook{}}};
}

/// A light with every byte field different, so a swap shows.
Light goldenLight() {
    Light light;
    light.exportIndex = 41;
    light.location = {-1100.5f, 900.0f, 200.25f};
    light.rotation = {1, 16384, -3};
    light.type = 1;
    light.effect = 2;
    light.brightness = 200;
    light.hue = 24;
    light.saturation = 90;
    light.radius = 32;
    light.period = 7;
    light.phase = 8;
    light.cone = 9;
    light.volumeBrightness = 10;
    light.volumeRadius = 11;
    light.volumeFog = 12;
    light.specialLit = false;
    light.actorShadows = true;
    light.corona = false;
    light.lensFlare = true;
    light.levelBrightness = 1.5f;
    return light;
}

/// One triangle in one batch of material "a".
Geometry goldenShape() {
    Geometry shape;
    shape.vertices = {GeometryVertex{{0, 0, 1}, {0, 0, 1}, 0.5f, 0.25f, 0, {}},
                      GeometryVertex{{4, 0, 1}, {0, 0, 1}, 1, 0, 0, {}},
                      GeometryVertex{{0, 4, 1}, {0, 0, 1}, 0, 1, 0, {}}};
    shape.indices = {0, 1, 2};
    shape.batches = {GeometryBatch{"a", 0x40, 0, 3, {}}};
    return shape;
}

std::vector<AddedLamp> golden() {
    AddedLamp first{goldenLight(), goldenShape(), {Flame{1, {1.0f, 2.0f, 3.0f}, 8.0f, 24.0f, 5, -1}}};
    AddedLamp second{goldenLight(), Geometry{}, {Flame{1, {-1.0f, -2.0f, -3.0f}, 4.0f, 12.0f, 6, -1}}};
    second.light.exportIndex = 7; // a lamp keeps its template's slot; LAMP has no order
    return {first, second};
}

void putLight(Bytes& out, const Light& light) {
    out.u32(light.exportIndex);
    for (const float part : light.location) out.f32(part);
    for (const std::int32_t part : light.rotation) out.i32(part);
    for (const std::uint8_t field : {light.type, light.effect, light.brightness, light.hue, light.saturation,
                                     light.radius, light.period, light.phase, light.cone, light.volumeBrightness,
                                     light.volumeRadius, light.volumeFog})
        out.u8(field);
    for (const bool flag : {light.specialLit, light.actorShadows, light.corona, light.lensFlare}) out.u8(flag ? 1 : 0);
    out.u8(light.strip);
    for (const float part : light.stripFrom) out.f32(part);
    for (const float part : light.stripTo) out.f32(part);
    out.f32(light.levelBrightness);
}

void putShape(Bytes& out, const Geometry& shape) {
    out.u32(static_cast<std::uint32_t>(shape.vertices.size()));
    for (const GeometryVertex& vertex : shape.vertices) {
        for (const float part : vertex.position) out.f32(part);
        for (const float part : vertex.normal) out.f32(part);
        out.f32(vertex.u);
        out.f32(vertex.v);
        out.u8(vertex.zone);
    }
    out.u32(static_cast<std::uint32_t>(shape.indices.size()));
    for (const std::uint32_t index : shape.indices) out.u32(index);
    out.u32(static_cast<std::uint32_t>(shape.batches.size()));
    for (const GeometryBatch& batch : shape.batches) {
        out.str(batch.material);
        out.u32(batch.polyFlags);
        out.u32(batch.firstIndex);
        out.u32(batch.indexCount);
        for (const float part : batch.panRate) out.f32(part);
    }
}

/// SS 4.2's record: the light, the shape, then the flames.
Bytes lampPayload(const std::vector<AddedLamp>& lamps) {
    Bytes out;
    out.u32(static_cast<std::uint32_t>(lamps.size()));
    for (const AddedLamp& lamp : lamps) {
        putLight(out, lamp.light);
        putShape(out, lamp.shape);
        out.u32(static_cast<std::uint32_t>(lamp.flames.size()));
        for (const Flame& flame : lamp.flames) {
            out.u32(flame.material);
            for (const float coordinate : flame.base) out.f32(coordinate);
            out.f32(flame.width);
            out.f32(flame.height);
            out.u32(flame.seed);
            out.i32(flame.light);
        }
    }
    return out;
}

/// A whole .utab holding `sections` in the order given.
std::vector<std::byte> fileWith(const std::vector<std::pair<std::string_view, Bytes>>& sections) {
    Bytes out;
    out.id("UTAB");
    out.u32(25); // formatVersion -- 25 since UTA-0338 added SUN
    out.u8(1);   // origin: Authored
    out.u8(0);   // kind: Map
    out.u16(0);  // reserved
    out.u32(static_cast<std::uint32_t>(sections.size()));
    std::uint64_t offset = 16 + 24 * sections.size();
    for (const auto& [id, payload] : sections) {
        out.id(id);
        out.u64(offset);
        out.u64(payload.size());
        out.u8(0); // compression
        out.u8(0);
        out.u8(0);
        out.u8(0);
        offset += payload.size();
    }
    for (const auto& section : sections) out.append(section.second);
    return out.data();
}

Bundle bundleWith(std::vector<AddedLamp> lamps) {
    Bundle bundle;
    bundle.header.origin = Origin::Authored;
    bundle.materials = materials();
    bundle.lamps = std::move(lamps);
    return bundle;
}

/// SS 4.2's refusals of a lamp, one each, as a change to a good one.
const std::vector<std::pair<std::string_view, std::function<void(AddedLamp&)>>> BAD = {
    {"no fitting", [](AddedLamp& lamp) {
         lamp.shape = {};
         lamp.flames.clear();
     }},
    {"a light in a strip", [](AddedLamp& lamp) {
         lamp.light.strip = uta::ubundle::STRIP_LEADER;
         lamp.light.stripTo = {1, 0, 0};
     }},
    {"a light LITE refuses", [](AddedLamp& lamp) { lamp.light.levelBrightness = -1; }},
    {"a shape GEOM refuses", [](AddedLamp& lamp) { lamp.shape.indices.push_back(9); }},
    {"a flame tied to a light", [](AddedLamp& lamp) { lamp.flames.front().light = 0; }},
    {"a flame with no flame look", [](AddedLamp& lamp) { lamp.flames.front().material = 0; }},
    {"a flame that is not finite",
     [](AddedLamp& lamp) { lamp.flames.front().base[0] = std::numeric_limits<float>::infinity(); }},
};

} // namespace

TEST_CASE("UTA-0256 INV-3: the LAMP golden bytes decode to the lamps they encode", "[ubundle][lamp]") {
    const auto result = read(fileWith({{"MATS", matsPayload()}, {"LAMP", lampPayload(golden())}}));
    REQUIRE(result.has_value());
    REQUIRE(result->lamps.has_value());
    const std::vector<AddedLamp> want = golden();
    REQUIRE(result->lamps->size() == want.size());
    for (std::size_t i = 0; i < want.size(); ++i) {
        CAPTURE(i);
        const AddedLamp& got = (*result->lamps)[i];
        CHECK(got.light.exportIndex == want[i].light.exportIndex);
        CHECK(got.light.location == want[i].light.location);
        CHECK(got.light.rotation == want[i].light.rotation);
        CHECK(got.light.brightness == want[i].light.brightness);
        CHECK(got.light.volumeFog == want[i].light.volumeFog);
        CHECK(got.light.lensFlare == want[i].light.lensFlare);
        CHECK(got.light.levelBrightness == want[i].light.levelBrightness);
        REQUIRE(got.shape.vertices.size() == want[i].shape.vertices.size());
        for (std::size_t v = 0; v < want[i].shape.vertices.size(); ++v) {
            CHECK(got.shape.vertices[v].position == want[i].shape.vertices[v].position);
            CHECK(got.shape.vertices[v].u == want[i].shape.vertices[v].u);
        }
        CHECK(got.shape.indices == want[i].shape.indices);
        REQUIRE(got.shape.batches.size() == want[i].shape.batches.size());
        REQUIRE(got.flames.size() == 1);
        CHECK(got.flames[0].base == want[i].flames[0].base);
        CHECK(got.flames[0].seed == want[i].flames[0].seed);
    }
}

TEST_CASE("UTA-0256 INV-3: write emits MATS then LAMP as the golden bytes", "[ubundle][lamp]") {
    const auto written = write(bundleWith(golden()));
    REQUIRE(written.has_value());
    CHECK(*written == fileWith({{"MATS", matsPayload()}, {"LAMP", lampPayload(golden())}}));
}

TEST_CASE("UTA-0256 INV-3: read and write each refuse every invalid lamp", "[ubundle][lamp]") {
    for (const auto& [name, spoil] : BAD) {
        INFO(name);
        std::vector<AddedLamp> lamps = golden();
        spoil(lamps.back()); // the last lamp, so a check stopping early misses it

        const auto result = read(fileWith({{"MATS", matsPayload()}, {"LAMP", lampPayload(lamps)}}));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::MalformedData);
        CHECK(result.error().message().find("LAMP: lamp 1") != std::string_view::npos);

        const auto written = write(bundleWith(lamps));
        REQUIRE_FALSE(written.has_value());
        CHECK(written.error().code() == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("UTA-0256 INV-3: LPRB's added cubes are counted against LAMP", "[ubundle][lamp]") {
    LightProbes probes;
    probes.spacing = 32;
    probes.probes = {LightProbe{{0, 0, 0}, {}}, LightProbe{{1, 0, 0}, {}}};

    Bundle bundle = bundleWith(golden());
    bundle.lightProbes = probes;
    bundle.lightProbes->added.resize(2);
    bundle.lightProbes->added[1][3][2] = 0.75f;
    const auto written = write(bundle);
    REQUIRE(written.has_value());
    const auto back = read(*written);
    REQUIRE(back.has_value());
    CHECK(back->lightProbes->added == bundle.lightProbes->added);

    SECTION("one cube short is refused") {
        bundle.lightProbes->added.pop_back();
        CHECK_FALSE(write(bundle).has_value());
    }
    SECTION("cubes with no lamp are refused") {
        bundle.lamps.reset();
        CHECK_FALSE(write(bundle).has_value());
    }
    SECTION("a negative added value is refused") {
        bundle.lightProbes->added[1][0][0] = -1.0f;
        CHECK_FALSE(write(bundle).has_value());
    }
}

TEST_CASE("UTA-0256 INV-3: an SMSK pair may name a lamp light and nothing past it", "[ubundle][lamp]") {
    // GEOM of one triangle, every vertex in chart 0; one pair per light index.
    Bundle bundle = bundleWith(golden());
    bundle.geometry = goldenShape();
    bundle.lights = std::vector<Light>{goldenLight()};
    uta::ubundle::ShadowMask mask;
    mask.texelSize = 8;
    mask.width = 2;
    mask.height = 2;
    mask.texels = {255, 255, 255, 255};
    mask.charts = {uta::ubundle::MaskChart{0, 3, 2, 2}};
    mask.vertexChart = {0, 0, 0};
    mask.vertexTexel = {{0.5f, 0.5f}, {1.5f, 0.5f}, {0.5f, 1.5f}};
    const auto allLit = [](std::uint32_t light) {
        return uta::ubundle::MaskPair{light, uta::ubundle::MASK_ALL_LIT, uta::ubundle::MASK_ALL_LIT, 0, {}};
    };
    for (std::uint32_t light = 0; light < 3; ++light) mask.pairs.push_back(allLit(light));
    bundle.shadowMask = mask;
    // LITE's one light and LAMP's two: indices 0, 1 and 2.
    const auto accepted = write(bundle);
    if (!accepted.has_value()) FAIL("refused: " << accepted.error().message());

    bundle.shadowMask->pairs.push_back(allLit(3));
    bundle.shadowMask->charts[0].pairCount = 4;
    const auto refused = write(bundle);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message().find("names light 3, and LITE holds 1 and LAMP 2") != std::string_view::npos);
}
