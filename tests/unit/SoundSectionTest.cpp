// UTA-0478's container cases: the SNDS section and the Sounds bundle kind.
//
// docs/specs/UTA-0478-baked-sounds.md SS 4.1 and SS 4.2, INV-1 to INV-3.
//
// THE RAW BYTES ARE AUTHORED FROM SS 4.1, never produced by `write`, for
// BundleTextureTest.cpp's reason: a transposition present in both the reader
// and the writer round-trips perfectly. `write` is checked against them.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "ubundle/Bundle.h"

#include "Bytes.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using uta::ErrorCode;
using uta::testing::Bytes;
using uta::ubundle::BakedSound;
using uta::ubundle::Bundle;
using uta::ubundle::BundleKind;
using uta::ubundle::Origin;
using uta::ubundle::read;
using uta::ubundle::SOUND_LIMIT;
using uta::ubundle::write;

namespace {

using Sounds = std::vector<BakedSound>;

BakedSound sound(std::string path, std::initializer_list<int> bytes) {
    BakedSound out;
    out.path = std::move(path);
    for (const int value : bytes) out.data.push_back(static_cast<std::byte>(value));
    return out;
}

/// Three sounds in ascending path order, the middle one a single byte.
Sounds golden() {
    return {sound("botpack.a.alpha", {0x52, 0x49, 0x46, 0x46, 0x00}), sound("botpack.a.beta", {0xFF}),
            sound("engine.b.gamma", {0x01, 0x02, 0x03})};
}

/// SS 4.1's layout: a u32 count, then for each a string and a u32 byte count
/// and that many bytes. `extraCount` is added to every byte count, so a count
/// can be made to pass its section.
Bytes sndsPayload(const Sounds& sounds, std::uint32_t extraCount = 0) {
    Bytes out;
    out.u32(static_cast<std::uint32_t>(sounds.size()));
    for (const BakedSound& item : sounds) {
        out.str(item.path);
        out.u32(static_cast<std::uint32_t>(item.data.size()) + extraCount);
        for (const std::byte part : item.data) out.u8(static_cast<std::uint8_t>(part));
    }
    return out;
}

/// LITE with no lights: a zero count.
Bytes litePayload() {
    Bytes out;
    out.u32(0);
    return out;
}

Bytes bakrPayload() {
    Bytes out;
    out.str("r79-f37");
    return out;
}

/// A whole .utab with the given kind byte and sections, in the order given.
std::vector<std::byte> fileWith(std::uint8_t kind, const std::vector<std::pair<std::string_view, Bytes>>& sections) {
    Bytes out;
    out.id("UTAB");
    out.u32(38); // formatVersion -- 38 since UTA-0493 gave ZONE its pain
    out.u8(1);   // origin: Authored
    out.u8(kind);
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

std::vector<std::byte> mapFileWith(const Bytes& snds) { return fileWith(0, {{"SNDS", snds}}); }

Bundle mapBundleWith(Sounds sounds) {
    Bundle bundle;
    bundle.header.origin = Origin::Authored;
    bundle.sounds = std::move(sounds);
    return bundle;
}

Bundle soundsBundleWith(Sounds sounds) {
    Bundle bundle = mapBundleWith(std::move(sounds));
    bundle.header.kind = BundleKind::Sounds;
    bundle.baker = "r79-f37";
    return bundle;
}

void sameSounds(const Sounds& actual, const Sounds& expected) {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CAPTURE(i);
        CHECK(actual[i].path == expected[i].path);
        CHECK(actual[i].data == expected[i].data);
    }
}

/// INV-2's refusals, one fixture per rule, each valid but for that rule.
/// The byte count past its section has no `write` side, so it is separate.
const std::vector<std::pair<std::string_view, Sounds>>& badSounds() {
    static const std::vector<std::pair<std::string_view, Sounds>> bad = [] {
        std::vector<std::pair<std::string_view, Sounds>> out;
        out.emplace_back("two equal paths", Sounds{sound("a.b.c", {1}), sound("a.b.c", {2})});
        out.emplace_back("two paths descending", Sounds{sound("a.b.d", {1}), sound("a.b.c", {2})});
        out.emplace_back("an empty path", Sounds{sound("", {1})});
        out.emplace_back("an empty payload", Sounds{sound("a.b.c", {})});
        Sounds many;
        for (std::size_t i = 0; i <= SOUND_LIMIT; ++i) {
            std::string path = std::to_string(i);
            path.insert(0, 6 - path.size(), '0'); // fixed width, so ascending by byte
            many.push_back(sound(std::move(path), {1}));
        }
        out.emplace_back("one sound past the limit", std::move(many));
        return out;
    }();
    return bad;
}

} // namespace

