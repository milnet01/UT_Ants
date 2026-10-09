// UTA-0338's container cases: the SUN section, LPRB's sun visibility and the
// sun's SMSK pairs -- INV-3 -- and folding the sun in -- INV-7.
//
// docs/specs/UTA-0338-baked-sun.md SS 4.2.
//
// THE GOLDEN BYTES ARE AUTHORED FROM SS 4.2, never produced by `write`, for
// BundleTextureTest.cpp's reason: a transposition present in both the reader
// and the writer round-trips perfectly.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "ubundle/Bundle.h"

#include "Bytes.h"

#include <catch2/catch_test_macros.hpp>

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
using uta::ubundle::Geometry;
using uta::ubundle::GeometryBatch;
using uta::ubundle::GeometryVertex;
using uta::ubundle::Light;
using uta::ubundle::LightProbe;
using uta::ubundle::LightProbes;
using uta::ubundle::MASK_ALL_LIT;
using uta::ubundle::MaskChart;
using uta::ubundle::MaskPair;
using uta::ubundle::Origin;
using uta::ubundle::ShadowMask;
using uta::ubundle::Sun;
using uta::ubundle::read;
using uta::ubundle::write;

namespace {

/// Every field different, so a swap shows.
Sun goldenSun() { return Sun{.yaw = -8192, .pitch = 9000, .hue = 28, .saturation = 210, .brightness = 180,
                             .levelBrightness = 1.5f}; }

/// SS 4.2's record: yaw, pitch, hue, saturation, brightness, level brightness.
Bytes sunPayload(const Sun& sun) {
    Bytes out;
    out.i32(sun.yaw);
    out.i32(sun.pitch);
    out.u8(sun.hue);
    out.u8(sun.saturation);
    out.u8(sun.brightness);
    out.f32(sun.levelBrightness);
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
        out.u32(0); // compression and reserved
        offset += payload.size();
    }
    for (const auto& section : sections) out.append(section.second);
    return out.data();
}

Bundle bundleWith(const Sun& sun) {
    Bundle bundle;
    bundle.header.origin = Origin::Authored;
    bundle.sun = sun;
    return bundle;
}

Light plainLight(std::uint32_t exportIndex) {
    Light light;
    light.exportIndex = exportIndex;
    light.type = 1;
    light.brightness = 64;
    light.radius = 16;
    return light;
}

/// One triangle in one batch, every vertex in chart 0.
Geometry triangle() {
    Geometry shape;
    shape.vertices = {GeometryVertex{{0, 0, 1}, {0, 0, 1}, 0, 0, 0, {}},
                      GeometryVertex{{4, 0, 1}, {0, 0, 1}, 1, 0, 0, {}},
                      GeometryVertex{{0, 4, 1}, {0, 0, 1}, 0, 1, 0, {}}};
    shape.indices = {0, 1, 2};
    shape.batches = {GeometryBatch{"a", 0x40, 0, 3, {}}};
    return shape;
}

MaskPair allLit(std::uint32_t light, std::uint8_t moverReach = 0) {
    return MaskPair{light, MASK_ALL_LIT, MASK_ALL_LIT, moverReach, {}};
}

/// One chart over the triangle holding `pairs`.
ShadowMask maskOf(std::vector<MaskPair> pairs) {
    ShadowMask mask;
    mask.texelSize = 8;
    mask.width = 2;
    mask.height = 2;
    mask.texels = {255, 255, 255, 255};
    mask.charts = {MaskChart{0, static_cast<std::uint32_t>(pairs.size()), 2, 2}};
    mask.vertexChart = {0, 0, 0};
    mask.vertexTexel = {{0.5f, 0.5f}, {1.5f, 0.5f}, {0.5f, 1.5f}};
    mask.pairs = std::move(pairs);
    return mask;
}

LightProbes twoProbes() {
    LightProbes probes;
    probes.spacing = 32;
    probes.probes = {LightProbe{{0, 0, 0}, {}}, LightProbe{{1, 0, 0}, {}}};
    return probes;
}

/// A lamp with a fitting, so LAMP accepts it.
AddedLamp lamp(std::uint32_t exportIndex) {
    return AddedLamp{plainLight(exportIndex), triangle(), {}};
}

