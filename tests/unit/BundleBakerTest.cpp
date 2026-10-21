// UTA-0349's container cases: the BAKR section, which names the baker that
// wrote a bundle, and readBaker, which finds it in a bundle's first bytes.
//
// THE GOLDEN BYTES ARE AUTHORED BY HAND, never produced by `write`, for
// BundleTextureTest.cpp's reason: a transposition present in both the reader
// and the writer round-trips perfectly.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "ubundle/Bundle.h"

#include "Bytes.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using uta::ErrorCode;
using uta::testing::Bytes;
using uta::ubundle::BAKER_PREFIX_SIZE;
using uta::ubundle::Bundle;
using uta::ubundle::MAX_BAKER_LENGTH;
using uta::ubundle::PaintedSky;
using uta::ubundle::read;
using uta::ubundle::readBaker;
using uta::ubundle::write;

namespace {

constexpr std::string_view BAKER = "r52-f28-l0123456789abcdef";

Bytes bakerPayload(std::string_view baker) {
    Bytes out;
    out.str(baker);
    return out;
}

/// PSKY's seven f32, every one different.
Bytes skyPayload() {
    Bytes out;
    for (const float channel : {0.1f, 0.2f, 0.7f, 0.5f, 0.6f, 0.8f, 0.3f}) out.f32(channel);
    return out;
}

/// A whole derived .utab holding `sections` in the order given.
std::vector<std::byte> fileWith(const std::vector<std::pair<std::string_view, Bytes>>& sections,
                                std::uint32_t count) {
    Bytes out;
    out.id("UTAB");
    out.u32(30); // formatVersion -- 30 since UTA-0387 gave LPRB its sky visibility
    out.u8(0);   // origin: Derived
    out.u8(0);   // kind: Map
    out.u16(0);  // reserved
    out.u32(count);
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

std::vector<std::byte> fileWith(const std::vector<std::pair<std::string_view, Bytes>>& sections) {
    return fileWith(sections, static_cast<std::uint32_t>(sections.size()));
}

Bundle bundleWith(std::string baker) {
    Bundle bundle;
    bundle.baker = std::move(baker);
    bundle.paintedSky = PaintedSky{.top = {0.1f, 0.2f, 0.7f}, .horizon = {0.5f, 0.6f, 0.8f}, .clouds = 0.3f};
    return bundle;
}

} // namespace

TEST_CASE("BAKR is written first and reads back as the baker version", "[ubundle]") {
    const std::vector<std::byte> golden =
        fileWith({{"BAKR", bakerPayload(BAKER)}, {"PSKY", skyPayload()}});

    const auto written = write(bundleWith(std::string(BAKER)));
    REQUIRE(written.has_value());
    CHECK(*written == golden);

    const auto bundle = read(golden);
    REQUIRE(bundle.has_value());
    CHECK(bundle->baker == std::string(BAKER));
}

TEST_CASE("a bundle with no baker carries no BAKR", "[ubundle]") {
    Bundle bundle = bundleWith("x");
    bundle.baker.reset();
    const auto written = write(bundle);
    REQUIRE(written.has_value());
    CHECK(*written == fileWith({{"PSKY", skyPayload()}}));

    const auto back = read(*written);
    REQUIRE(back.has_value());
    CHECK_FALSE(back->baker.has_value());
}

TEST_CASE("a baker version that is empty or too long or not printable ASCII is refused", "[ubundle]") {
    const std::string longest(MAX_BAKER_LENGTH, 'r');
    REQUIRE(write(bundleWith(longest)).has_value());
    REQUIRE(read(fileWith({{"BAKR", bakerPayload(longest)}})).has_value());
    REQUIRE(write(bundleWith("!~")).has_value()); // the ends of the printable range

    for (const std::string& bad : {std::string{}, longest + "r", std::string("r52 f28"),
                                   std::string("r52\x7F"), std::string("r52\x1F")}) {
        INFO("baker: " << bad);
        const auto written = write(bundleWith(bad));
        REQUIRE_FALSE(written.has_value());
        CHECK(written.error().code() == ErrorCode::InvalidArgument);

        const auto parsed = read(fileWith({{"BAKR", bakerPayload(bad)}}));
        REQUIRE_FALSE(parsed.has_value());
        CHECK(parsed.error().code() == ErrorCode::MalformedData);

        const auto found = readBaker(fileWith({{"BAKR", bakerPayload(bad)}}));
        REQUIRE_FALSE(found.has_value());
        CHECK(found.error().code() == ErrorCode::MalformedData);
    }
}

TEST_CASE("readBaker needs only the header and the table and BAKR", "[ubundle]") {
    const std::vector<std::byte> whole =
        fileWith({{"BAKR", bakerPayload(BAKER)}, {"PSKY", skyPayload()}});
    const std::size_t throughBakr = 16 + 2 * 24 + 4 + BAKER.size();

    SECTION("the whole file") {
        const auto found = readBaker(whole);
        REQUIRE(found.has_value());
        CHECK(*found == std::string(BAKER));
    }
    SECTION("the file cut where BAKR ends") {
        const auto found = readBaker(std::span(whole).first(throughBakr));
        REQUIRE(found.has_value());
        CHECK(*found == std::string(BAKER));
    }
    SECTION("the file cut inside BAKR") {
        const auto found = readBaker(std::span(whole).first(throughBakr - 1));
        REQUIRE_FALSE(found.has_value());
        CHECK(found.error().code() == ErrorCode::MalformedData);
    }
    SECTION("the file cut inside the table") {
        REQUIRE_FALSE(readBaker(std::span(whole).first(16 + 24 + 10)).has_value());
    }
    SECTION("a bad magic") {
        std::vector<std::byte> bad = whole;
        bad[0] = std::byte{'X'};
        REQUIRE_FALSE(readBaker(bad).has_value());
    }
    SECTION("a section id this version does not define") {
        const auto found = readBaker(fileWith({{"PSKY", skyPayload()}, {"WHAT", bakerPayload(BAKER)}}));
        REQUIRE_FALSE(found.has_value());
        CHECK(found.error().code() == ErrorCode::MalformedData);
    }
    SECTION("more sections than this version defines") {
        const auto found = readBaker(fileWith({{"BAKR", bakerPayload(BAKER)}}, 20));
        REQUIRE_FALSE(found.has_value());
        CHECK(found.error().code() == ErrorCode::MalformedData);
    }
}

TEST_CASE("readBaker finds BAKR after other sections and nothing where there is none", "[ubundle]") {
    const auto later = readBaker(fileWith({{"PSKY", skyPayload()}, {"BAKR", bakerPayload(BAKER)}}));
    REQUIRE(later.has_value());
    CHECK(*later == std::string(BAKER));

    const auto none = readBaker(fileWith({{"PSKY", skyPayload()}}));
    REQUIRE(none.has_value());
    CHECK_FALSE(none->has_value());
}
