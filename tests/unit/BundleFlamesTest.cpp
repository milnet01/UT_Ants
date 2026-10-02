// UTA-0263's container cases: the FLAM section.
//
// docs/specs/UTA-0263-shader-flames.md SS 4.3, INV-2.
//
// THE GOLDEN ARRAY IS AUTHORED FROM SS 4.3, never produced by `write`, for
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
using uta::ubundle::Bundle;
using uta::ubundle::Flame;
using uta::ubundle::FlameLook;
using uta::ubundle::MaterialRecord;
using uta::ubundle::Origin;
using uta::ubundle::read;
using uta::ubundle::write;

namespace {

/// MATS with "a", no flame look, then "b", a flame whose ramp is all zero.
Bytes matsPayload() {
    Bytes out;
    out.u32(2);
    out.str("a");
    out.u8(0); // metallic
    out.u8(0); // parallaxDepth
    out.u8(0); // flame: none
    out.u8(0); // liquid: none -- UTA-0105 SS 4.2
    out.str("b");
    out.u8(0);
    out.u8(0);
    out.u8(1); // flame: a look follows
    for (int i = 0; i < 24; ++i) out.f32(0.0f);
    out.u8(0); // liquid: none
    return out;
}

std::vector<MaterialRecord> materials() {
    return {MaterialRecord{"a", false, 0, std::nullopt}, MaterialRecord{"b", false, 0, FlameLook{}}};
}

void putFlame(Bytes& out, const Flame& flame) {
    out.u32(flame.material);
    for (const float coordinate : flame.base) out.f32(coordinate);
    out.f32(flame.width);
    out.f32(flame.height);
    out.u32(flame.seed);
    out.i32(flame.light);
}

Bytes flamPayload(const std::vector<Flame>& flames) {
    Bytes out;
    out.u32(static_cast<std::uint32_t>(flames.size()));
    for (const Flame& flame : flames) putFlame(out, flame);
    return out;
}

/// A whole .utab holding MATS then FLAM.
std::vector<std::byte> fileWith(const Bytes& flam) {
    const std::vector<std::pair<std::string_view, Bytes>> sections = {{"MATS", matsPayload()}, {"FLAM", flam}};
    Bytes out;
    out.id("UTAB");
    out.u32(19); // formatVersion -- 19 since UTA-0215 gave each ZONE entry its water and tint
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

/// Two flames of material 1, every field different, so a swap shows.
std::vector<Flame> golden() {
    return {Flame{1, {10.5f, -20.25f, 30.0f}, 16.0f, 48.0f, 7, -1},
            Flame{1, {-1.0f, 2.0f, -3.5f}, 8.5f, 24.25f, 1234567, -1}};
}

Bundle bundleWith(std::vector<Flame> flames) {
    Bundle bundle;
    bundle.header.origin = Origin::Authored;
    bundle.materials = materials();
    bundle.flames = std::move(flames);
    return bundle;
}

void sameFlames(const std::vector<Flame>& actual, const std::vector<Flame>& expected) {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CAPTURE(i);
        CHECK(actual[i].material == expected[i].material);
        CHECK(actual[i].base == expected[i].base);
        CHECK(actual[i].width == expected[i].width);
        CHECK(actual[i].height == expected[i].height);
        CHECK(actual[i].seed == expected[i].seed);
        CHECK(actual[i].light == expected[i].light);
    }
}

/// SS 4.3's refusals, one bad record each, as a change to a good one.
const std::vector<std::pair<std::string_view, std::function<void(Flame&)>>> BAD = {
    {"a material past MATS", [](Flame& flame) { flame.material = 2; }},
    {"a material with no flame look", [](Flame& flame) { flame.material = 0; }},
    {"a light below -1", [](Flame& flame) { flame.light = -2; }},
    {"a light past LITE", [](Flame& flame) { flame.light = 0; }},
    {"a zero width", [](Flame& flame) { flame.width = 0.0f; }},
    {"a height that is not finite",
     [](Flame& flame) { flame.height = std::numeric_limits<float>::infinity(); }},
    {"a negative height", [](Flame& flame) { flame.height = -1.0f; }},
    {"a base that is not finite", [](Flame& flame) { flame.base[1] = std::numeric_limits<float>::quiet_NaN(); }},
};

} // namespace

TEST_CASE("INV-2: the FLAM golden bytes decode to the flames they encode", "[ubundle][flames]") {
    const auto result = read(fileWith(flamPayload(golden())));
    REQUIRE(result.has_value());
    REQUIRE(result->flames.has_value());
    sameFlames(*result->flames, golden());
}

TEST_CASE("INV-2: write emits MATS then FLAM as the golden bytes", "[ubundle][flames]") {
    const auto written = write(bundleWith(golden()));
    REQUIRE(written.has_value());
    CHECK(*written == fileWith(flamPayload(golden())));
}

TEST_CASE("INV-2: an empty FLAM stays distinct from an absent one", "[ubundle][flames]") {
    const auto written = write(bundleWith({}));
    REQUIRE(written.has_value());
    const auto back = read(*written);
    REQUIRE(back.has_value());
    REQUIRE(back->flames.has_value());
    CHECK(back->flames->empty());

    Bundle none = bundleWith({});
    none.flames.reset();
    const auto noneBack = read(*write(none));
    REQUIRE(noneBack.has_value());
    CHECK_FALSE(noneBack->flames.has_value());
}

TEST_CASE("INV-2: read and write each refuse every invalid FLAM record", "[ubundle][flames]") {
    for (const auto& [name, spoil] : BAD) {
        INFO(name);
        std::vector<Flame> flames = golden();
        spoil(flames.back()); // the last record, so a check stopping early misses it

        const auto result = read(fileWith(flamPayload(flames)));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::MalformedData);
        CHECK(result.error().message().find("FLAM: record 1") != std::string_view::npos);

        const auto written = write(bundleWith(flames));
        REQUIRE_FALSE(written.has_value());
        CHECK(written.error().code() == ErrorCode::InvalidArgument);
    }
}