/// SS 4.2's refusals of the record, one each, as a change to a good one.
const std::vector<std::pair<std::string_view, std::function<void(Sun&)>>> BAD = {
    {"a pitch of 0", [](Sun& sun) { sun.pitch = 0; }},
    {"a pitch below the horizon", [](Sun& sun) { sun.pitch = -100; }},
    {"a pitch past straight up", [](Sun& sun) { sun.pitch = 16385; }},
    {"a brightness of 0", [](Sun& sun) { sun.brightness = 0; }},
    {"a level brightness below 0", [](Sun& sun) { sun.levelBrightness = -1; }},
    {"a level brightness not finite",
     [](Sun& sun) { sun.levelBrightness = std::numeric_limits<float>::quiet_NaN(); }},
};

} // namespace

TEST_CASE("UTA-0338 INV-3: the SUN golden bytes decode to the sun they encode", "[ubundle][sun]") {
    const auto result = read(fileWith({{"SUN ", sunPayload(goldenSun())}}));
    if (!result.has_value()) FAIL("refused: " << result.error().message());
    REQUIRE(result->sun.has_value());
    CHECK(result->sun->yaw == -8192);
    CHECK(result->sun->pitch == 9000);
    CHECK(result->sun->hue == 28);
    CHECK(result->sun->saturation == 210);
    CHECK(result->sun->brightness == 180);
    CHECK(result->sun->levelBrightness == 1.5f);
}

TEST_CASE("UTA-0338 INV-3: write emits SUN as the golden bytes", "[ubundle][sun]") {
    const auto written = write(bundleWith(goldenSun()));
    REQUIRE(written.has_value());
    CHECK(*written == fileWith({{"SUN ", sunPayload(goldenSun())}}));
    CHECK(write(Bundle{}).has_value());
}

TEST_CASE("UTA-0338 INV-3: read and write each refuse every invalid sun", "[ubundle][sun]") {
    for (const auto& [name, spoil] : BAD) {
        INFO(name);
        Sun sun = goldenSun();
        spoil(sun);
        const auto result = read(fileWith({{"SUN ", sunPayload(sun)}}));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::MalformedData);
        CHECK(result.error().message().starts_with("SUN: "));
        const auto written = write(bundleWith(sun));
        REQUIRE_FALSE(written.has_value());
        CHECK(written.error().code() == ErrorCode::InvalidArgument);
    }
    SECTION("pitch straight up is accepted") {
        Sun sun = goldenSun();
        sun.pitch = 16384;
        CHECK(write(bundleWith(sun)).has_value());
    }
}

TEST_CASE("UTA-0338 INV-3: LPRB's sun visibility is counted against SUN", "[ubundle][sun]") {
    Bundle bundle = bundleWith(goldenSun());
    bundle.lightProbes = twoProbes();
    bundle.lightProbes->sunSeen = {0.0f, 0.625f};
    const auto written = write(bundle);
    if (!written.has_value()) FAIL("refused: " << written.error().message());
    const auto back = read(*written);
    REQUIRE(back.has_value());
    CHECK(back->lightProbes->sunSeen == bundle.lightProbes->sunSeen);

    SECTION("one short is refused") {
        bundle.lightProbes->sunSeen.pop_back();
        CHECK_FALSE(write(bundle).has_value());
    }
    SECTION("none with a sun is refused") {
        bundle.lightProbes->sunSeen.clear();
        CHECK_FALSE(write(bundle).has_value());
    }
    SECTION("some with no sun is refused") {
        bundle.sun.reset();
        CHECK_FALSE(write(bundle).has_value());
    }
    SECTION("a value above 1 is refused") {
        bundle.lightProbes->sunSeen[1] = 1.0001f;
        CHECK_FALSE(write(bundle).has_value());
    }
    SECTION("a value below 0 is refused") {
        bundle.lightProbes->sunSeen[1] = -0.0001f;
        CHECK_FALSE(write(bundle).has_value());
    }
    SECTION("a value that is not a number is refused") {
        bundle.lightProbes->sunSeen[0] = std::numeric_limits<float>::quiet_NaN();
        CHECK_FALSE(write(bundle).has_value());
    }
    SECTION("a sun-free bundle with no visibility is accepted") {
        bundle.sun.reset();
        bundle.lightProbes->sunSeen.clear();
        CHECK(write(bundle).has_value());
    }
}

