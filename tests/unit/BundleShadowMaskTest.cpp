// UTA-0326's container cases: the SMSK section.
//
// docs/specs/UTA-0326-baked-shadow-mask.md SS 4.2 and INV-1.
//
// THE GOLDEN BYTES ARE AUTHORED FROM SS 4.2, never produced by `write`, for
// BundleTextureTest.cpp's reason: a transposition present in both the reader
// and the writer round-trips perfectly.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "ubundle/Bundle.h"

#include "Bytes.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using uta::ErrorCode;
using uta::testing::Bytes;
using uta::ubundle::Bundle;
using uta::ubundle::Geometry;
using uta::ubundle::GeometryVertex;
using uta::ubundle::Light;
using uta::ubundle::MASK_ALL_LIT;
using uta::ubundle::MASK_NO_CHART;
using uta::ubundle::Origin;
using uta::ubundle::ShadowMask;
using uta::ubundle::read;
using uta::ubundle::write;

namespace {

/// A GEOM payload of `vertices` zeroed vertices and no triangle.
Bytes geomPayload(std::uint32_t vertices) {
    Bytes out;
    out.u32(vertices);
    for (std::uint32_t i = 0; i < vertices; ++i) {
        for (int k = 0; k < 8; ++k) out.f32(0); // position, normal, u, v
        out.u8(0);                             // zone
    }
    out.u32(0); // indices
    out.u32(0); // batches
    return out;
}

/// A LITE payload of `count` zeroed lights, export indices 0 upward.
Bytes litePayload(std::uint32_t count) {
    Bytes out;
    out.u32(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        out.u32(i);                              // exportIndex
        for (int k = 0; k < 3; ++k) out.f32(0);  // location
        for (int k = 0; k < 3; ++k) out.i32(0);  // rotation
        for (int k = 0; k < 12; ++k) out.u8(0);  // type to volumeFog
        for (int k = 0; k < 4; ++k) out.u8(0);   // the four bools
        out.u8(0);                               // strip
        for (int k = 0; k < 6; ++k) out.f32(0);  // stripFrom, stripTo
        out.f32(1);                              // levelBrightness
    }
    return out;
}

/// An SMSK payload in SS 4.2's order.
Bytes smskPayload(const ShadowMask& mask) {
    Bytes out;
    out.f32(mask.texelSize);
    out.u32(mask.width);
    out.u32(mask.height);
    out.u32(static_cast<std::uint32_t>(mask.vertexChart.size()));
    for (const std::uint32_t chart : mask.vertexChart) out.u32(chart);
    out.u32(static_cast<std::uint32_t>(mask.vertexTexel.size()));
    for (const auto& texel : mask.vertexTexel) {
        out.f32(texel[0]);
        out.f32(texel[1]);
    }
    out.u32(static_cast<std::uint32_t>(mask.charts.size()));
    for (const auto& chart : mask.charts) {
        out.u32(chart.firstPair);
        out.u32(chart.pairCount);
        out.u16(chart.width);
        out.u16(chart.height);
    }
    out.u32(static_cast<std::uint32_t>(mask.pairs.size()));
    for (const auto& pair : mask.pairs) {
        out.u32(pair.light);
        out.u16(pair.x);
        out.u16(pair.y);
        out.u8(pair.moverReach);
        for (const std::uint8_t part : pair.reserved) out.u8(part);
    }
    out.u32(static_cast<std::uint32_t>(mask.texels.size()));
    for (const std::uint8_t texel : mask.texels) out.u8(texel);
    return out;
}

/// A whole .utab holding `sections` in the order given.
std::vector<std::byte> fileWith(const std::vector<std::pair<std::string_view, Bytes>>& sections) {
    Bytes out;
    out.id("UTAB");
    out.u32(23); // formatVersion -- 23 since UTA-0326 added SMSK
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

/// A 4 by 3 atlas over three vertices and two lights, every value distinct,
/// so a transposition shows. Chart 0 holds two pairs, the second marked;
/// chart 1 one all-lit pair; vertex 1 is unlit.
ShadowMask golden() {
    ShadowMask mask;
    mask.texelSize = 8;
    mask.width = 4;
    mask.height = 3;
    mask.vertexChart = {0, MASK_NO_CHART, 1};
    mask.vertexTexel = {{1.5f, 2.25f}, {0, 0}, {0.5f, 1.75f}};
    mask.charts = {{0, 2, 2, 3}, {2, 1, 2, 2}};
    mask.pairs = {{0, 0, 0, 0, {}}, {1, 2, 0, 1, {}}, {0, MASK_ALL_LIT, MASK_ALL_LIT, 0, {}}};
    mask.texels = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 255};
    return mask;
}

Bundle bundleWith(const ShadowMask& mask, std::uint32_t vertices = 3, std::uint32_t lights = 2) {
    Bundle bundle;
    bundle.header.origin = Origin::Authored;
    bundle.geometry = Geometry{};
    bundle.geometry->vertices.resize(vertices, GeometryVertex{});
    bundle.lights = std::vector<Light>(lights);
    for (std::uint32_t i = 0; i < lights; ++i) (*bundle.lights)[i].exportIndex = i;
    bundle.shadowMask = mask;
    return bundle;
}

/// `read` refuses a file holding GEOM of `vertices`, LITE of two lights and
/// this SMSK with MalformedData, and `write` refuses the same bundle with
/// InvalidArgument, both naming `says`.
void refusedBothWays(const ShadowMask& mask, std::string_view says, std::uint32_t vertices = 3) {
    const auto result = read(
        fileWith({{"GEOM", geomPayload(vertices)}, {"LITE", litePayload(2)}, {"SMSK", smskPayload(mask)}}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::MalformedData);
    CHECK_THAT(std::string(result.error().message()), Catch::Matchers::ContainsSubstring(std::string(says)));

    const auto written = write(bundleWith(mask, vertices));
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code() == ErrorCode::InvalidArgument);
    CHECK_THAT(std::string(written.error().message()), Catch::Matchers::ContainsSubstring(std::string(says)));
}

} // namespace

