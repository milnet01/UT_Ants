// Opening a package: the header gate, and the two table shapes a reader gets
// wrong in ways that still parse.
//
// Locks INV-5 (signature and version), INV-6 (every name index and object
// reference is in range once open succeeds) and INV-7 (a sizeless export
// carries no serial offset).

#include "support/UnrealPackageBuilder.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using uta::ErrorCode;
using uta::test::asBytes;
using uta::test::ExportEntry;
using uta::test::ImportEntry;
using uta::test::UnrealPackageBuilder;
using uta::upkg::ObjectReference;
using uta::upkg::ObjectReferenceKind;
using uta::upkg::Package;

namespace {

// Name indices into the table every fixture below starts with.
constexpr std::int32_t NAME_NONE = 0;
constexpr std::int32_t NAME_CORE = 1;
constexpr std::int32_t NAME_BRUSH = 2;

UnrealPackageBuilder minimalPackage() {
    UnrealPackageBuilder builder;
    builder.addName("None").addName("Core").addName("Brush");
    return builder;
}

} // namespace

TEST_CASE("a package with the wrong signature is malformed", "[package-reader]") {
    UnrealPackageBuilder builder = minimalPackage();
    builder.overrideSignature(0xDEADBEEFu);

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));

    REQUIRE_FALSE(package.has_value());
    CHECK(package.error().code() == ErrorCode::MalformedData);
}

TEST_CASE("a package shorter than a header is malformed", "[package-reader]") {
    const std::vector<std::uint8_t> bytes{0xC1, 0x83, 0x2A, 0x9E, 68};
    const auto package = Package::open(asBytes(bytes));

    REQUIRE_FALSE(package.has_value());
    CHECK(package.error().code() == ErrorCode::MalformedData);
}

TEST_CASE("a package version outside 61 to 69 is unsupported", "[package-reader]") {
    // 76 is not hypothetical: section 2.1 found one in the reference install,
    // and a reader that only bounds the version below parses its UE2-era tail
    // as garbage instead of refusing it.
    for (std::uint16_t version : {std::uint16_t{60}, std::uint16_t{70}, std::uint16_t{76}}) {
        UnrealPackageBuilder builder = minimalPackage();
        builder.setPackageVersion(version);

        const std::vector<std::uint8_t> bytes = builder.build();
        const auto package = Package::open(asBytes(bytes));

        REQUIRE_FALSE(package.has_value());
        CHECK(package.error().code() == ErrorCode::UnsupportedVersion);
    }
}

TEST_CASE("the supported version range opens at both ends", "[package-reader]") {
    for (std::uint16_t version : {std::uint16_t{61}, std::uint16_t{68}, std::uint16_t{69}}) {
        UnrealPackageBuilder builder = minimalPackage();
        builder.setPackageVersion(version);

        const std::vector<std::uint8_t> bytes = builder.build();
        const auto package = Package::open(asBytes(bytes));

        REQUIRE(package.has_value());
        CHECK(package->header().packageVersion == version);
    }
}

TEST_CASE("a pre-64 package's names are null-terminated without a length", "[package-reader]") {
    // Section 2.1 found real content below 64, mostly textures and music.
    UnrealPackageBuilder builder = minimalPackage();
    builder.setPackageVersion(62);

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));

    REQUIRE(package.has_value());
    REQUIRE(package->names().size() == 3);
    const auto name = package->name(NAME_CORE);
    REQUIRE(name.has_value());
    CHECK(*name == "Core");
}

TEST_CASE("the name table reads back the names that were written", "[package-reader]") {
    const std::vector<std::uint8_t> bytes = minimalPackage().build();
    const auto package = Package::open(asBytes(bytes));

    REQUIRE(package.has_value());
    REQUIRE(package->names().size() == 3);

    const auto none = package->name(NAME_NONE);
    REQUIRE(none.has_value());
    CHECK(*none == "None");

    const auto brush = package->name(NAME_BRUSH);
    REQUIRE(brush.has_value());
    CHECK(*brush == "Brush");

    // Past the end is an error rather than a silent empty string.
    CHECK_FALSE(package->name(3).has_value());
}

