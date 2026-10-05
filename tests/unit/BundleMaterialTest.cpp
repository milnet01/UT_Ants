// UTA-0011's container cases: the MATS section.
//
// docs/specs/UTA-0011-map-baker.md SS 4.10, INV-18, UTA-0263 SS 4.2's flame
// look, that spec's INV-1, UTA-0105 SS 4.2's liquid look, that spec's INV-1, and UTA-0286 SS 4.2's
// fire look, that spec's INV-1, and UTA-0277 SS 4.1's tile kind, that spec's
// INV-7. The baker's own cases are
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
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using uta::ErrorCode;
using uta::testing::Bytes;
using uta::ubundle::Bundle;
using uta::ubundle::FireLook;
using uta::ubundle::FireSpark;
using uta::ubundle::FlameLook;
using uta::ubundle::LiquidKind;
using uta::ubundle::LiquidLook;
using uta::ubundle::MaterialRecord;
using uta::ubundle::Origin;
using uta::ubundle::read;
using uta::ubundle::TileKind;
using uta::ubundle::write;

namespace {

/// One element as the fixture states it. `metallic` is a byte rather than a
/// bool, so a case can state a value the struct cannot hold.
struct RecordSpec {
    std::string id;
    std::uint8_t metallic = 0;
    std::uint8_t parallaxDepth = 0; ///< UTA-0040 SS 4.1
    std::uint8_t flameByte = 0;     ///< UTA-0263 SS 4.2; a byte, so 2 can be stated
    std::optional<FlameLook> flame; ///< written after flameByte when present
    std::uint8_t liquidByte = 0;      ///< UTA-0105 SS 4.2: the kind, or 0; a byte, so 4 can be stated
    std::optional<LiquidLook> liquid; ///< written after liquidByte when present
    std::uint8_t fireByte = 0;        ///< UTA-0286 SS 4.2; a byte, so 2 can be stated
    std::optional<FireLook> fire;     ///< written after fireByte when present
    std::uint8_t tileKindByte = 0;    ///< UTA-0277 SS 4.1; a byte, so 3 can be stated
};

/// A look whose every field differs from its neighbour's and from the
/// defaults, so a reader that swapped two fields disagrees somewhere.
LiquidLook liquidOf(LiquidKind kind, std::uint8_t first) {
    LiquidLook look;
    look.kind = kind;
    look.amplitude = first;
    look.frequency = static_cast<std::uint8_t>(first + 1);
    look.panning = 3;
    look.moveIce = 1;
    look.pan = {static_cast<std::uint8_t>(first + 2), static_cast<std::uint8_t>(first + 3)};
    look.bump = {static_cast<std::uint8_t>(first + 4), static_cast<std::uint8_t>(first + 5),
                 static_cast<std::uint8_t>(first + 6)};
    look.size = {256, 128};
    float value = static_cast<float>(first);
    for (auto& colour : look.ramp)
        for (float& channel : colour) channel = (value += 0.25f);
    return look;
}

/// The look's fields in SS 4.2's wire order, after its kind byte.
void putLiquid(Bytes& out, const LiquidLook& look) {
    out.u8(look.amplitude);
    out.u8(look.frequency);
    out.u8(look.panning);
    out.u8(look.moveIce); // UTA-0270
    for (const std::uint8_t pan : look.pan) out.u8(pan);
    for (const std::uint8_t bump : look.bump) out.u8(bump);
    for (const std::uint16_t side : look.size) out.u16(side);
    for (const auto& colour : look.ramp)
        for (const float channel : colour) out.f32(channel);
}

/// A fire look whose fields all differ from each other and from the defaults,
/// with two sparks whose eight bytes all differ, so a reader that swapped two
/// fields or two sparks disagrees somewhere.
FireLook fireOf(std::uint8_t first) {
    FireLook look;
    look.size = {128, 64};
    look.renderHeat = first;
    look.rising = 1;
    look.masked = 1;
    look.sparksLimit = -3;
    look.maxFrameRate = 12.5f;
    for (std::size_t i = 0; i < look.palette.size(); ++i)
        look.palette[i] = {std::uint8_t(i), std::uint8_t(i + first), std::uint8_t(255 - i)};
    look.sparks = {{25, 200, 1, 2, 3, 4, 5, 6}, {26, 100, 7, 8, 9, 10, 11, 12}};
    return look;
}

/// The look's fields in SS 4.2's wire order, after its fire byte.
void putFire(Bytes& out, const FireLook& look) {
    for (const std::uint16_t side : look.size) out.u16(side);
    out.u8(look.renderHeat);
    out.u8(look.rising);
    out.u8(look.masked);
    out.u32(static_cast<std::uint32_t>(look.sparksLimit));
    out.f32(look.maxFrameRate);
    for (const auto& entry : look.palette)
        for (const std::uint8_t channel : entry) out.u8(channel);
    out.u16(static_cast<std::uint16_t>(look.sparks.size()));
    for (const FireSpark& spark : look.sparks)
        for (const std::uint8_t byte :
             {spark.type, spark.heat, spark.x, spark.y, spark.byteA, spark.byteB, spark.byteC, spark.byteD})
            out.u8(byte);
}

/// A ramp whose 24 values all differ, so a reader that swapped two channels or
/// two entries disagrees somewhere.
FlameLook rampFrom(float first) {
    FlameLook look;
    float value = first;
    for (auto& colour : look.ramp)
        for (float& channel : colour) channel = (value += 0.125f);
    return look;
}

/// A MATS payload: SS 4.2's vector<MaterialRecord>, in SS 4.10's field order,
/// with UTA-0040 SS 4.1's depth byte after `metallic`, then UTA-0263 SS 4.2's
/// flame byte and, where it is 1, the ramp's 24 floats; the liquid and fire
/// looks; and last UTA-0277 SS 4.1's tile kind byte.
Bytes matsPayload(const std::vector<RecordSpec>& records) {
    Bytes out;
    out.u32(static_cast<std::uint32_t>(records.size()));
    for (const RecordSpec& record : records) {
        out.str(record.id);
        out.u8(record.metallic);
        out.u8(record.parallaxDepth);
        out.u8(record.flameByte);
        if (record.flame)
            for (const auto& colour : record.flame->ramp)
                for (const float channel : colour) out.f32(channel);
        out.u8(record.liquidByte);
        if (record.liquid) putLiquid(out, *record.liquid);
        out.u8(record.fireByte);
        if (record.fire) putFire(out, *record.fire);
        out.u8(record.tileKindByte);
    }
    return out;
}

/// A whole .utab carrying exactly one MATS section holding `payload`.
std::vector<std::byte> fileWith(const Bytes& payload) {
    Bytes out;
    out.id("UTAB");
    out.u32(22); // formatVersion -- 22 since UTA-0277 gave each material its tile kind
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
/// dropped, reordered, defaulted or swapped a field disagrees somewhere. The
/// middle one is a flame, so a look read into its neighbour disagrees too. The
/// first and third are liquids of different kinds (UTA-0105), for the same reason,
/// and the last carries a fire look (UTA-0286). Three tile kinds, all different
/// from their neighbours' (UTA-0277).
const std::vector<RecordSpec> GOLDEN = {
    {"dm-fixture.base.wall", 0, 4, 0, std::nullopt, 1, liquidOf(LiquidKind::Wet, 10), 0, std::nullopt, 2},
    {"dm-fixture.base.wall#masked", 1, 0, 1, rampFrom(0.5f)},
    {"texpkg.floor", 0, 255, 0, std::nullopt, 3, liquidOf(LiquidKind::Wave, 40), 0, std::nullopt, 1},
    {"texpkg.spray", 0, 7, 0, std::nullopt, 0, std::nullopt, 1, fireOf(9), 2},
};

std::vector<MaterialRecord> recordsOf(const std::vector<RecordSpec>& specs) {
    std::vector<MaterialRecord> out;
    for (const RecordSpec& spec : specs)
        out.push_back(MaterialRecord{spec.id, spec.metallic == 1, spec.parallaxDepth, spec.flame, spec.liquid,
                                     spec.fire, static_cast<TileKind>(spec.tileKindByte)});
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
    CHECK(result->header.formatVersion == 22);
    CHECK_FALSE(result->textures.has_value());

    REQUIRE(result->materials.has_value());
    const std::vector<MaterialRecord>& materials = *result->materials;
    REQUIRE(materials.size() == 4);
    CHECK(materials[0].id == "dm-fixture.base.wall");
    CHECK_FALSE(materials[0].metallic);
    CHECK(materials[0].parallaxDepth == 4);
    CHECK(materials[1].id == "dm-fixture.base.wall#masked");
    CHECK(materials[1].metallic);
    CHECK(materials[1].parallaxDepth == 0);
    REQUIRE(materials[1].flame.has_value());
    CHECK(materials[1].flame->ramp == rampFrom(0.5f).ramp);
    CHECK_FALSE(materials[0].flame.has_value());
    CHECK_FALSE(materials[2].flame.has_value());
    CHECK(materials[2].id == "texpkg.floor");
    CHECK_FALSE(materials[2].metallic);
    CHECK(materials[2].parallaxDepth == 255);
    CHECK_FALSE(materials[1].liquid.has_value());
    REQUIRE(materials[0].liquid.has_value());
    CHECK(*materials[0].liquid == liquidOf(LiquidKind::Wet, 10));
    REQUIRE(materials[2].liquid.has_value());
    CHECK(*materials[2].liquid == liquidOf(LiquidKind::Wave, 40));
    for (std::size_t i = 0; i < 3; ++i) CHECK_FALSE(materials[i].fire.has_value());
    CHECK(materials[3].id == "texpkg.spray");
    REQUIRE(materials[3].fire.has_value());
    CHECK(*materials[3].fire == fireOf(9));
    CHECK(materials[0].tileKind == TileKind::Unsure);
    CHECK(materials[1].tileKind == TileKind::Fixed);
    CHECK(materials[2].tileKind == TileKind::Shuffle);
    CHECK(materials[3].tileKind == TileKind::Unsure);
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
            REQUIRE((*back->materials)[i].flame.has_value() == specs[i].flame.has_value());
            if (specs[i].flame) CHECK((*back->materials)[i].flame->ramp == specs[i].flame->ramp);
            CHECK((*back->materials)[i].liquid == specs[i].liquid);
            CHECK((*back->materials)[i].fire == specs[i].fire);
            CHECK((*back->materials)[i].tileKind == static_cast<TileKind>(specs[i].tileKindByte));
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
    // UTA-0277 SS 4.1's minimum element is 10 bytes. This payload declares two
    // records and holds one whole record and nine bytes more: 19 / 10 is one,
    // so the count is refused up front. A minimum of 9, the size before the
    // tile kind byte, would admit the count and fail later on a short read
    // instead -- a different refusal, which is what this case tells apart.
    Bytes payload;
    payload.u32(2);
    payload.str("");
    for (int i = 0; i < 6; ++i) payload.u8(0);
    payload.u32(0);
    for (int i = 0; i < 5; ++i) payload.u8(0);
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

TEST_CASE("UTA-0263 INV-1: a MATS flame byte of 2 is refused", "[ubundle][mats][flames]") {
    refused(fileWith(matsPayload({{"a", 0, 0, 2}})), "flame byte 2");
}

TEST_CASE("UTA-0263 INV-1: a flame ramp value that is not finite is refused", "[ubundle][mats][flames]") {
    for (const float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        FlameLook look = rampFrom(0.0f);
        look.ramp[7][2] = bad; // the last value, so a check stopping early misses it
        refused(fileWith(matsPayload({{"a", 0, 0, 1, look}})), "not finite");

        Bundle bundle;
        bundle.materials = recordsOf({{"a", 0, 0, 1, look}});
        const auto written = write(bundle);
        REQUIRE_FALSE(written.has_value());
        CHECK(written.error().code() == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("UTA-0105 INV-1: a MATS liquid kind of 4 is refused", "[ubundle][mats][liquids]") {
    refused(fileWith(matsPayload({{"a", 0, 0, 0, std::nullopt, 4, liquidOf(LiquidKind::Wet, 1)}})),
            "liquid byte 4");
}

TEST_CASE("UTA-0105 INV-1: an invalid liquid look is refused by read and write", "[ubundle][mats][liquids]") {
    struct Case {
        std::string_view what;
        std::string_view says;
        void (*breakIt)(LiquidLook&);
    };
    const Case cases[] = {
        {"a panning style past WavyY", "panning", [](LiquidLook& l) { l.panning = 5; }},
        {"a MoveIce byte of 2", "MoveIce", [](LiquidLook& l) { l.moveIce = 2; }},
        {"a width of 0", "size", [](LiquidLook& l) { l.size[0] = 0; }},
        {"a height above 8192", "size", [](LiquidLook& l) { l.size[1] = 8193; }},
        {"a ramp value that is not finite", "not finite",
         [](LiquidLook& l) { l.ramp[7][2] = std::numeric_limits<float>::quiet_NaN(); }},
    };
    for (const Case& c : cases) {
        INFO(c.what);
        LiquidLook look = liquidOf(LiquidKind::Ice, 1);
        c.breakIt(look);
        refused(fileWith(matsPayload({{"a", 0, 0, 0, std::nullopt, 2, look}})), c.says);

        Bundle bundle;
        bundle.materials = recordsOf({{"a", 0, 0, 0, std::nullopt, 2, look}});
        const auto written = write(bundle);
        REQUIRE_FALSE(written.has_value());
        CHECK(written.error().code() == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("UTA-0105 INV-1: the largest permitted look round-trips", "[ubundle][mats][liquids]") {
    // The edges of each refused range, so an off-by-one bound refuses these.
    LiquidLook look = liquidOf(LiquidKind::Ice, 1);
    look.panning = 4;
    look.size = {1, 8192};
    Bundle bundle;
    bundle.materials = recordsOf({{"a", 0, 0, 0, std::nullopt, 2, look}});
    const auto written = write(bundle);
    REQUIRE(written.has_value());
    const auto back = read(*written);
    REQUIRE(back.has_value());
    CHECK((*back->materials)[0].liquid == look);
}

TEST_CASE("UTA-0286 INV-1: a MATS fire byte of 2 is refused", "[ubundle][mats][fire]") {
    refused(fileWith(matsPayload({{"a", 0, 0, 0, std::nullopt, 0, std::nullopt, 2}})), "fire byte 2");
}

TEST_CASE("UTA-0286 INV-1: an invalid fire look is refused by read and write", "[ubundle][mats][fire]") {
    struct Case {
        std::string_view what;
        std::string_view says;
        void (*breakIt)(FireLook&);
    };
    const Case cases[] = {
        {"a width of 0", "size", [](FireLook& l) { l.size[0] = 0; }},
        {"a height above FIRE_SIZE_MAX", "size", [](FireLook& l) { l.size[1] = 1025; }},
        {"a rising byte of 2", "rising", [](FireLook& l) { l.rising = 2; }},
        {"a masked byte of 2", "masked", [](FireLook& l) { l.masked = 2; }},
        {"a negative MaxFrameRate", "MaxFrameRate", [](FireLook& l) { l.maxFrameRate = -1.0f; }},
        {"a MaxFrameRate that is not finite", "MaxFrameRate",
         [](FireLook& l) { l.maxFrameRate = std::numeric_limits<float>::infinity(); }},
        {"more sparks than FIRE_SPARKS_MAX", "sparks", [](FireLook& l) { l.sparks.resize(4097); }},
    };
    for (const Case& c : cases) {
        INFO(c.what);
        FireLook look = fireOf(1);
        c.breakIt(look);
        refused(fileWith(matsPayload({{"a", 0, 0, 0, std::nullopt, 0, std::nullopt, 1, look}})), c.says);

        Bundle bundle;
        bundle.materials = recordsOf({{"a", 0, 0, 0, std::nullopt, 0, std::nullopt, 1, look}});
        const auto written = write(bundle);
        REQUIRE_FALSE(written.has_value());
        CHECK(written.error().code() == ErrorCode::InvalidArgument);
    }
}

TEST_CASE("UTA-0286 INV-1: a record with both a flame look and a fire look is refused", "[ubundle][mats][fire]") {
    const std::vector<RecordSpec> both{{"a", 0, 0, 1, rampFrom(0.0f), 0, std::nullopt, 1, fireOf(1)}};
    refused(fileWith(matsPayload(both)), "both a flame look and a fire look");
    Bundle bundle;
    bundle.materials = recordsOf(both);
    const auto written = write(bundle);
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE("UTA-0286 INV-1: the largest permitted fire look round-trips", "[ubundle][mats][fire]") {
    // The edges of each refused range, so an off-by-one bound refuses these.
    FireLook look = fireOf(1);
    look.size = {1, 1024};
    look.maxFrameRate = 0.0f;
    look.sparks.resize(4096, {13, 1, 2, 3, 4, 5, 6, 7});
    Bundle bundle;
    bundle.materials = recordsOf({{"a", 0, 0, 0, std::nullopt, 0, std::nullopt, 1, look}});
    const auto written = write(bundle);
    REQUIRE(written.has_value());
    const auto back = read(*written);
    REQUIRE(back.has_value());
    CHECK((*back->materials)[0].fire == look);
}

TEST_CASE("UTA-0277 INV-7: a MATS tile kind byte of 3 is refused by read and write", "[ubundle][mats][tilekind]") {
    refused(fileWith(matsPayload({{"a", 0, 0, 0, std::nullopt, 0, std::nullopt, 0, std::nullopt, 3}})),
            "tile kind byte 3");
    Bundle bundle;
    bundle.materials = recordsOf({{"a", 0, 0, 0, std::nullopt, 0, std::nullopt, 0, std::nullopt, 3}});
    const auto written = write(bundle);
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().code() == ErrorCode::InvalidArgument);
}