TEST_CASE("INV-1: the SMSK golden bytes decode to the mask they encode", "[ubundle][shadowmask]") {
    const auto result =
        read(fileWith({{"GEOM", geomPayload(3)}, {"LITE", litePayload(2)}, {"SMSK", smskPayload(golden())}}));
    REQUIRE(result.has_value());
    REQUIRE(result->shadowMask.has_value());
    const ShadowMask& mask = *result->shadowMask;
    const ShadowMask expected = golden();
    CHECK(mask.texelSize == 8);
    CHECK(mask.width == 4);
    CHECK(mask.height == 3);
    CHECK(mask.vertexChart == expected.vertexChart);
    CHECK(mask.vertexTexel == expected.vertexTexel);
    REQUIRE(mask.charts.size() == 2);
    CHECK(mask.charts[1].firstPair == 2);
    CHECK(mask.charts[1].pairCount == 1);
    CHECK(mask.charts[0].width == 2);
    CHECK(mask.charts[0].height == 3);
    REQUIRE(mask.pairs.size() == 3);
    CHECK(mask.pairs[1].light == 1);
    CHECK(mask.pairs[1].x == 2);
    CHECK(mask.pairs[1].y == 0);
    CHECK(mask.pairs[1].moverReach == 1);
    CHECK(mask.pairs[2].x == MASK_ALL_LIT);
    CHECK(mask.pairs[2].y == MASK_ALL_LIT);
    CHECK(mask.texels == expected.texels);
}

TEST_CASE("INV-1: write emits SMSK after LITE as the golden bytes", "[ubundle][shadowmask]") {
    const auto written = write(bundleWith(golden()));
    REQUIRE(written.has_value());
    CHECK(*written == fileWith({{"GEOM", geomPayload(3)}, {"LITE", litePayload(2)}, {"SMSK", smskPayload(golden())}}));
}

TEST_CASE("INV-1: an SMSK whose texel size is not finite and positive is refused", "[ubundle][shadowmask]") {
    for (const float size : {0.0f, -8.0f, std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::quiet_NaN()}) {
        CAPTURE(size);
        ShadowMask mask = golden();
        mask.texelSize = size;
        refusedBothWays(mask, "SMSK: a texel size of");
    }
}

TEST_CASE("INV-1: an SMSK side of 0 or past the limit is refused", "[ubundle][shadowmask]") {
    ShadowMask zero = golden();
    zero.height = 0;
    zero.texels.clear();
    refusedBothWays(zero, "SMSK: an atlas side of 0");

    ShadowMask wide = golden();
    wide.width = uta::ubundle::SHADOW_MASK_ATLAS_LIMIT + 1;
    wide.texels.assign(std::size_t{wide.width} * wide.height, 255);
    refusedBothWays(wide, "SMSK: an atlas side of 4097");
}

TEST_CASE("INV-1: SMSK texels that do not fill the atlas are refused", "[ubundle][shadowmask]") {
    ShadowMask mask = golden();
    mask.texels.pop_back();
    refusedBothWays(mask, "SMSK: 11 texels for a 4 by 3 atlas");
}

TEST_CASE("INV-1: SMSK vertex arrays unlike GEOM's vertex count are refused", "[ubundle][shadowmask]") {
    refusedBothWays(golden(), "SMSK holds 3 vertex charts and 3 vertex texels, and GEOM 4 vertices", 4);

    ShadowMask texels = golden();
    texels.vertexTexel.pop_back();
    refusedBothWays(texels, "SMSK: 3 vertex charts and 2 vertex texels");
}

