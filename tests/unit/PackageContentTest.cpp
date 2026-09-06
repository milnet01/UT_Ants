// The typed level-content readers, against fixtures.
//
// Locks INV-1 (a reader consumes its export exactly or returns an Error),
// INV-5 (the redundant offset fields are checked, not discarded), INV-6 (the
// second mip chain is read if and only if bHasComp says so), INV-7 (bulk
// payload is a view, never a copy) and INV-8 (an unmodelled class is refused
// by name).
//
// docs/specs/UTA-0004-typed-level-content.md SS 7 tier 1.
//
// `readModel` has no case here: SS 4.5 withholds that layout, so the fixture
// builder has nothing to encode against, and SS 10 records that the real-asset
// tier is its only check.
//
// Two versions run throughout. SS 4.6's WidthOffset and SS 4.8's NextOffset
// exist only from package version 63, and SS 2.1 measured that stock content
// sits on both sides of that line -- so a fixture set at one version leaves a
// branch real packages take exercised by nothing.

#include "support/UnrealPackageBuilder.h"
#include "upkg/Geometry.h"
#include "upkg/Level.h"
#include "upkg/Package.h"
#include "upkg/Sound.h"
#include "upkg/Texture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

using uta::ErrorCode;
using uta::test::asBytes;
using uta::test::encodeCompactIndex;
using uta::test::ExportEntry;
using uta::test::ImportEntry;
using uta::test::TaggedPropertyWriter;
using uta::test::UnrealPackageBuilder;
using uta::upkg::Package;

namespace {

constexpr std::int32_t NAME_NONE = 0;
constexpr std::int32_t NAME_OBJECT = 1;
constexpr std::int32_t NAME_CLASS_WORD = 2;
constexpr std::int32_t NAME_ITEM = 3;
constexpr std::int32_t NAME_WAV = 4;
constexpr std::int32_t NAME_HASCOMP = 5;
/// The class name is what the reader dispatches on (SS 3.3 item 4), so it is
/// a fixture parameter rather than a constant.
constexpr std::int32_t NAME_CLASS_NAME = 6;

void appendU8(std::vector<std::uint8_t>& into, std::uint8_t value) {
    into.push_back(value);
}

void appendU16(std::vector<std::uint8_t>& into, std::uint16_t value) {
    into.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    into.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void appendU32(std::vector<std::uint8_t>& into, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        into.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
}

void appendFloat(std::vector<std::uint8_t>& into, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32(into, bits);
}

void appendIndex(std::vector<std::uint8_t>& into, std::int32_t value) {
    const std::vector<std::uint8_t> encoded = encodeCompactIndex(value);
    into.insert(into.end(), encoded.begin(), encoded.end());
}

void appendVector(std::vector<std::uint8_t>& into, float x, float y, float z) {
    appendFloat(into, x);
    appendFloat(into, y);
    appendFloat(into, z);
}

UnrealPackageBuilder packageWithNames(std::uint16_t version, std::string_view className) {
    UnrealPackageBuilder builder;
    builder.setPackageVersion(version)
        .addName("None")
        .addName("TheObject")
        .addName("Class")
        .addName("ItemName")
        .addName("WAV")
        .addName("bHasComp")
        .addName(className);
    return builder;
}

/// A package holding one export of class `className` whose serialised data is
/// `data`. The class arrives as an IMPORT, which is how a real package names a
/// class it does not itself define.
std::vector<std::uint8_t> packageWithObject(std::uint16_t version,
                                            std::string_view className,
                                            const std::vector<std::uint8_t>& data) {
    UnrealPackageBuilder builder = packageWithNames(version, className);

    ImportEntry import;
    import.classPackage = NAME_NONE;
    import.className = NAME_CLASS_WORD;
    import.objectName = NAME_CLASS_NAME;
    builder.addImport(import);

    ExportEntry entry;
    entry.objectName = NAME_OBJECT;
    entry.objectClass = -1; // import 0
    entry.serialData = data;
    builder.addExport(entry);
    return builder.build();
}

/// An empty property list -- just the `None` terminator.
std::vector<std::uint8_t> emptyProperties() {
    return TaggedPropertyWriter{}.build(NAME_NONE);
}

/// One polygon with `vertexCount` vertices, in SS 4.4's layout.
std::vector<std::uint8_t> onePolygon(std::int32_t vertexCount) {
    std::vector<std::uint8_t> body;
    appendIndex(body, vertexCount);
    appendVector(body, 1.0F, 2.0F, 3.0F);  // base
    appendVector(body, 0.0F, 0.0F, 1.0F);  // normal
    appendVector(body, 1.0F, 0.0F, 0.0F);  // textureU
    appendVector(body, 0.0F, 1.0F, 0.0F);  // textureV
    for (std::int32_t index = 0; index < vertexCount; ++index) {
        appendVector(body, static_cast<float>(index), 0.0F, 0.0F);
    }
    appendU32(body, 0x20u); // polyFlags
    appendIndex(body, 0);   // actor: null
    appendIndex(body, 0);   // texture: null
    appendIndex(body, NAME_ITEM);
    appendIndex(body, 7);  // link
    appendIndex(body, 11); // brushPoly
    appendU16(body, static_cast<std::uint16_t>(-3));
    appendU16(body, 5);
    return body;
}

std::vector<std::uint8_t> polysData(std::int32_t polygonCount, std::int32_t vertexCount) {
    std::vector<std::uint8_t> data = emptyProperties();
    appendU32(data, static_cast<std::uint32_t>(polygonCount)); // Num
    appendU32(data, static_cast<std::uint32_t>(polygonCount)); // Max
    for (std::int32_t index = 0; index < polygonCount; ++index) {
        const std::vector<std::uint8_t> polygon = onePolygon(vertexCount);
        data.insert(data.end(), polygon.begin(), polygon.end());
    }
    return data;
}

/// One mip. `widthOffset` is written verbatim so a test can make it disagree;
/// a value of 0 with version >= 63 means "fill in the correct one later".
void appendMip(std::vector<std::uint8_t>& into, std::uint16_t version,
               std::uint32_t widthOffset, const std::vector<std::uint8_t>& pixels,
               std::uint32_t width, std::uint32_t height) {
    if (version >= 63) {
        appendU32(into, widthOffset);
    }
    appendIndex(into, static_cast<std::int32_t>(pixels.size()));
    into.insert(into.end(), pixels.begin(), pixels.end());
    appendU32(into, width);
    appendU32(into, height);
    appendU8(into, 0);
    appendU8(into, 0);
}

/// The offset at which the first export's serialised bytes begin. The builder
/// lays a package out as header, then each export's bytes, then the tables --
/// so this is recovered by opening the package rather than assumed.
std::size_t firstExportOffset(const std::vector<std::uint8_t>& bytes) {
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());
    REQUIRE(package->exports().size() == 1);
    return package->exports()[0].serialOffset;
}

} // namespace

