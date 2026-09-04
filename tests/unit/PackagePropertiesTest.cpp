// The tagged property list an object's serialised data begins with.
//
// Locks INV-9 (the list ends at None and at no other condition), INV-10 (a
// Bool takes its value from the info byte and consumes no value bytes),
// INV-11 (an undecodable property is carried through rather than aborting the
// list) and INV-12 (an execution-stack frame is skipped before the list).
//
// Every case here puts the property under test BETWEEN two known ones. That
// is deliberate: the failures this file is about desynchronise the cursor
// rather than raising an error, so what proves the cursor is where it should
// be is the property after it reading correctly.

#include "support/UnrealPackageBuilder.h"
#include "upkg/Package.h"
#include "upkg/Properties.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

using uta::ErrorCode;
using uta::test::asBytes;
using uta::test::ExportEntry;
using uta::test::OBJECT_FLAG_HAS_STACK;
using uta::test::TaggedPropertyWriter;
using uta::test::UnrealPackageBuilder;
using uta::upkg::Package;
using uta::upkg::Property;
using uta::upkg::PropertyType;
using uta::upkg::readProperties;

namespace {

// The name table every fixture below starts with, by index.
constexpr std::int32_t NAME_NONE = 0;
constexpr std::int32_t NAME_FIRST = 1;
constexpr std::int32_t NAME_MIDDLE = 2;
constexpr std::int32_t NAME_LAST = 3;
constexpr std::int32_t NAME_STRUCT = 4;
constexpr std::int32_t NAME_CLASS = 5;

UnrealPackageBuilder packageWithNames() {
    UnrealPackageBuilder builder;
    builder.addName("None")
        .addName("FirstProperty")
        .addName("MiddleProperty")
        .addName("LastProperty")
        .addName("InventedStruct")
        .addName("Class");
    return builder;
}

/// A package holding one export whose serialised data is `data`.
UnrealPackageBuilder packageWithObject(const std::vector<std::uint8_t>& data,
                                       std::uint32_t objectFlags = 0) {
    UnrealPackageBuilder builder = packageWithNames();
    ExportEntry entry;
    entry.objectName = NAME_FIRST;
    entry.objectClass = -1; // an import, so this is not a class export
    entry.objectFlags = objectFlags;
    entry.serialData = data;
    builder.addExport(entry);

    uta::test::ImportEntry import;
    import.classPackage = NAME_NONE;
    import.className = NAME_CLASS;
    import.objectName = NAME_FIRST;
    builder.addImport(import);
    return builder;
}

} // namespace

TEST_CASE("a property list reads back the values that were written",
          "[package-properties]") {
    TaggedPropertyWriter writer;
    writer.addInt(NAME_FIRST, -12345)
        .addFloat(NAME_MIDDLE, 0.5F)
        .addByte(NAME_LAST, 200)
        .addStr(NAME_FIRST, "DM-Deck16")
        .addVector(NAME_MIDDLE, 1.0F, -2.0F, 3.5F)
        .addRotator(NAME_LAST, 16384, -16384, 0);

    const std::vector<std::uint8_t> bytes =
        packageWithObject(writer.build(NAME_NONE)).build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto properties = readProperties(*package, package->exports()[0]);
    REQUIRE(properties.has_value());
    REQUIRE(properties->size() == 6);

    CHECK((*properties)[0].type == PropertyType::Int);
    CHECK(std::get<std::int32_t>((*properties)[0].value) == -12345);

    CHECK((*properties)[1].type == PropertyType::Float);
    CHECK(std::get<float>((*properties)[1].value) == 0.5F);

    CHECK((*properties)[2].type == PropertyType::Byte);
    CHECK(std::get<std::uint8_t>((*properties)[2].value) == 200);

    CHECK((*properties)[3].type == PropertyType::Str);
    CHECK(std::get<std::string>((*properties)[3].value) == "DM-Deck16");

    CHECK((*properties)[4].type == PropertyType::Vector);
    const auto vector = std::get<uta::upkg::Vector3>((*properties)[4].value);
    CHECK(vector.x == 1.0F);
    CHECK(vector.y == -2.0F);
    CHECK(vector.z == 3.5F);

    CHECK((*properties)[5].type == PropertyType::Rotator);
    const auto rotator = std::get<uta::upkg::Rotator>((*properties)[5].value);
    CHECK(rotator.pitch == 16384);
    CHECK(rotator.yaw == -16384);
    CHECK(rotator.roll == 0);
}