TEST_CASE("INV-1: an SMSK with no GEOM or no LITE is refused", "[ubundle][shadowmask]") {
    const auto noGeom = read(fileWith({{"LITE", litePayload(2)}, {"SMSK", smskPayload(golden())}}));
    REQUIRE_FALSE(noGeom.has_value());
    CHECK(noGeom.error().code() == ErrorCode::MalformedData);
    CHECK_THAT(std::string(noGeom.error().message()), Catch::Matchers::ContainsSubstring("no GEOM"));
    Bundle withoutGeom = bundleWith(golden());
    withoutGeom.geometry.reset();
    const auto writtenNoGeom = write(withoutGeom);
    REQUIRE_FALSE(writtenNoGeom.has_value());
    CHECK(writtenNoGeom.error().code() == ErrorCode::InvalidArgument);
    CHECK_THAT(std::string(writtenNoGeom.error().message()), Catch::Matchers::ContainsSubstring("no GEOM"));

    const auto noLite = read(fileWith({{"GEOM", geomPayload(3)}, {"SMSK", smskPayload(golden())}}));
    REQUIRE_FALSE(noLite.has_value());
    CHECK(noLite.error().code() == ErrorCode::MalformedData);
    CHECK_THAT(std::string(noLite.error().message()), Catch::Matchers::ContainsSubstring("no LITE"));
    Bundle withoutLite = bundleWith(golden());
    withoutLite.lights.reset();
    const auto writtenNoLite = write(withoutLite);
    REQUIRE_FALSE(writtenNoLite.has_value());
    CHECK(writtenNoLite.error().code() == ErrorCode::InvalidArgument);
    CHECK_THAT(std::string(writtenNoLite.error().message()), Catch::Matchers::ContainsSubstring("no LITE"));
}

TEST_CASE("INV-1: a vertex naming no chart SMSK holds is refused", "[ubundle][shadowmask]") {
    ShadowMask mask = golden();
    mask.vertexChart[0] = 2;
    refusedBothWays(mask, "SMSK: vertex 0 names chart 2, and SMSK holds 2");
}

TEST_CASE("INV-1: a vertex texel outside its chart's rectangle is refused", "[ubundle][shadowmask]") {
    // Chart 0 is 2 by 3, so 2 and 3 are its far edges and in.
    for (const auto texel : {std::array<float, 2>{2.01f, 1}, std::array<float, 2>{-0.01f, 1},
                             std::array<float, 2>{1, 3.01f}, std::array<float, 2>{std::numeric_limits<float>::quiet_NaN(), 1}}) {
        CAPTURE(texel[0], texel[1]);
        ShadowMask mask = golden();
        mask.vertexTexel[0] = texel;
        refusedBothWays(mask, "SMSK: vertex 0 has a texel outside its chart's rectangle");
    }
    ShadowMask edge = golden();
    edge.vertexTexel[0] = {2, 3};
    CHECK(write(bundleWith(edge)).has_value());

    ShadowMask unlit = golden();
    unlit.vertexTexel[1] = {std::numeric_limits<float>::infinity(), 0};
    refusedBothWays(unlit, "SMSK: vertex 1 has a texel that is not finite");
}

TEST_CASE("INV-1: a chart's pair run past the pairs or over another's is refused", "[ubundle][shadowmask]") {
    ShadowMask past = golden();
    past.charts[1].pairCount = 2;
    refusedBothWays(past, "SMSK: chart 1's pair run passes the end of the pairs");

    ShadowMask overlap = golden();
    overlap.charts[1].firstPair = 1;
    refusedBothWays(overlap, "SMSK: chart 1's pair run overlaps another chart's");
}

TEST_CASE("INV-1: a pair naming no light LITE holds is refused", "[ubundle][shadowmask]") {
    ShadowMask mask = golden();
    mask.pairs[1].light = 2;
    refusedBothWays(mask, "SMSK: pair 1 names light 2, and LITE holds 2");
}

TEST_CASE("INV-1: a chart's pairs not strictly ascending by light are refused", "[ubundle][shadowmask]") {
    ShadowMask mask = golden();
    mask.pairs[1].light = 0;
    refusedBothWays(mask, "SMSK: chart 0's pairs are not strictly ascending by light");
}

TEST_CASE("INV-1: a pair whose rectangle passes the atlas is refused", "[ubundle][shadowmask]") {
    ShadowMask right = golden();
    right.pairs[1].x = 3;
    refusedBothWays(right, "SMSK: chart 0's pair 1 has a rectangle passing the atlas");

    ShadowMask down = golden();
    down.pairs[0].y = 1;
    refusedBothWays(down, "SMSK: chart 0's pair 0 has a rectangle passing the atlas");

    // MASK_ALL_LIT in x alone is no corner the atlas holds.
    ShadowMask half = golden();
    half.pairs[0].x = MASK_ALL_LIT;
    refusedBothWays(half, "SMSK: chart 0's pair 0 has a rectangle passing the atlas");
}

TEST_CASE("INV-1: a mover byte above 1 or a non-zero reserved byte is refused", "[ubundle][shadowmask]") {
    ShadowMask mover = golden();
    mover.pairs[0].moverReach = 2;
    refusedBothWays(mover, "SMSK: pair 0's mover byte 2 is not 0 or 1");

    for (std::size_t k = 0; k < 3; ++k) {
        CAPTURE(k);
        ShadowMask reserved = golden();
        reserved.pairs[2].reserved[k] = 1;
        refusedBothWays(reserved, "SMSK: pair 2's reserved bytes are not zero");
    }
}