// --- Polys ------------------------------------------------------------------

TEST_CASE("a Polys export reads back the polygons that were written", "[upkg]") {
    const std::vector<std::uint8_t> bytes =
        packageWithObject(68, "Polys", polysData(2, 4));
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto polys = uta::upkg::readPolys(*package, package->exports()[0]);
    REQUIRE(polys.has_value());
    REQUIRE(polys->polygons.size() == 2);

    const uta::upkg::Polygon& first = polys->polygons[0];
    CHECK(first.vertices.size() == 4);
    CHECK(first.polyFlags == 0x20u);
    CHECK(first.itemName == static_cast<std::uint32_t>(NAME_ITEM));
    CHECK(first.link == 7);
    CHECK(first.brushPoly == 11);
    // Signed, and narrower than the compact index everything around it uses --
    // a reader that sign-extended from a wider read gets this wrong.
    CHECK(first.panU == -3);
    CHECK(first.panV == 5);
    CHECK(first.normal.z == 3.0F / 3.0F);
}

TEST_CASE("a Polys export with bytes left over is refused", "[upkg]") {
    // INV-1: the layout is right only when the reader ends exactly at the
    // export's end. One trailing byte is the smallest possible disagreement.
    std::vector<std::uint8_t> data = polysData(1, 3);
    data.push_back(0x00u);
    const std::vector<std::uint8_t> bytes = packageWithObject(68, "Polys", data);
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto polys = uta::upkg::readPolys(*package, package->exports()[0]);
    REQUIRE_FALSE(polys.has_value());
    CHECK(polys.error().code() == ErrorCode::MalformedData);
    CHECK(polys.error().message().find("unread") != std::string_view::npos);
}