TEST_CASE("an export with no serialised data has no property list",
          "[package-properties]") {
    const std::vector<std::uint8_t> bytes = packageWithObject({}).build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto properties = readProperties(*package, package->exports()[0]);
    REQUIRE(properties.has_value());
    CHECK(properties->empty());
}

TEST_CASE("a list with its terminator removed is malformed", "[package-properties]") {
    // INV-9. The failure this stops is a truncated object reading as a
    // complete one: a loop that stops at the end of the buffer returns what it
    // has and reports success.
    TaggedPropertyWriter writer;
    writer.addInt(NAME_FIRST, 1).addInt(NAME_LAST, 2);

    const std::vector<std::uint8_t> bytes =
        packageWithObject(writer.buildWithoutTerminator()).build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto properties = readProperties(*package, package->exports()[0]);
    REQUIRE_FALSE(properties.has_value());
    CHECK(properties.error().code() == ErrorCode::MalformedData);
}

TEST_CASE("a Bool takes its value from the info byte and consumes no value bytes",
          "[package-properties]") {
    // INV-10. Its size byte is still consumed -- every Bool tag in real
    // content carries size code 5. Skip that byte and the property after the
    // Bool reads the wrong bytes, in a list that still parses.
    TaggedPropertyWriter writer;
    writer.addInt(NAME_FIRST, 777).addBool(NAME_MIDDLE, true).addInt(NAME_LAST, -777);

    const std::vector<std::uint8_t> bytes =
        packageWithObject(writer.build(NAME_NONE)).build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto properties = readProperties(*package, package->exports()[0]);
    REQUIRE(properties.has_value());
    REQUIRE(properties->size() == 3);

    CHECK(std::get<std::int32_t>((*properties)[0].value) == 777);

    CHECK((*properties)[1].type == PropertyType::Bool);
    CHECK(std::get<bool>((*properties)[1].value) == true);

    // The one that proves the cursor survived the Bool.
    CHECK((*properties)[2].type == PropertyType::Int);
    CHECK(std::get<std::int32_t>((*properties)[2].value) == -777);
}

TEST_CASE("a false Bool is a value, not an absent one", "[package-properties]") {
    TaggedPropertyWriter writer;
    writer.addBool(NAME_MIDDLE, false).addInt(NAME_LAST, 5);

    const std::vector<std::uint8_t> bytes =
        packageWithObject(writer.build(NAME_NONE)).build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto properties = readProperties(*package, package->exports()[0]);
    REQUIRE(properties.has_value());
    REQUIRE(properties->size() == 2);

    CHECK_FALSE(std::holds_alternative<std::monostate>((*properties)[0].value));
    CHECK(std::get<bool>((*properties)[0].value) == false);
    CHECK(std::get<std::int32_t>((*properties)[1].value) == 5);
}

TEST_CASE("an undecodable struct is carried through and parsing continues",
          "[package-properties]") {
    // INV-11. A struct's members are serialised without tags, so its layout is
    // only knowable from the class table (UTA-0005). The tag's size field is
    // what makes carrying it through safe.
    const std::vector<std::uint8_t> opaque{0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03};

    TaggedPropertyWriter writer;
    writer.addInt(NAME_FIRST, 11)
        .addUndecodedStruct(NAME_MIDDLE, NAME_STRUCT, opaque)
        .addInt(NAME_LAST, 22);

    const std::vector<std::uint8_t> bytes =
        packageWithObject(writer.build(NAME_NONE)).build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto properties = readProperties(*package, package->exports()[0]);
    REQUIRE(properties.has_value());
    REQUIRE(properties->size() == 3);

    const Property& carried = (*properties)[1];
    CHECK(carried.type == PropertyType::Struct);
    CHECK(carried.structNameIndex == static_cast<std::uint32_t>(NAME_STRUCT));

    const auto raw = std::get<std::span<const std::byte>>(carried.value);
    REQUIRE(raw.size() == opaque.size());
    for (std::size_t i = 0; i < opaque.size(); ++i) {
        CHECK(static_cast<std::uint8_t>(raw[i]) == opaque[i]);
    }

    // Skipping by a guessed width rather than the declared size is what this
    // third property catches.
    CHECK(std::get<std::int32_t>((*properties)[2].value) == 22);
}

