// UTA-0164's container cases: the AOCC section.
//
// docs/specs/UTA-0164-ambient-occlusion.md SS 4.1 and INV-1.
//
// THE GOLDEN BYTES ARE AUTHORED FROM SS 4.1, never produced by `write`, for
// BundleTextureTest.cpp's reason: a transposition present in both the reader
// and the writer round-trips perfectly.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "ubundle/Bundle.h"

#include "Bytes.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

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
using uta::ubundle::Occlusion;
using uta::ubundle::Origin;
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

/// An AOCC payload in SS 4.1's order.
Bytes aoccPayload(const Occlusion& occlusion) {
    Bytes out;
    out.f32(occlusion.texelSize);
    out.u32(occlusion.width);
    out.u32(occlusion.height);
    out.u32(static_cast<std::uint32_t>(occlusion.uv.size()));
    for (const auto& uv : occlusion.uv) {
        out.f32(uv[0]);
        out.f32(uv[1]);
    }
    out.u32(static_cast<std::uint32_t>(occlusion.texels.size()));
    for (const std::uint8_t texel : occlusion.texels) out.u8(texel);
    return out;
}

/// A whole .utab holding `sections` in the order given.
std::vector<std::byte> fileWith(const std::vector<std::pair<std::string_view, Bytes>>& sections) {
    Bytes out;
    out.id("UTAB");
    out.u32(14); // formatVersion -- 14 since UTA-0164 SS 4.1
    out.u8(1);  // origin: Authored
    out.u8(0);  // kind: Map
    out.u16(0); // reserved
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

/// A 2 by 3 atlas and two uvs, every value distinct, so a transposition shows.
Occlusion golden() {
    Occlusion occlusion;
    occlusion.texelSize = 16;
    occlusion.width = 2;
    occlusion.height = 3;
    occlusion.uv = {{0.25f, 0.5f}, {0.75f, 1.0f}};
    occlusion.texels = {10, 20, 30, 40, 50, 255};
    return occlusion;
}

Geometry twoVertices() {
    Geometry geometry;
    geometry.vertices.resize(2, GeometryVertex{});
    return geometry;
}

/// `read` refuses a file holding GEOM of `vertices` and this AOCC with
/// MalformedData, and `write` refuses the same bundle with InvalidArgument,
/// both naming `says`.
void refusedBothWays(const Occlusion& occlusion, std::string_view says, std::uint32_t vertices = 2) {
    const auto result = read(fileWith({{"GEOM", geomPayload(vertices)}, {"AOCC", aoccPayload(occlusion)}}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::MalformedData);
    CHECK_THAT(std::string(result.error().message()), Catch::Matchers::ContainsSubstring(std::string(says)));

    Bundle bundle;
    bundle.geometry = Geometry{};
    bundle.geometry->vertices.resize(vertices, GeometryVertex{});
    bundle.occlusion = occlusion;
    const auto written = write(bundle);
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code() == ErrorCode::InvalidArgument);
    CHECK_THAT(std::string(written.error().message()), Catch::Matchers::ContainsSubstring(std::string(says)));
}

} // namespace

TEST_CASE("INV-1: the AOCC golden bytes decode to the atlas they encode", "[ubundle][occlusion]") {
    const auto result = read(fileWith({{"GEOM", geomPayload(2)}, {"AOCC", aoccPayload(golden())}}));
    REQUIRE(result.has_value());
    REQUIRE(result->occlusion.has_value());
    const Occlusion& occlusion = *result->occlusion;
    CHECK(occlusion.texelSize == 16);
    CHECK(occlusion.width == 2);
    CHECK(occlusion.height == 3);
    CHECK(occlusion.uv == golden().uv);
    CHECK(occlusion.texels == golden().texels);
}

TEST_CASE("INV-1: write emits AOCC after GEOM as the golden bytes", "[ubundle][occlusion]") {
    Bundle bundle;
    bundle.header.origin = Origin::Authored;
    bundle.geometry = twoVertices();
    bundle.occlusion = golden();
    const auto written = write(bundle);
    REQUIRE(written.has_value());
    CHECK(*written == fileWith({{"GEOM", geomPayload(2)}, {"AOCC", aoccPayload(golden())}}));
}

TEST_CASE("INV-1: an AOCC whose texel size is not finite and positive is refused", "[ubundle][occlusion]") {
    for (const float size : {0.0f, -16.0f, std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::quiet_NaN()}) {
        CAPTURE(size);
        Occlusion occlusion = golden();
        occlusion.texelSize = size;
        refusedBothWays(occlusion, "AOCC: a texel size of");
    }
}

TEST_CASE("INV-1: an AOCC side of 0 or past the limit is refused", "[ubundle][occlusion]") {
    Occlusion zero = golden();
    zero.width = 0;
    zero.texels.clear();
    refusedBothWays(zero, "AOCC: an atlas side of 0");

    Occlusion wide = golden();
    wide.width = uta::ubundle::OCCLUSION_ATLAS_LIMIT + 1;
    wide.height = 1;
    wide.texels.assign(wide.width, 255);
    refusedBothWays(wide, "AOCC: an atlas side of 4097");
}

TEST_CASE("INV-1: AOCC texels that do not fill the atlas are refused", "[ubundle][occlusion]") {
    Occlusion occlusion = golden();
    occlusion.texels.pop_back();
    refusedBothWays(occlusion, "AOCC: 5 texels for a 2 by 3 atlas");
}

TEST_CASE("INV-1: an AOCC uv outside 0 to 1 is refused", "[ubundle][occlusion]") {
    for (const float c : {-0.01f, 1.01f, std::numeric_limits<float>::quiet_NaN()}) {
        CAPTURE(c);
        Occlusion occlusion = golden();
        occlusion.uv[1][1] = c;
        refusedBothWays(occlusion, "AOCC: vertex 1 has a uv component of");
    }
}

TEST_CASE("INV-1: an AOCC uv count unlike GEOM's vertex count is refused", "[ubundle][occlusion]") {
    refusedBothWays(golden(), "AOCC holds 2 uvs, and GEOM 3 vertices", 3);
}

TEST_CASE("INV-1: an AOCC with no GEOM is refused", "[ubundle][occlusion]") {
    const auto result = read(fileWith({{"AOCC", aoccPayload(golden())}}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::MalformedData);
    CHECK_THAT(std::string(result.error().message()), Catch::Matchers::ContainsSubstring("no GEOM"));

    Bundle bundle;
    bundle.occlusion = golden();
    const auto written = write(bundle);
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code() == ErrorCode::InvalidArgument);
}