TEST_CASE("a Polys declaring more polygons than the export can hold is refused",
          "[upkg]") {
    // INV-4: the count is checked against the bytes present before anything is
    // reserved from it.
    std::vector<std::uint8_t> data = emptyProperties();
    appendU32(data, 0x00FFFFFFu);
    appendU32(data, 0x00FFFFFFu);
    const std::vector<std::uint8_t> bytes = packageWithObject(68, "Polys", data);
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto polys = uta::upkg::readPolys(*package, package->exports()[0]);
    REQUIRE_FALSE(polys.has_value());
    CHECK(polys.error().code() == ErrorCode::MalformedData);
    CHECK(polys.error().message().find("more than the") != std::string_view::npos);
}

// --- Palette ----------------------------------------------------------------

TEST_CASE("a Palette export reads back its colours", "[upkg]") {
    std::vector<std::uint8_t> data = emptyProperties();
    appendIndex(data, 2);
    for (const std::uint8_t component : {1, 2, 3, 4, 5, 6, 7, 8}) {
        appendU8(data, component);
    }
    const std::vector<std::uint8_t> bytes = packageWithObject(68, "Palette", data);
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto palette = uta::upkg::readPalette(*package, package->exports()[0]);
    REQUIRE(palette.has_value());
    REQUIRE(palette->entries.size() == 2);
    CHECK(palette->entries[0].r == 1);
    CHECK(palette->entries[0].a == 4);
    CHECK(palette->entries[1].r == 5);
    CHECK(palette->entries[1].a == 8);
}

TEST_CASE("a Palette declaring a count larger than the file is refused", "[upkg]") {
    // INV-4, and the case UTA-0003's INV-2 is graded on: a four-byte edit
    // asking for gigabytes must be refused before anything is reserved.
    std::vector<std::uint8_t> data = emptyProperties();
    appendIndex(data, 0x00FFFFFF);
    const std::vector<std::uint8_t> bytes = packageWithObject(68, "Palette", data);
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto palette = uta::upkg::readPalette(*package, package->exports()[0]);
    REQUIRE_FALSE(palette.has_value());
    CHECK(palette.error().code() == ErrorCode::MalformedData);
    CHECK(palette.error().message().find("more than the") != std::string_view::npos);
}

// --- Texture ----------------------------------------------------------------

TEST_CASE("a version 61 texture has no WidthOffset field", "[upkg]") {
    // SS 2.1: stock content sits on both sides of the version 63 line, so the
    // older branch is exercised by real packages and must be exercised here.
    std::vector<std::uint8_t> data = emptyProperties();
    appendU8(data, 1);
    appendMip(data, 61, 0, {0xAAu, 0xBBu, 0xCCu, 0xDDu}, 2, 2);
    const std::vector<std::uint8_t> bytes = packageWithObject(61, "Texture", data);
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto texture = uta::upkg::readTexture(*package, package->exports()[0]);
    REQUIRE(texture.has_value());
    REQUIRE(texture->mips.size() == 1);
    CHECK(texture->mips[0].width == 2);
    CHECK(texture->mips[0].pixels.size() == 4);
    CHECK(texture->compressedMips.empty());
}

TEST_CASE("a version 68 texture's WidthOffset must name the end of its data",
          "[upkg]") {
    // INV-5. The offset is redundant, which is exactly why a reader is tempted
    // to read it and throw it away; checking it costs nothing and catches a
    // desynchronised chain.
    const std::vector<std::uint8_t> pixels{0x11u, 0x22u, 0x33u, 0x44u};

    // Two passes: the correct WidthOffset is an offset into the whole file, so
    // it cannot be known until the export's own offset is.
    std::vector<std::uint8_t> probe = emptyProperties();
    appendU8(probe, 1);
    appendMip(probe, 68, 0, pixels, 2, 2);
    const std::size_t serialOffset =
        firstExportOffset(packageWithObject(68, "Texture", probe));
    // Within the export: the property list, the mip count, the WidthOffset
    // field itself, and the size byte -- then the pixels.
    const auto correct = static_cast<std::uint32_t>(
        serialOffset + emptyProperties().size() + 1 + 4 + 1 + pixels.size());

    SECTION("the correct offset is accepted") {
        std::vector<std::uint8_t> data = emptyProperties();
        appendU8(data, 1);
        appendMip(data, 68, correct, pixels, 2, 2);
        const std::vector<std::uint8_t> bytes = packageWithObject(68, "Texture", data);
        const auto package = Package::open(asBytes(bytes));
        REQUIRE(package.has_value());
        const auto texture = uta::upkg::readTexture(*package, package->exports()[0]);
        REQUIRE(texture.has_value());
        CHECK(texture->mips.size() == 1);
    }

    SECTION("an offset one byte out is refused") {
        std::vector<std::uint8_t> data = emptyProperties();
        appendU8(data, 1);
        appendMip(data, 68, correct + 1, pixels, 2, 2);
        const std::vector<std::uint8_t> bytes = packageWithObject(68, "Texture", data);
        const auto package = Package::open(asBytes(bytes));
        REQUIRE(package.has_value());
        const auto texture = uta::upkg::readTexture(*package, package->exports()[0]);
        REQUIRE_FALSE(texture.has_value());
        CHECK(texture.error().code() == ErrorCode::MalformedData);
        CHECK(texture.error().message().find("WidthOffset") != std::string_view::npos);
    }
}