TEST_CASE("an export naming a name past the table is refused at open", "[package-reader]") {
    // The failure INV-6 exists to stop is a package that opens and hands the
    // out-of-range index to whichever caller indexes exports() directly.
    UnrealPackageBuilder builder = minimalPackage();
    ExportEntry entry;
    entry.objectName = 99; // only three names exist
    builder.addExport(entry);

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));

    REQUIRE_FALSE(package.has_value());
    CHECK(package.error().code() == ErrorCode::MalformedData);
}

TEST_CASE("an import naming a name past the table is refused at open", "[package-reader]") {
    UnrealPackageBuilder builder = minimalPackage();
    ImportEntry entry;
    entry.classPackage = NAME_CORE;
    entry.className = NAME_BRUSH;
    entry.objectName = 42; // only three names exist
    builder.addImport(entry);

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));

    REQUIRE_FALSE(package.has_value());
    CHECK(package.error().code() == ErrorCode::MalformedData);
}

TEST_CASE("an object reference past the table it names is refused at open",
          "[package-reader]") {
    UnrealPackageBuilder builder = minimalPackage();
    ExportEntry entry;
    entry.objectName = NAME_BRUSH;
    entry.objectClass = 7; // export index 6, and there is one export
    builder.addExport(entry);

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));

    REQUIRE_FALSE(package.has_value());
    CHECK(package.error().code() == ErrorCode::MalformedData);
}

TEST_CASE("an object reference resolves to the table its sign names", "[package-reader]") {
    UnrealPackageBuilder builder = minimalPackage();

    ImportEntry import;
    import.classPackage = NAME_CORE;
    import.className = NAME_BRUSH;
    import.objectName = NAME_CORE;
    builder.addImport(import);

    ExportEntry entry;
    entry.objectName = NAME_BRUSH;
    entry.objectClass = -1; // the first import
    builder.addExport(entry);

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));

    REQUIRE(package.has_value());
    REQUIRE(package->exports().size() == 1);

    const ObjectReference reference = package->exports()[0].objectClass;
    CHECK(reference.kind() == ObjectReferenceKind::Import);
    CHECK(reference.index() == 0);

    // A null reference names no object, and "None" is what the format calls
    // that -- not an error and not an empty string.
    const auto nullName = package->objectName(ObjectReference{0});
    REQUIRE(nullName.has_value());
    CHECK(*nullName == "None");

    const auto className = package->objectName(reference);
    REQUIRE(className.has_value());
    CHECK(*className == "Core");
}

TEST_CASE("a sizeless export does not consume a serial offset", "[package-reader]") {
    // INV-7's breaking case: read the offset unconditionally and the export
    // table desynchronises from the first sizeless entry onward. Every entry
    // after it is wrong, and the package still parses without error -- so the
    // third export's bytes are the only thing that shows it.
    const std::vector<std::uint8_t> firstData{0x11, 0x22, 0x33, 0x44};
    const std::vector<std::uint8_t> thirdData{0xAA, 0xBB, 0xCC, 0xDD, 0xEE};

    UnrealPackageBuilder builder = minimalPackage();

    ExportEntry first;
    first.objectName = NAME_BRUSH;
    first.serialData = firstData;
    builder.addExport(first);

    ExportEntry sizeless;
    sizeless.objectName = NAME_CORE; // no serialData at all
    builder.addExport(sizeless);

    ExportEntry third;
    third.objectName = NAME_BRUSH;
    third.serialData = thirdData;
    builder.addExport(third);

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));

    REQUIRE(package.has_value());
    REQUIRE(package->exports().size() == 3);

    CHECK(package->exports()[1].serialSize == 0);
    CHECK(package->exports()[1].serialOffset == 0);

    const auto empty = package->serialBytes(package->exports()[1]);
    REQUIRE(empty.has_value());
    CHECK(empty->empty());

    const auto data = package->serialBytes(package->exports()[2]);
    REQUIRE(data.has_value());
    REQUIRE(data->size() == thirdData.size());
    for (std::size_t i = 0; i < thirdData.size(); ++i) {
        CHECK(static_cast<std::uint8_t>((*data)[i]) == thirdData[i]);
    }
}
