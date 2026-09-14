// UTA-0011's container cases: the MATS section.
//
// docs/specs/UTA-0011-map-baker.md SS 4.10, INV-18. The baker's own cases are
// tests/unit/BakeTest.cpp; nothing here reaches ubake.
//
// THE GOLDEN ARRAY IS AUTHORED FROM SS 4.10, never produced by `write`, for
// BundleTextureTest.cpp's reason: a transposition present in both the reader
// and the writer round-trips perfectly, so the reader and the writer are each
// graded against the same bytes instead.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "ubundle/Bundle.h"

#include "Bytes.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using uta::ErrorCode;
using uta::testing::Bytes;
using uta::ubundle::Bundle;
using uta::ubundle::MaterialRecord;
using uta::ubundle::Origin;
using uta::ubundle::read;
using uta::ubundle::write;

namespace {

/// One element as the fixture states it. `metallic` is a byte rather than a
/// bool, so a case can state a value the struct cannot hold.
struct RecordSpec {
    std::string id;
    std::uint8_t metallic = 0;
    std::uint8_t parallaxDepth = 0; ///< UTA-0040 SS 4.1
};

/// A MATS payload: SS 4.2's vector<MaterialRecord>, in SS 4.10's field order,
/// with UTA-0040 SS 4.1's depth byte after `metallic`.
Bytes matsPayload(const std::vector<RecordSpec>& records) {
    Bytes out;
    out.u32(static_cast<std::uint32_t>(records.size()));
    for (const RecordSpec& record : records) {
        out.str(record.id);
        out.u8(record.metallic);
        out.u8(record.parallaxDepth);
    }
    return out;
}

/// A whole .utab carrying exactly one MATS section holding `payload`.
std::vector<std::byte> fileWith(const Bytes& payload) {
    Bytes out;
    out.id("UTAB");
    out.u32(10); // formatVersion -- 10 since UTA-0162 SS 4.1
    out.u8(1);  // origin: Authored
    out.u8(0);  // kind: Map
    out.u16(0); // reserved
    out.u32(1); // sectionCount

    out.id("MATS");
    out.u64(16 + 24); // offset: the table ends here
    out.u64(payload.size());
    out.u8(0); // compression
    out.u8(0);
    out.u8(0);
    out.u8(0);

    out.append(payload);
    return out.data();
}

/// Three records in ascending id order, their metallic values not all equal and
/// their depths all different -- the top byte among them -- so a reader that
/// dropped, reordered, defaulted or swapped a field disagrees somewhere.
const std::vector<RecordSpec> GOLDEN = {
    {"dm-fixture.base.wall", 0, 4},
    {"dm-fixture.base.wall#masked", 1, 0},
    {"texpkg.floor", 0, 255},
};

std::vector<MaterialRecord> recordsOf(const std::vector<RecordSpec>& specs) {
    std::vector<MaterialRecord> out;
    for (const RecordSpec& spec : specs)
        out.push_back(MaterialRecord{spec.id, spec.metallic == 1, spec.parallaxDepth});
    return out;
}

void refused(const std::vector<std::byte>& bytes, std::string_view says) {
    const auto result = read(bytes);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == ErrorCode::MalformedData);
    CHECK(result.error().message().find(says) != std::string_view::npos);
}

} // namespace

TEST_CASE("the MATS golden bytes decode to the records they encode", "[ubundle][mats]") {
    const auto result = read(fileWith(matsPayload(GOLDEN)));
    REQUIRE(result.has_value());
    CHECK(result->header.formatVersion == 10);
    CHECK_FALSE(result->textures.has_value());

    REQUIRE(result->materials.has_value());
    const std::vector<MaterialRecord>& materials = *result->materials;
    REQUIRE(materials.size() == 3);
    CHECK(materials[0].id == "dm-fixture.base.wall");
    CHECK_FALSE(materials[0].metallic);
    CHECK(materials[0].parallaxDepth == 4);
    CHECK(materials[1].id == "dm-fixture.base.wall#masked");
    CHECK(materials[1].metallic);
    CHECK(materials[1].parallaxDepth == 0);
    CHECK(materials[2].id == "texpkg.floor");
    CHECK_FALSE(materials[2].metallic);
    CHECK(materials[2].parallaxDepth == 255);
}