TEST_CASE("a texture's second chain is read if and only if bHasComp says so",
          "[upkg]") {
    // INV-6, and both directions matter: a reader that looks for a second
    // chain by trying to read one and seeing whether bytes remain succeeds by
    // accident on a texture that has none.
    const std::vector<std::uint8_t> pixels{0x01u, 0x02u, 0x03u, 0x04u};

    SECTION("bHasComp false: one chain, and compressedMips is empty") {
        TaggedPropertyWriter writer;
        writer.addBool(NAME_HASCOMP, false);
        std::vector<std::uint8_t> data = writer.build(NAME_NONE);
        appendU8(data, 1);
        appendMip(data, 61, 0, pixels, 2, 2);
        const std::vector<std::uint8_t> bytes = packageWithObject(61, "Texture", data);
        const auto package = Package::open(asBytes(bytes));
        REQUIRE(package.has_value());

        const auto texture = uta::upkg::readTexture(*package, package->exports()[0]);
        REQUIRE(texture.has_value());
        CHECK(texture->mips.size() == 1);
        CHECK(texture->compressedMips.empty());
    }

    SECTION("bHasComp true: both chains, and both are read") {
        TaggedPropertyWriter writer;
        writer.addBool(NAME_HASCOMP, true);
        std::vector<std::uint8_t> data = writer.build(NAME_NONE);
        appendU8(data, 1);
        appendMip(data, 61, 0, pixels, 2, 2);
        appendU8(data, 2);
        appendMip(data, 61, 0, pixels, 2, 2);
        appendMip(data, 61, 0, pixels, 1, 1);
        const std::vector<std::uint8_t> bytes = packageWithObject(61, "Texture", data);
        const auto package = Package::open(asBytes(bytes));
        REQUIRE(package.has_value());

        const auto texture = uta::upkg::readTexture(*package, package->exports()[0]);
        REQUIRE(texture.has_value());
        CHECK(texture->mips.size() == 1);
        CHECK(texture->compressedMips.size() == 2);
    }
}

TEST_CASE("a texture subclass sharing the layout is read; another is refused",
          "[upkg]") {
    // INV-8. WaveTexture shares the mip chain and then stores something
    // further that this item does not describe, so reading its mips and
    // leaving the rest unread would defeat SS 4.3 for every caller at once.
    std::vector<std::uint8_t> data = emptyProperties();
    appendU8(data, 1);
    appendMip(data, 61, 0, {0x09u}, 1, 1);

    SECTION("WetTexture is modelled") {
        const std::vector<std::uint8_t> bytes =
            packageWithObject(61, "WetTexture", data);
        const auto package = Package::open(asBytes(bytes));
        REQUIRE(package.has_value());
        CHECK(uta::upkg::readTexture(*package, package->exports()[0]).has_value());
    }

    SECTION("WaveTexture is refused, and the message names the class") {
        const std::vector<std::uint8_t> bytes =
            packageWithObject(61, "WaveTexture", data);
        const auto package = Package::open(asBytes(bytes));
        REQUIRE(package.has_value());
        const auto texture = uta::upkg::readTexture(*package, package->exports()[0]);
        REQUIRE_FALSE(texture.has_value());
        CHECK(texture.error().code() == ErrorCode::InvalidArgument);
        CHECK(texture.error().message().find("WaveTexture") != std::string_view::npos);
    }
}