TEST_CASE("UTA-0478 INV-1: SNDS reads back what was written in a map bundle", "[ubundle][sounds]") {
    const auto written = write(mapBundleWith(golden()));
    REQUIRE(written.has_value());
    CHECK(*written == mapFileWith(sndsPayload(golden())));
    const auto back = read(*written);
    if (!back.has_value()) FAIL(back.error().message());
    REQUIRE(back->sounds.has_value());
    sameSounds(*back->sounds, golden());
}

TEST_CASE("UTA-0478 INV-1: SNDS reads back what was written in a Sounds bundle", "[ubundle][sounds]") {
    const auto written = write(soundsBundleWith(golden()));
    REQUIRE(written.has_value());
    // BAKR first, then SNDS, under kind byte 2.
    CHECK(*written == fileWith(2, {{"BAKR", bakrPayload()}, {"SNDS", sndsPayload(golden())}}));
    const auto back = read(*written);
    if (!back.has_value()) FAIL(back.error().message());
    CHECK(back->header.kind == BundleKind::Sounds);
    REQUIRE(back->sounds.has_value());
    sameSounds(*back->sounds, golden());
}

TEST_CASE("UTA-0478 INV-1: an empty SNDS stays distinct from an absent one", "[ubundle][sounds]") {
    const auto back = read(*write(mapBundleWith({})));
    REQUIRE(back.has_value());
    REQUIRE(back->sounds.has_value());
    CHECK(back->sounds->empty());

    Bundle none;
    const auto noneBack = read(*write(none));
    REQUIRE(noneBack.has_value());
    CHECK_FALSE(noneBack->sounds.has_value());
}

TEST_CASE("UTA-0478 INV-2: read and write each refuse every invalid SNDS", "[ubundle][sounds]") {
    for (const auto& [name, sounds] : badSounds()) {
        CAPTURE(name);
        const auto result = read(mapFileWith(sndsPayload(sounds)));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == ErrorCode::MalformedData);
        const auto written = write(mapBundleWith(sounds));
        REQUIRE_FALSE(written.has_value());
        CHECK(written.error().code() == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("UTA-0478 INV-2: the fixtures are valid but for their one rule", "[ubundle][sounds]") {
    // Without this a fixture that breaks a second rule would still be refused.
    CHECK(read(mapFileWith(sndsPayload({sound("a.b.c", {1}), sound("a.b.d", {2})}))).has_value());
    Sounds atLimit;
    for (std::size_t i = 0; i < SOUND_LIMIT; ++i) {
        std::string path = std::to_string(i);
        path.insert(0, 6 - path.size(), '0');
        atLimit.push_back(sound(std::move(path), {1}));
    }
    CHECK(read(mapFileWith(sndsPayload(atLimit))).has_value());
    CHECK(write(mapBundleWith(atLimit)).has_value());
}

TEST_CASE("UTA-0478 INV-2: a byte count passing its section is refused by read", "[ubundle][sounds]") {
    // One past the section's end: the last sound's count is one too many.
    Sounds one = {sound("a.b.c", {1, 2, 3})};
    const auto result = read(mapFileWith(sndsPayload(one, 1)));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::MalformedData);

    // A count near the top of a u32 must not wrap past the same check.
    const auto huge = read(mapFileWith(sndsPayload(one, 0xFFFFFFF0u)));
    REQUIRE_FALSE(huge.has_value());
    CHECK(huge.error().code() == ErrorCode::MalformedData);
}

TEST_CASE("UTA-0478 INV-3: a Sounds bundle holding another section is refused", "[ubundle][sounds]") {
    Bundle bundle = soundsBundleWith(golden());
    bundle.lights.emplace(); // an empty LITE
    const auto written = write(bundle);
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code() == ErrorCode::InvalidArgument);

    const auto result = read(
        fileWith(2, {{"BAKR", bakrPayload()}, {"LITE", litePayload()}, {"SNDS", sndsPayload(golden())}}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::MalformedData);

    // The same sections in a Map bundle are fine.
    CHECK(read(fileWith(0, {{"BAKR", bakrPayload()}, {"LITE", litePayload()}, {"SNDS", sndsPayload(golden())}}))
              .has_value());
}

TEST_CASE("UTA-0478 INV-3: a kind byte of 3 is refused by read", "[ubundle][sounds]") {
    auto bytes = *write(soundsBundleWith(golden()));
    REQUIRE(bytes[9] == static_cast<std::byte>(2)); // magic 4, version 4, origin 1, then the kind
    CHECK(read(bytes).has_value());
    bytes[9] = static_cast<std::byte>(3);
    const auto result = read(bytes);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::MalformedData);
}