TEST_CASE("write emits the MATS golden bytes", "[ubundle][mats]") {
    Bundle bundle;
    bundle.header.origin = Origin::Authored;
    bundle.materials = recordsOf(GOLDEN);
    const auto written = write(bundle);
    REQUIRE(written.has_value());
    CHECK(*written == fileWith(matsPayload(GOLDEN)));
}

TEST_CASE("MATS round-trips through write and read", "[ubundle][mats]") {
    // An empty id is legal -- SS 4.10's minimum element is one -- and a
    // present but empty section is distinct from an absent one (UTA-0008
    // SS 4.4).
    for (const std::vector<RecordSpec>& specs :
         {GOLDEN, std::vector<RecordSpec>{{"", 1, 1}}, std::vector<RecordSpec>{}}) {
        Bundle bundle;
        bundle.materials = recordsOf(specs);
        const auto written = write(bundle);
        REQUIRE(written.has_value());
        const auto back = read(*written);
        REQUIRE(back.has_value());
        REQUIRE(back->materials.has_value());
        REQUIRE(back->materials->size() == specs.size());
        for (std::size_t i = 0; i < specs.size(); ++i) {
            CHECK((*back->materials)[i].id == specs[i].id);
            CHECK((*back->materials)[i].metallic == (specs[i].metallic == 1));
            CHECK((*back->materials)[i].parallaxDepth == specs[i].parallaxDepth);
        }
    }
}

TEST_CASE("a MATS metallic byte of 2 is refused", "[ubundle][mats]") {
    // INV-18: refused, never read as true or false.
    refused(fileWith(matsPayload({{"a", 2}})), "metallic byte 2");
}

TEST_CASE("MATS records out of order are refused", "[ubundle][mats]") {
    // INV-18. Two orders: plainly descending, and a repeated id, which
    // strictly ascending also rules out.
    refused(fileWith(matsPayload({{"b", 0}, {"a", 0}})), "does not sort strictly after");
    refused(fileWith(matsPayload({{"a", 0}, {"a", 1}})), "does not sort strictly after");
}

TEST_CASE("write refuses MATS records out of order", "[ubundle][mats]") {
    Bundle bundle;
    bundle.materials = recordsOf({{"b", 0}, {"a", 0}});
    const auto written = write(bundle);
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE("a MATS count the section cannot hold is refused before an element is read",
          "[ubundle][mats]") {
    // UTA-0040 SS 4.1's minimum element is 6 bytes. This payload declares two
    // records and holds one whole record and five bytes more: 11 / 6 is one,
    // so the count is refused up front. A minimum of 5, the size before the
    // depth byte, would admit the count and fail later on a short read instead
    // -- a different refusal, which is what this case tells apart.
    Bytes payload;
    payload.u32(2);
    payload.str("");
    payload.u8(0);
    payload.u8(0);
    payload.u32(0);
    payload.u8(0);
    refused(fileWith(payload), "exceeds the bytes remaining");
}

TEST_CASE("write emits MATS after TEXS", "[ubundle][mats]") {
    // SS 4.10: appended, so UTA-0008 SS 4.10's order clause is extended.
    Bundle bundle;
    bundle.textures.emplace();
    bundle.materials.emplace();
    const auto written = write(bundle);
    REQUIRE(written.has_value());
    REQUIRE(written->size() > 16 + 2 * 24);
    const auto idAt = [&](std::size_t offset) {
        std::string id;
        for (std::size_t i = 0; i < 4; ++i) id += static_cast<char>((*written)[offset + i]);
        return id;
    };
    CHECK(idAt(16) == "TEXS");
    CHECK(idAt(16 + 24) == "MATS");
}
