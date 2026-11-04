// UTA-0293's container cases: the VEGE section.
//
// docs/specs/UTA-0293-grass.md SS 4.3, INV-7.
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
#include <string_view>
#include <utility>
#include <vector>

using uta::ErrorCode;
using uta::testing::Bytes;
using uta::ubundle::Bundle;
using uta::ubundle::Grass;
using uta::ubundle::GrassCell;
using uta::ubundle::GrassClump;
using uta::ubundle::MASK_NO_CHART;
using uta::ubundle::MaskChart;
using uta::ubundle::MaterialRecord;
using uta::ubundle::Origin;
using uta::ubundle::read;
using uta::ubundle::ShadowMask;
using uta::ubundle::write;

namespace {

/// MATS with one material, "a", and nothing else.
Bytes matsPayload() {
    Bytes out;
    out.u32(1);
    out.str("a");
    out.u8(0); // metallic
    out.u8(0); // parallaxDepth
    out.u8(0); // flame: none
    out.u8(0); // liquid: none
    out.u8(0); // fire: none
    out.u8(0); // tile kind: Fixed
    for (int i = 0; i < 32; ++i) out.u8(0); // its picture hash: none
    out.u8(0); // pane: no
    return out;
}

/// Two clumps with no chart, every field different so a swap shows, and one
/// cell holding both.
Grass golden() {
    Grass grass;
    grass.clumps = {GrassClump{{10.5f, -20.25f, 30.0f}, 0, {0.25f, 0.75f}, MASK_NO_CHART, {1.5f, 2.5f}, 7},
                    GrassClump{{-1.0f, 2.0f, -3.5f}, 0, {4.0f, -8.0f}, MASK_NO_CHART, {0.0f, 3.0f}, 1234567}};
    grass.cells = {GrassCell{0, 2, {-1.0f, -20.25f, -3.5f}, {10.5f, 2.0f, 50.0f}}};
    return grass;
}

void putClump(Bytes& out, const GrassClump& clump) {
    for (const float c : clump.base) out.f32(c);
    out.u32(clump.material);
    for (const float c : clump.uv) out.f32(c);
    out.u32(clump.maskChart);
    for (const float c : clump.maskTexel) out.f32(c);
    out.u32(clump.seed);
}

/// SS 4.3's layout: the clumps, then the cells, each a u32 count first.
Bytes vegePayload(const Grass& grass) {
    Bytes out;
    out.u32(static_cast<std::uint32_t>(grass.clumps.size()));
    for (const GrassClump& clump : grass.clumps) putClump(out, clump);
    out.u32(static_cast<std::uint32_t>(grass.cells.size()));
    for (const GrassCell& cell : grass.cells) {
        out.u32(cell.firstClump);
        out.u32(cell.clumpCount);
        for (const float c : cell.low) out.f32(c);
        for (const float c : cell.high) out.f32(c);
    }
    return out;
}

/// A whole .utab holding MATS then VEGE.
std::vector<std::byte> fileWith(const Bytes& vege) {
    const std::vector<std::pair<std::string_view, Bytes>> sections = {{"MATS", matsPayload()}, {"VEGE", vege}};
    Bytes out;
    out.id("UTAB");
    out.u32(35); // formatVersion -- 35 since UTA-0293 added VEGE
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

Bundle bundleWith(Grass grass) {
    Bundle bundle;
    bundle.header.origin = Origin::Authored;
    bundle.materials = std::vector<MaterialRecord>{MaterialRecord{"a", false, 0, std::nullopt}};
    bundle.grass = std::move(grass);
    return bundle;
}

void sameGrass(const Grass& actual, const Grass& expected) {
    REQUIRE(actual.clumps.size() == expected.clumps.size());
    for (std::size_t i = 0; i < expected.clumps.size(); ++i) {
        CAPTURE(i);
        CHECK(actual.clumps[i].base == expected.clumps[i].base);
        CHECK(actual.clumps[i].material == expected.clumps[i].material);
        CHECK(actual.clumps[i].uv == expected.clumps[i].uv);
        CHECK(actual.clumps[i].maskChart == expected.clumps[i].maskChart);
        CHECK(actual.clumps[i].maskTexel == expected.clumps[i].maskTexel);
        CHECK(actual.clumps[i].seed == expected.clumps[i].seed);
    }
    REQUIRE(actual.cells.size() == expected.cells.size());
    for (std::size_t i = 0; i < expected.cells.size(); ++i) {
        CAPTURE(i);
        CHECK(actual.cells[i].firstClump == expected.cells[i].firstClump);
        CHECK(actual.cells[i].clumpCount == expected.cells[i].clumpCount);
        CHECK(actual.cells[i].low == expected.cells[i].low);
        CHECK(actual.cells[i].high == expected.cells[i].high);
    }
}

/// INV-7's refusals, one each, as a change to the golden grass. The bundle
/// has no SMSK, so any chart but MASK_NO_CHART is past its end.
const std::vector<std::pair<std::string_view, std::function<void(Grass&)>>> BAD = {
    {"a material past MATS", [](Grass& grass) { grass.clumps[1].material = 1; }},
    {"a chart past SMSK", [](Grass& grass) { grass.clumps[0].maskChart = 0; }},
    {"a base that is not finite",
     [](Grass& grass) { grass.clumps[1].base[2] = std::numeric_limits<float>::quiet_NaN(); }},
    {"a cell whose run passes the clumps", [](Grass& grass) { grass.cells[0].firstClump = 1; }},
    {"a cell whose run wraps a u32", [](Grass& grass) { grass.cells[0].firstClump = 0xFFFFFFFF; }},
};

} // namespace

TEST_CASE("UTA-0293 INV-7: the VEGE golden bytes decode to the grass they encode", "[ubundle][grass]") {
    const auto result = read(fileWith(vegePayload(golden())));
    REQUIRE(result.has_value());
    REQUIRE(result->grass.has_value());
    sameGrass(*result->grass, golden());
}

TEST_CASE("UTA-0293 INV-7: write emits MATS then VEGE as the golden bytes", "[ubundle][grass]") {
    const auto written = write(bundleWith(golden()));
    REQUIRE(written.has_value());
    CHECK(*written == fileWith(vegePayload(golden())));
}

TEST_CASE("UTA-0293 INV-7: a clump may name a chart SMSK holds", "[ubundle][grass]") {
    // The smallest SMSK the cross-section rules accept: no GEOM vertices, no
    // lights, one chart.
    Bundle bundle = bundleWith(golden());
    bundle.geometry.emplace();
    bundle.lights.emplace();
    ShadowMask mask;
    mask.texelSize = 1.0f;
    mask.width = 1;
    mask.height = 1;
    mask.charts = {MaskChart{0, 0, 1, 1}};
    mask.texels = {255};
    bundle.shadowMask = std::move(mask);
    bundle.grass->clumps[0].maskChart = 0;

    const auto written = write(bundle);
    REQUIRE(written.has_value());
    const auto back = read(*written);
    REQUIRE(back.has_value());
    sameGrass(*back->grass, *bundle.grass);

    bundle.grass->clumps[0].maskChart = 1;
    const auto refused = write(bundle);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE("UTA-0293 INV-7: read and write each refuse every invalid VEGE", "[ubundle][grass]") {
    for (const auto& [name, spoil] : BAD) {
        CAPTURE(name);
        Grass grass = golden();
        spoil(grass);
        const auto result = read(fileWith(vegePayload(grass)));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::MalformedData);
        const auto written = write(bundleWith(grass));
        REQUIRE_FALSE(written.has_value());
        CHECK(written.error().code() == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("UTA-0293 INV-7: an empty VEGE stays distinct from an absent one", "[ubundle][grass]") {
    const auto written = write(bundleWith({}));
    REQUIRE(written.has_value());
    const auto back = read(*written);
    REQUIRE(back.has_value());
    REQUIRE(back->grass.has_value());
    CHECK(back->grass->clumps.empty());

    Bundle none = bundleWith({});
    none.grass.reset();
    const auto noneBack = read(*write(none));
    REQUIRE(noneBack.has_value());
    CHECK_FALSE(noneBack->grass.has_value());
}