// --- Sound ------------------------------------------------------------------

TEST_CASE("a version 61 sound has no NextOffset field", "[upkg]") {
    std::vector<std::uint8_t> data = emptyProperties();
    appendIndex(data, NAME_WAV);
    appendIndex(data, 3);
    for (const std::uint8_t value : {0x52u, 0x49u, 0x46u}) {
        appendU8(data, value);
    }
    const std::vector<std::uint8_t> bytes = packageWithObject(61, "Sound", data);
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto sound = uta::upkg::readSound(*package, package->exports()[0]);
    REQUIRE(sound.has_value());
    CHECK(sound->formatName == static_cast<std::uint32_t>(NAME_WAV));
    CHECK(sound->data.size() == 3);
}

TEST_CASE("a version 68 sound's NextOffset must name the end of its data",
          "[upkg]") {
    // INV-5's other half, and the symmetry is the point: the same redundant
    // field in a different reader, checked the same way.
    const std::vector<std::uint8_t> payload{0x52u, 0x49u, 0x46u, 0x46u};

    std::vector<std::uint8_t> probe = emptyProperties();
    appendIndex(probe, NAME_WAV);
    appendU32(probe, 0);
    appendIndex(probe, static_cast<std::int32_t>(payload.size()));
    probe.insert(probe.end(), payload.begin(), payload.end());
    const std::size_t serialOffset =
        firstExportOffset(packageWithObject(68, "Sound", probe));
    const auto correct = static_cast<std::uint32_t>(
        serialOffset + emptyProperties().size() + 1 + 4 + 1 + payload.size());

    SECTION("the correct offset is accepted") {
        std::vector<std::uint8_t> data = emptyProperties();
        appendIndex(data, NAME_WAV);
        appendU32(data, correct);
        appendIndex(data, static_cast<std::int32_t>(payload.size()));
        data.insert(data.end(), payload.begin(), payload.end());
        const std::vector<std::uint8_t> bytes = packageWithObject(68, "Sound", data);
        const auto package = Package::open(asBytes(bytes));
        REQUIRE(package.has_value());
        CHECK(uta::upkg::readSound(*package, package->exports()[0]).has_value());
    }

    SECTION("an offset one byte out is refused") {
        std::vector<std::uint8_t> data = emptyProperties();
        appendIndex(data, NAME_WAV);
        appendU32(data, correct + 1);
        appendIndex(data, static_cast<std::int32_t>(payload.size()));
        data.insert(data.end(), payload.begin(), payload.end());
        const std::vector<std::uint8_t> bytes = packageWithObject(68, "Sound", data);
        const auto package = Package::open(asBytes(bytes));
        REQUIRE(package.has_value());
        const auto sound = uta::upkg::readSound(*package, package->exports()[0]);
        REQUIRE_FALSE(sound.has_value());
        CHECK(sound.error().message().find("NextOffset") != std::string_view::npos);
    }
}

TEST_CASE("a sound declaring more bytes than the export holds is refused",
          "[upkg]") {
    std::vector<std::uint8_t> data = emptyProperties();
    appendIndex(data, NAME_WAV);
    appendIndex(data, 0x00FFFFFF);
    const std::vector<std::uint8_t> bytes = packageWithObject(61, "Sound", data);
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto sound = uta::upkg::readSound(*package, package->exports()[0]);
    REQUIRE_FALSE(sound.has_value());
    CHECK(sound.error().code() == ErrorCode::MalformedData);
}

// --- The view rule ----------------------------------------------------------