TEST_CASE("an execution-stack frame is skipped before the property list",
          "[package-properties]") {
    // INV-12. Ignore the frame and the first property name is read out of the
    // frame's bytes, which usually decodes as some other name rather than
    // failing. So the two lists are identical and only the flag differs.
    TaggedPropertyWriter plain;
    plain.addInt(NAME_FIRST, 4242).addBool(NAME_MIDDLE, true).addInt(NAME_LAST, -1);

    TaggedPropertyWriter withStack;
    withStack.addInt(NAME_FIRST, 4242).addBool(NAME_MIDDLE, true).addInt(NAME_LAST, -1);
    withStack.setStackFrame(-1, -1, 0x00FF00FF00FF00FFLL, 7, 128);

    const std::vector<std::uint8_t> plainBytes =
        packageWithObject(plain.build(NAME_NONE)).build();
    const std::vector<std::uint8_t> stackBytes =
        packageWithObject(withStack.build(NAME_NONE), OBJECT_FLAG_HAS_STACK).build();

    const auto plainPackage = Package::open(asBytes(plainBytes));
    const auto stackPackage = Package::open(asBytes(stackBytes));
    REQUIRE(plainPackage.has_value());
    REQUIRE(stackPackage.has_value());

    const auto plainProperties = readProperties(*plainPackage, plainPackage->exports()[0]);
    const auto stackProperties = readProperties(*stackPackage, stackPackage->exports()[0]);
    REQUIRE(plainProperties.has_value());
    REQUIRE(stackProperties.has_value());

    REQUIRE(plainProperties->size() == stackProperties->size());
    for (std::size_t i = 0; i < plainProperties->size(); ++i) {
        CHECK((*plainProperties)[i].nameIndex == (*stackProperties)[i].nameIndex);
        CHECK((*plainProperties)[i].type == (*stackProperties)[i].type);
    }
    CHECK(std::get<std::int32_t>((*stackProperties)[0].value) == 4242);
    CHECK(std::get<std::int32_t>((*stackProperties)[2].value) == -1);
}

TEST_CASE("a stack frame with a null node carries no trailing offset",
          "[package-properties]") {
    // The offset is present only when the first reference is non-null. Read it
    // unconditionally and the list starts one index too late.
    TaggedPropertyWriter writer;
    writer.addInt(NAME_FIRST, 9).addInt(NAME_LAST, 10);
    writer.setStackFrame(0, 0, 0, 0, 0);

    const std::vector<std::uint8_t> bytes =
        packageWithObject(writer.build(NAME_NONE), OBJECT_FLAG_HAS_STACK).build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto properties = readProperties(*package, package->exports()[0]);
    REQUIRE(properties.has_value());
    REQUIRE(properties->size() == 2);
    CHECK(std::get<std::int32_t>((*properties)[0].value) == 9);
    CHECK(std::get<std::int32_t>((*properties)[1].value) == 10);
}

TEST_CASE("a non-zero array index is read in its leading-marker encoding",
          "[package-properties]") {
    // The one place the format is not little-endian: the marker bits lead and
    // the value's high bits follow them. All three widths, each between two
    // properties that pin the cursor.
    TaggedPropertyWriter writer;
    writer.addInt(NAME_FIRST, 1)
        .addIntAt(NAME_MIDDLE, 5, 100)
        .addIntAt(NAME_MIDDLE, 300, 200)
        .addIntAt(NAME_MIDDLE, 100000, 300)
        .addInt(NAME_LAST, 2);

    const std::vector<std::uint8_t> bytes =
        packageWithObject(writer.build(NAME_NONE)).build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto properties = readProperties(*package, package->exports()[0]);
    REQUIRE(properties.has_value());
    REQUIRE(properties->size() == 5);

    CHECK((*properties)[1].arrayIndex == 5);
    CHECK((*properties)[2].arrayIndex == 300);
    CHECK((*properties)[3].arrayIndex == 100000);
    CHECK(std::get<std::int32_t>((*properties)[3].value) == 300);
    CHECK(std::get<std::int32_t>((*properties)[4].value) == 2);
}

TEST_CASE("a class export has no property list and is refused", "[package-properties]") {
    // A class export is recognised by a NULL class reference -- not by one
    // naming `Class`, which no package writes. Class objects are UTA-0005.
    TaggedPropertyWriter writer;
    writer.addInt(NAME_FIRST, 1);

    UnrealPackageBuilder builder = packageWithNames();
    ExportEntry entry;
    entry.objectName = NAME_FIRST;
    entry.objectClass = 0; // null: this export IS a class
    entry.serialData = writer.build(NAME_NONE);
    builder.addExport(entry);

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto properties = readProperties(*package, package->exports()[0]);
    REQUIRE_FALSE(properties.has_value());
    CHECK(properties.error().code() == ErrorCode::InvalidArgument);
}
