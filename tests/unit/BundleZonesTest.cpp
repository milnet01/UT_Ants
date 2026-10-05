// UTA-0156's container cases: the ZONE section, with UTA-0015's fog flag.
//
// docs/specs/UTA-0156-zone-ambient-light.md SS 4.1, INV-1, and
// docs/specs/UTA-0015-volumetric-fog.md SS 4.1, INV-1. Which zone a vertex may
// name is UTA-0156 INV-2's, in BundleGeometryTest.cpp and BundleMoversTest.cpp.
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
using uta::ubundle::LightProbes;
using uta::ubundle::Origin;
using uta::ubundle::read;
using uta::ubundle::write;
using uta::ubundle::Zone;

namespace {

/// A ZONE payload in SS 4.1's order: the count, then each entry's brightness,
/// hue, saturation and fog flag.
Bytes zonePayload(const std::vector<Zone>& zones) {
    Bytes out;
    out.u32(static_cast<std::uint32_t>(zones.size()));
    for (const Zone& zone : zones) {
        out.u8(zone.brightness);
        out.u8(zone.hue);
        out.u8(zone.saturation);
        out.u8(zone.fog);
        out.f32(zone.panSpeed[0]); // UTA-0276
        out.f32(zone.panSpeed[1]);
        out.u8(zone.water); // UTA-0215
        for (const float part : zone.viewFog) out.f32(part);
        out.f32(zone.viewFlash);
    }
    return out;
}

/// An LPRB payload with a spacing of 32 and no probe.
Bytes emptyLprb() {
    Bytes out;
    out.u32(32);
    out.u32(0);
    return out;
}

/// A whole .utab holding `sections` in the order given.
std::vector<std::byte> fileWith(const std::vector<std::pair<std::string_view, Bytes>>& sections) {
    Bytes out;
    out.id("UTAB");
    out.u32(21); // formatVersion -- 21 since UTA-0286 gave a non-flame FireTexture its fire look
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

/// Three entries, every byte distinct but the flags, which alternate, so a
/// transposition shows.
std::vector<Zone> golden() {
    return {Zone{90, 17, 250, 1, {2.32F, 1.0F}, 1, {0.1289F, 0.1953F, 0.1758F}, -0.078F},
            Zone{40, 3, 200, 0, {0.5F, -3.0F}}, Zone{7, 131, 0, 1, {1, 1}, 1, {0.5859F, 0.1953F, 0.0781F}, 0}};
}

void sameZones(const std::vector<Zone>& actual, const std::vector<Zone>& expected) {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CAPTURE(i);
        CHECK(int(actual[i].brightness) == int(expected[i].brightness));
        CHECK(int(actual[i].hue) == int(expected[i].hue));
        CHECK(int(actual[i].saturation) == int(expected[i].saturation));
        CHECK(int(actual[i].fog) == int(expected[i].fog));
        CHECK(actual[i].panSpeed == expected[i].panSpeed);
        CHECK(int(actual[i].water) == int(expected[i].water));
        CHECK(actual[i].viewFog == expected[i].viewFog);
        CHECK(actual[i].viewFlash == expected[i].viewFlash);
    }
}

/// `read` refuses a file holding these entries with MalformedData, and `write`
/// refuses a bundle holding them with InvalidArgument, both naming `says`.
void refusedBothWays(const std::vector<Zone>& zones, std::string_view says) {
    const auto result = read(fileWith({{"ZONE", zonePayload(zones)}}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::MalformedData);
    CHECK_THAT(std::string(result.error().message()), Catch::Matchers::ContainsSubstring(std::string(says)));

    Bundle bundle;
    bundle.zones = zones;
    const auto written = write(bundle);
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code() == ErrorCode::InvalidArgument);
    CHECK_THAT(std::string(written.error().message()), Catch::Matchers::ContainsSubstring(std::string(says)));
}

} // namespace

TEST_CASE("INV-1: the ZONE golden bytes decode to the entries they encode", "[ubundle][zone]") {
    const auto result = read(fileWith({{"ZONE", zonePayload(golden())}}));
    REQUIRE(result.has_value());
    CHECK(result->header.formatVersion == 21);
    REQUIRE(result->zones.has_value());
    sameZones(*result->zones, golden());
}

TEST_CASE("INV-1: write emits ZONE after LPRB as the golden bytes", "[ubundle][zone]") {
    Bundle bundle;
    bundle.header.origin = Origin::Authored;
    bundle.lightProbes = LightProbes{32, {}};
    bundle.zones = golden();
    const auto written = write(bundle);
    REQUIRE(written.has_value());
    CHECK(*written == fileWith({{"LPRB", emptyLprb()}, {"ZONE", zonePayload(golden())}}));
}

TEST_CASE("INV-1: a ZONE of no entries is refused", "[ubundle][zone]") {
    refusedBothWays({}, "ZONE: 0 entries");
}

TEST_CASE("INV-1: a ZONE of 65 entries is refused and one of 64 is not", "[ubundle][zone]") {
    refusedBothWays(std::vector<Zone>(65), "ZONE: 65 entries");

    Bundle bundle;
    bundle.zones = std::vector<Zone>(64);
    const auto written = write(bundle);
    REQUIRE(written.has_value());
    const auto result = read(*written);
    REQUIRE(result.has_value());
    CHECK(result->zones->size() == 64);
}

TEST_CASE("UTA-0276: a ZONE pan speed that is not finite is refused", "[ubundle][zone]") {
    refusedBothWays({Zone{0, 0, 0, 0, {std::numeric_limits<float>::infinity(), 1}}}, "entry 0's pan speed is not finite");
    refusedBothWays({Zone{0, 0, 0, 0, {1, std::numeric_limits<float>::quiet_NaN()}}}, "entry 0's pan speed is not finite");
}

TEST_CASE("UTA-0215 INV-1: a ZONE water byte of 2 or a tint that is not finite is refused", "[ubundle][zone]") {
    refusedBothWays({Zone{0, 0, 0, 0, {1, 1}, 2}}, "entry 0 has a water byte of 2");
    refusedBothWays({Zone{0, 0, 0, 0, {1, 1}, 1, {std::numeric_limits<float>::infinity(), 0, 0}, 0}},
                    "entry 0's view tint is not finite");
    refusedBothWays({Zone{0, 0, 0, 0, {1, 1}, 1, {0, 0, 0}, std::numeric_limits<float>::quiet_NaN()}},
                    "entry 0's view tint is not finite");
}

TEST_CASE("UTA-0015 INV-1: a fog byte of 2 is refused", "[ubundle][zone]") {
    refusedBothWays({Zone{0, 0, 0, 1}, Zone{0, 0, 0, 2}}, "entry 1 has a fog byte of 2");
}