TEST_CASE("mip pixels and sound payload are views into the caller's bytes",
          "[upkg]") {
    // INV-7, and it is not observable from behaviour: a reader that copied
    // into a vector would satisfy every other test in this file. Pointer
    // identity against the input buffer is what separates them.
    SECTION("a mip's pixels") {
        std::vector<std::uint8_t> data = emptyProperties();
        appendU8(data, 1);
        appendMip(data, 61, 0, {0x7Au, 0x7Bu}, 1, 1);
        const std::vector<std::uint8_t> bytes = packageWithObject(61, "Texture", data);
        const auto package = Package::open(asBytes(bytes));
        REQUIRE(package.has_value());
        const auto texture = uta::upkg::readTexture(*package, package->exports()[0]);
        REQUIRE(texture.has_value());
        REQUIRE(texture->mips.size() == 1);

        const auto* base = reinterpret_cast<const std::byte*>(bytes.data());
        CHECK(texture->mips[0].pixels.data() >= base);
        CHECK(texture->mips[0].pixels.data() < base + bytes.size());
    }

    SECTION("a sound's payload") {
        std::vector<std::uint8_t> data = emptyProperties();
        appendIndex(data, NAME_WAV);
        appendIndex(data, 2);
        appendU8(data, 0x5Au);
        appendU8(data, 0x5Bu);
        const std::vector<std::uint8_t> bytes = packageWithObject(61, "Sound", data);
        const auto package = Package::open(asBytes(bytes));
        REQUIRE(package.has_value());
        const auto sound = uta::upkg::readSound(*package, package->exports()[0]);
        REQUIRE(sound.has_value());

        const auto* base = reinterpret_cast<const std::byte*>(bytes.data());
        CHECK(sound->data.data() >= base);
        CHECK(sound->data.data() < base + bytes.size());
    }
}

// --- Level ------------------------------------------------------------------
//
// docs/specs/UTA-0057-level-tail-and-reachspecs.md SS 7 tier 1. These cases
// cover that spec's INV-1 and INV-4, UTA-0004's INV-9 (whose fixture case is
// owed here because this is the first item to implement the actor array), and
// UTA-0004's INV-1 at fixture level. None can be exercised by real content:
// the install supplies no truncated array and no known-wrong length.

namespace {

using uta::test::LevelExportWriter;

/// A level with a small actor array interleaving null and non-null slots, and
/// a reach-spec array whose entries are distinguishable by their distance.
LevelExportWriter levelWithSpecs(int specCount) {
    LevelExportWriter writer;
    writer.setProperties(emptyProperties());
    // Interleaved, so a reader that compacted the array or counted wrongly
    // disagrees on both members -- UTA-0004 INV-9.
    writer.addActor(1).addActor(0).addActor(2).addActor(0).addActor(0).addActor(3);
    writer.setURL("unreal", "host", "CTF-Fixture.unr", "portal", {"Game=Fixture", "Mutator=None"},
                  7777, 1);
    writer.setModel(9);
    for (int index = 0; index < specCount; ++index) {
        // distance is the label: position i in the file carries 1000 + i.
        writer.addReachSpec(1000 + index, 20 + index, 40 + index, 50 + index, 70 + index,
                            index, static_cast<std::uint8_t>(index % 2));
    }
    // The one trailer slot real content ever fills carries a TextBuffer
    // reference. Set here so a reader that stops before it, or reads it as a
    // fixed-width field, ends in the wrong place.
    writer.setTrailerFloat(24.17F).setTrailerIndex(7, 3000);
    return writer;
}

} // namespace

TEST_CASE("a Level reads back its actors, its slot count and its reach specs", "[upkg]") {
    const std::vector<std::uint8_t> bytes =
        packageWithObject(68, "Level", levelWithSpecs(10).build());
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto level = uta::upkg::readLevel(*package, package->exports()[0]);
    REQUIRE(level.has_value());

    // UTA-0004 INV-9: the null slots are counted, not returned.
    CHECK(level->rawSlotCount == 6u);
    REQUIRE(level->actors.size() == 3);
    CHECK(level->actors[0].raw() == 1);
    CHECK(level->actors[1].raw() == 2);
    CHECK(level->actors[2].raw() == 3);

    // INV-1: file order and file indexing, nothing dropped or renumbered. The
    // positions checked are spread across the array, so a reader that lost or
    // reordered one entry disagrees at the ones after it.
    REQUIRE(level->reachSpecs.size() == 10);
    CHECK(level->reachSpecs[0].distance == 1000);
    CHECK(level->reachSpecs[4].distance == 1004);
    CHECK(level->reachSpecs[9].distance == 1009);

    // The two fields the graph rests on, and the two nothing else checks --
    // SS 4.3 says the collision pair is graded by no invariant, so a fixture
    // is the only place their order is pinned at all.
    const uta::upkg::ReachSpec& fifth = level->reachSpecs[4];
    CHECK(fifth.start.raw() == 24);
    CHECK(fifth.end.raw() == 44);
    CHECK(fifth.collisionRadius == 54);
    CHECK(fifth.collisionHeight == 74);
    CHECK(fifth.reachFlags == 4);
    CHECK(fifth.pruned == 0u);
    CHECK(level->reachSpecs[5].pruned == 1u);
}