TEST_CASE("UTA-0338 INV-3: an SMSK pair may name the sun past the lamps and nothing past it", "[ubundle][sun]") {
    // LITE's one light, LAMP's one lamp and the sun: indices 0, 1 and 2.
    Bundle bundle = bundleWith(goldenSun());
    bundle.materials = std::vector<uta::ubundle::MaterialRecord>{{"a", false, 0, std::nullopt}};
    bundle.geometry = triangle();
    bundle.lights = std::vector<Light>{plainLight(3)};
    bundle.lamps = std::vector<AddedLamp>{lamp(5)};
    bundle.shadowMask = maskOf({allLit(0, 1), allLit(1, 1), allLit(2)});
    const auto accepted = write(bundle);
    if (!accepted.has_value()) FAIL("refused: " << accepted.error().message());
    const auto back = read(*accepted);
    if (!back.has_value()) FAIL("read refused: " << back.error().message());

    SECTION("a sun pair with moverReach is refused") {
        bundle.shadowMask->pairs[2].moverReach = 1;
        const auto refused = write(bundle);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().message().find("sun's and has moverReach") != std::string_view::npos);
    }
    SECTION("a pair past the sun is refused") {
        bundle.shadowMask = maskOf({allLit(0), allLit(1), allLit(2), allLit(3)});
        const auto refused = write(bundle);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().message().find("names light 3, and LITE holds 1 and LAMP 1 and a sun")
              != std::string_view::npos);
    }
    SECTION("with no sun the sun's index is refused") {
        bundle.sun.reset();
        CHECK_FALSE(write(bundle).has_value());
    }
}

TEST_CASE("UTA-0338 INV-7: lightOfSun shines down from where the sun stands", "[ubundle][sun]") {
    const Light light = uta::ubundle::lightOfSun(goldenSun());
    CHECK(light.effect == uta::ubundle::SUN_EFFECT);
    CHECK(light.type == 1);
    CHECK(light.rotation == std::array<std::int32_t, 3>{-9000, -8192 + 32768, 0});
    CHECK(light.brightness == 180);
    CHECK(light.hue == 28);
    CHECK(light.saturation == 210);
    CHECK(light.levelBrightness == 1.5f);
    CHECK(uta::ubundle::litDirectly(light));
}

TEST_CASE("UTA-0338 INV-7: after the lamps either way the sun is LITE's last light", "[ubundle][sun]") {
    const auto folded = [](bool withLamps, bool on) {
        Bundle bundle = bundleWith(goldenSun());
        bundle.geometry = triangle();
        bundle.lights = std::vector<Light>{plainLight(3), plainLight(4)};
        if (withLamps) {
            bundle.lamps = std::vector<AddedLamp>{lamp(5), lamp(6)};
            bundle.shadowMask = maskOf({allLit(0), allLit(2), allLit(3), allLit(4)});
        } else {
            bundle.shadowMask = maskOf({allLit(1), allLit(2)});
        }
        uta::ubundle::applyAddedLamps(bundle, on);
        uta::ubundle::applySun(bundle);
        return bundle;
    };
    for (const bool withLamps : {false, true})
        for (const bool on : {false, true}) {
            INFO("lamps " << withLamps << " on " << on);
            const Bundle bundle = folded(withLamps, on);
            CHECK_FALSE(bundle.sun.has_value());
            REQUIRE(bundle.lights.has_value());
            const auto last = static_cast<std::uint32_t>(bundle.lights->size() - 1);
            CHECK(bundle.lights->back().effect == uta::ubundle::SUN_EFFECT);
            CHECK(bundle.lights->size() == 3 + (withLamps && on ? 2 : 0));
            // The sun's pair is the chart's last, and it names the sun.
            REQUIRE_FALSE(bundle.shadowMask->pairs.empty());
            CHECK(bundle.shadowMask->pairs.back().light == last);
            std::size_t sunPairs = 0;
            for (const MaskPair& pair : bundle.shadowMask->pairs) sunPairs += pair.light == last ? 1 : 0;
            CHECK(sunPairs == 1);
            CHECK(bundle.shadowMask->charts[0].pairCount == bundle.shadowMask->pairs.size());
            // The other pairs: LITE's, and the lamps' only while they are on.
            CHECK(bundle.shadowMask->pairs.size() == (withLamps ? (on ? 4u : 2u) : 2u));
        }
}

TEST_CASE("UTA-0338 INV-7: applySun leaves a sun-free bundle alone", "[ubundle][sun]") {
    Bundle bundle;
    bundle.lights = std::vector<Light>{plainLight(3)};
    bundle.shadowMask = maskOf({allLit(0)});
    bundle.geometry = triangle();
    const auto before = write(bundle);
    REQUIRE(before.has_value());
    uta::ubundle::applySun(bundle);
    const auto after = write(bundle);
    REQUIRE(after.has_value());
    CHECK(*after == *before);
    Bundle empty;
    uta::ubundle::applySun(empty);
    CHECK_FALSE(empty.lights.has_value());
}
