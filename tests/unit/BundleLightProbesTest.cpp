// UTA-0112's container cases: the LPRB section.
//
// docs/specs/UTA-0112-baked-light-probes.md SS 4.2, INV-1. The baker's cases
// are tests/unit/BakeLightProbesTest.cpp; nothing here reaches ubake.
//
// THE PAYLOADS ARE AUTHORED FROM SS 4.2, field by field, never produced by
// `write` -- an encoder and a decoder sharing a wrong width round-trip every
// value, so each is graded against the same authored bytes instead.
//
// EACH REFUSAL BREAKS ONE RULE of golden(), which every rule accepts.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "ubundle/Bundle.h"

#include "Bytes.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

using uta::ErrorCode;
using uta::testing::Bytes;
using uta::ubundle::Bundle;
using uta::ubundle::Collision;
using uta::ubundle::LightProbe;
using uta::ubundle::LightProbes;
using uta::ubundle::Origin;
using uta::ubundle::read;
using uta::ubundle::write;

namespace {

/// An LPRB payload in SS 4.2's order: the spacing, then the probes, each its
/// cell's three i32 and then its cube face by face, red, green and blue.
Bytes lprbPayload(const LightProbes& probes) {
    Bytes out;
    out.u32(probes.spacing);
    out.u32(static_cast<std::uint32_t>(probes.probes.size()));
    for (const LightProbe& probe : probes.probes) {
        for (const std::int32_t part : probe.cell) out.i32(part);
        for (const auto& face : probe.cube)
            for (const float channel : face) out.f32(channel);
    }
    return out;
}

/// A COLL payload holding an empty level tree and no mover: four empty
/// vectors, the outside byte, and an empty mover vector.
Bytes emptyColl() {
    Bytes out;
    for (int vector = 0; vector < 4; ++vector) out.u32(0);
    out.u8(0);
    out.u32(0);
    return out;
}

/// A whole .utab holding `sections` in the order given.
std::vector<std::byte> fileWith(const std::vector<std::pair<std::string_view, Bytes>>& sections) {
    Bytes out;
    out.id("UTAB");
    out.u32(8); // formatVersion -- 8 since UTA-0112 SS 4.2
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

/// Three probes, strictly ascending by z, then y, then x, with a cell that is
/// negative on each axis somewhere. Every value distinct but one -0.0, which
/// is not below zero and so is kept, bit for bit.
LightProbes golden() {
    LightProbes probes;
    probes.spacing = 128;
    float next = 0.125F;
    for (const auto cell : {std::array<std::int32_t, 3>{5, 0, -2},
                            std::array<std::int32_t, 3>{-3, 1, -2},
                            std::array<std::int32_t, 3>{0, -1, 1}}) {
        LightProbe probe;
        probe.cell = cell;
        for (auto& face : probe.cube)
            for (float& channel : face) {
                channel = next;
                next += 0.0625F;
            }
        probes.probes.push_back(probe);
    }
    probes.probes[1].cube[3][1] = -0.0F;
    return probes;
}

void sameProbes(const LightProbes& actual, const LightProbes& expected) {
    CHECK(actual.spacing == expected.spacing);
    REQUIRE(actual.probes.size() == expected.probes.size());
    for (std::size_t p = 0; p < expected.probes.size(); ++p) {
        CHECK(actual.probes[p].cell == expected.probes[p].cell);
        for (std::size_t face = 0; face < 6; ++face)
            for (std::size_t channel = 0; channel < 3; ++channel)
                CHECK(std::bit_cast<std::uint32_t>(actual.probes[p].cube[face][channel])
                      == std::bit_cast<std::uint32_t>(expected.probes[p].cube[face][channel]));
    }
}

/// `read` refuses it with MalformedData, and `write` with InvalidArgument --
/// the same rule, blamed on the file or on the caller.
void refusedBothWays(const LightProbes& probes, std::string_view says) {
    const auto result = read(fileWith({{"LPRB", lprbPayload(probes)}}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::MalformedData);
    CHECK(result.error().message().find(says) != std::string_view::npos);

    Bundle bundle;
    bundle.lightProbes = probes;
    const auto written = write(bundle);
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code() == ErrorCode::InvalidArgument);
    CHECK(written.error().message().find(says) != std::string_view::npos);
}

} // namespace

TEST_CASE("the LPRB golden bytes decode to the probes they encode", "[ubundle][lprb]") {
    const auto result = read(fileWith({{"COLL", emptyColl()}, {"LPRB", lprbPayload(golden())}}));
    REQUIRE(result.has_value());
    CHECK(result->header.formatVersion == 8);
    REQUIRE(result->collision.has_value());
    REQUIRE(result->lightProbes.has_value());
    sameProbes(*result->lightProbes, golden());
}

TEST_CASE("write emits LPRB after COLL as the golden bytes", "[ubundle][lprb]") {
    Bundle bundle;
    bundle.header.origin = Origin::Authored;
    bundle.collision = Collision{};
    bundle.lightProbes = golden();
    const auto written = write(bundle);
    REQUIRE(written.has_value());
    CHECK(*written == fileWith({{"COLL", emptyColl()}, {"LPRB", lprbPayload(golden())}}));
}

TEST_CASE("a probe is 84 bytes and an empty section 8", "[ubundle][lprb]") {
    LightProbes empty;
    empty.spacing = 128;
    CHECK(lprbPayload(empty).size() == 8);
    CHECK(lprbPayload(golden()).size() == 8 + 3 * 84);

    Bundle bundle;
    bundle.header.origin = Origin::Authored;
    bundle.lightProbes = empty;
    const auto written = write(bundle);
    REQUIRE(written.has_value());
    CHECK(*written == fileWith({{"LPRB", lprbPayload(empty)}}));
}

TEST_CASE("a spacing of zero is refused", "[ubundle][lprb]") {
    LightProbes probes = golden();
    probes.spacing = 0;
    refusedBothWays(probes, "LPRB: the spacing is 0");
}

TEST_CASE("an empty section with a spacing of zero is refused", "[ubundle][lprb]") {
    refusedBothWays(LightProbes{}, "LPRB: the spacing is 0");
}

TEST_CASE("two probes of one cell are refused", "[ubundle][lprb]") {
    LightProbes probes = golden();
    probes.probes[2].cell = probes.probes[1].cell;
    refusedBothWays(probes, "LPRB: probe 2's cell does not sort strictly after");
}

TEST_CASE("probes out of order on z are refused", "[ubundle][lprb]") {
    LightProbes probes = golden();
    std::swap(probes.probes[1], probes.probes[2]);
    refusedBothWays(probes, "LPRB: probe 2's cell does not sort strictly after");
}

TEST_CASE("probes out of order on x within one z and y are refused", "[ubundle][lprb]") {
    LightProbes probes = golden();
    // Both on z -2 and y 0: x decides, and 4 does not sort after 5.
    probes.probes[1].cell = {4, 0, -2};
    refusedBothWays(probes, "LPRB: probe 1's cell does not sort strictly after");
}

TEST_CASE("probes ordered by x before z are refused", "[ubundle][lprb]") {
    LightProbes probes = golden();
    // Ascending by x, descending by z: sorted the wrong way round.
    probes.probes[0].cell = {0, 0, 3};
    probes.probes[1].cell = {1, 0, 2};
    probes.probes[2].cell = {2, 0, 1};
    refusedBothWays(probes, "LPRB: probe 1's cell does not sort strictly after");
}

TEST_CASE("a negative cube value is refused", "[ubundle][lprb]") {
    LightProbes probes = golden();
    probes.probes[2].cube[4][0] = -0.5F;
    refusedBothWays(probes, "LPRB: probe 2's face 4 channel 0");
}

TEST_CASE("a NaN cube value is refused", "[ubundle][lprb]") {
    LightProbes probes = golden();
    probes.probes[0].cube[0][2] = std::numeric_limits<float>::quiet_NaN();
    refusedBothWays(probes, "LPRB: probe 0's face 0 channel 2");
}

TEST_CASE("an infinite cube value is refused", "[ubundle][lprb]") {
    LightProbes probes = golden();
    probes.probes[1].cube[5][1] = std::numeric_limits<float>::infinity();
    refusedBothWays(probes, "LPRB: probe 1's face 5 channel 1");
}