TEST_CASE("a Level stating a zero-length reach-spec array succeeds with an empty one",
          "[upkg]") {
    // INV-4: an empty array is returned when the FILE states one, and this is
    // the case that distinguishes that from a reader giving up.
    const std::vector<std::uint8_t> bytes =
        packageWithObject(68, "Level", levelWithSpecs(0).build());
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto level = uta::upkg::readLevel(*package, package->exports()[0]);
    REQUIRE(level.has_value());
    CHECK(level->reachSpecs.empty());
    CHECK(level->rawSlotCount == 6u);
}

TEST_CASE("a Level declaring more reach specs than the export can hold is refused",
          "[upkg]") {
    // INV-4's breaking case: the count is checked against the bytes present
    // before anything is reserved from it. A reader that instead treated the
    // short array as "no paths" and seeked to the end would consume its export
    // exactly and report an unpathed level, which UTA-0004 INV-1 cannot see.
    LevelExportWriter writer = levelWithSpecs(0);
    writer.setReachSpecCountOverride(0x00FFFFFF);
    const std::vector<std::uint8_t> bytes = packageWithObject(68, "Level", writer.build());
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto level = uta::upkg::readLevel(*package, package->exports()[0]);
    REQUIRE_FALSE(level.has_value());
    CHECK(level.error().code() == ErrorCode::MalformedData);
    CHECK(level.error().message().find("more than the") != std::string_view::npos);
}

TEST_CASE("a Level declaring more actor slots than the export can hold is refused",
          "[upkg]") {
    LevelExportWriter writer = levelWithSpecs(2);
    writer.setActorSlotCountOverride(0x00FFFFFF);
    const std::vector<std::uint8_t> bytes = packageWithObject(68, "Level", writer.build());
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto level = uta::upkg::readLevel(*package, package->exports()[0]);
    REQUIRE_FALSE(level.has_value());
    CHECK(level.error().code() == ErrorCode::MalformedData);
    CHECK(level.error().message().find("more than the") != std::string_view::npos);
}

TEST_CASE("a Level whose trailer ends in extra ZERO bytes is read", "[upkg]") {
    // The shape one map in the reference install carries: one more zero byte
    // than every other map. It is not explained, so it is not modelled as a
    // field -- what the reader requires is that the run be zero.
    LevelExportWriter writer = levelWithSpecs(3);
    writer.addTrailerByte(0).addTrailerByte(0);
    const std::vector<std::uint8_t> bytes = packageWithObject(68, "Level", writer.build());
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto level = uta::upkg::readLevel(*package, package->exports()[0]);
    REQUIRE(level.has_value());
    CHECK(level->reachSpecs.size() == 3);
}

TEST_CASE("a Level whose trailer ends in a NON-zero byte is refused", "[upkg]") {
    // UTA-0004 INV-1 at fixture level. The zero-run tolerance above is exactly
    // as wide as the content needs and no wider: a byte carrying a value is a
    // layout this reader does not describe, and is refused rather than
    // skipped to reach the export's end.
    LevelExportWriter writer = levelWithSpecs(3);
    writer.addTrailerByte(0).addTrailerByte(0x7Fu);
    const std::vector<std::uint8_t> bytes = packageWithObject(68, "Level", writer.build());
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto level = uta::upkg::readLevel(*package, package->exports()[0]);
    REQUIRE_FALSE(level.has_value());
    CHECK(level.error().code() == ErrorCode::MalformedData);
    CHECK(level.error().message().find("not zero") != std::string_view::npos);
}

TEST_CASE("a Level export with no serialised data is refused", "[upkg]") {
    // INV-4 again, in the shape a sizeless export takes: an export with no
    // bytes states no array, so returning an empty one would be the reader
    // giving up. This is where readLevel departs from readPolys, which returns
    // an empty result for a sizeless export.
    const std::vector<std::uint8_t> bytes = packageWithObject(68, "Level", {});
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto level = uta::upkg::readLevel(*package, package->exports()[0]);
    REQUIRE_FALSE(level.has_value());
    CHECK(level.error().code() == ErrorCode::MalformedData);
    CHECK(level.error().message().find("no serialised data") != std::string_view::npos);
}
