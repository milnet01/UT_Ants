// Reading one class export.
//
// Locks INV-2 (the parent comes from the export table, which the container has
// already validated, and readClass performs no reference validation of its
// own) and INV-10 (InvalidArgument for an export that is not a class, and for
// one with no serialised data).
//
// Also covers the branch no real content takes: every class export in the
// reference install sits at package version 68 or 69, so SS 4.3 step 10's
// "version 62 and above" condition is true for all of them and its false arm
// is exercised here and nowhere else.

#include "support/UnrealPackageBuilder.h"
#include "upkg/Class.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using uta::ErrorCode;
using uta::test::asBytes;
using uta::test::ClassExportWriter;
using uta::test::ExportEntry;
using uta::test::TaggedPropertyWriter;
using uta::test::UnrealPackageBuilder;
using uta::upkg::ObjectReferenceKind;
using uta::upkg::Package;
using uta::upkg::readClass;

namespace {

constexpr std::int32_t NAME_NONE = 0;
constexpr std::int32_t NAME_BASE = 1;
constexpr std::int32_t NAME_DERIVED = 2;
constexpr std::int32_t NAME_HEALTH = 3;
constexpr std::int32_t NAME_CONFIG = 4;

/// A package holding two classes: Base with no parent, and Derived whose
/// export-table `super` names Base. `derivedSuperField` is the IN-DATA parent,
/// written independently so the two records can be made to disagree.
std::vector<std::uint8_t> twoClassPackage(std::int32_t derivedSuperField,
                                          std::uint16_t version = 68) {
    UnrealPackageBuilder builder;
    builder.setPackageVersion(version);
    builder.addName("None");
    builder.addName("Base");
    builder.addName("Derived");
    builder.addName("Health");
    builder.addName("DefaultConfig");

    ClassExportWriter base;
    base.setFriendlyName(NAME_BASE)
        .setConfigName(NAME_CONFIG)
        .setDefaults(TaggedPropertyWriter{}.addInt(NAME_HEALTH, 100).build(NAME_NONE));

    ClassExportWriter derived;
    derived.setSuperField(derivedSuperField)
        .setFriendlyName(NAME_DERIVED)
        .setConfigName(NAME_CONFIG)
        .setDefaults(TaggedPropertyWriter{}.addInt(NAME_HEALTH, 250).build(NAME_NONE));

    ExportEntry baseEntry;
    baseEntry.objectClass = 0; // null: this is what makes it a class export
    baseEntry.objectName = NAME_BASE;
    baseEntry.serialData = base.build(version);
    builder.addExport(baseEntry);

    ExportEntry derivedEntry;
    derivedEntry.objectClass = 0;
    derivedEntry.super = 1; // the export at index 0, which is Base
    derivedEntry.objectName = NAME_DERIVED;
    derivedEntry.serialData = derived.build(version);
    builder.addExport(derivedEntry);

    return builder.build();
}

} // namespace

TEST_CASE("a class export reads its own fields and its own defaults") {
    const std::vector<std::uint8_t> bytes = twoClassPackage(1);
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto info = readClass(*package, package->exports()[1]);
    REQUIRE(info.has_value());

    CHECK(info->friendlyName == static_cast<std::uint32_t>(NAME_DERIVED));
    CHECK(info->configName == static_cast<std::uint32_t>(NAME_CONFIG));
    REQUIRE(info->defaults.size() == 1);
    // The class's OWN defaults, not the effective set: what the file holds is
    // a difference against the parent, and merging is effectiveDefaults' job.
    CHECK(info->defaults[0].nameIndex == static_cast<std::uint32_t>(NAME_HEALTH));
}

TEST_CASE("INV-2: the parent comes from the export table, not from the in-data field") {
    // The in-data SuperField names an export that does not exist. The package
    // still OPENS, because UTA-0003 validates the two tables and not an
    // object's bytes -- so this fixture is only reachable at all because the
    // two records are independent.
    const std::vector<std::uint8_t> bytes = twoClassPackage(9999);
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto info = readClass(*package, package->exports()[1]);

    // A reader taking the in-data field fails here or returns garbage. This
    // one reports the table's parent and does not fail, so the fixture
    // isolates this rule and no other.
    REQUIRE(info.has_value());
    REQUIRE(info->super.kind() == ObjectReferenceKind::Export);
    CHECK(info->super.index() == 0);
}

TEST_CASE("INV-10: an export whose class reference is not null is InvalidArgument") {
    UnrealPackageBuilder builder;
    builder.addName("None");
    builder.addName("Base");
    builder.addName("Texture");

    ClassExportWriter writer;
    writer.setFriendlyName(1);

    ExportEntry classEntry;
    classEntry.objectClass = 0;
    classEntry.objectName = 1;
    classEntry.serialData = writer.build(68);
    builder.addExport(classEntry);

    ExportEntry objectEntry;
    objectEntry.objectClass = 1; // the export at index 0 -- not null, so not a class
    objectEntry.objectName = 2;
    objectEntry.serialData = TaggedPropertyWriter{}.build(0);
    builder.addExport(objectEntry);

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto info = readClass(*package, package->exports()[1]);
    REQUIRE_FALSE(info.has_value());
    CHECK(info.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE("INV-10: a class export with no serialised data is InvalidArgument") {
    UnrealPackageBuilder builder;
    builder.addName("None");
    builder.addName("Empty");

    ExportEntry empty;
    empty.objectClass = 0;
    empty.objectName = 1;
    // No serialData at all: a class with no bytes has no parent to report, and
    // an empty result would be indistinguishable from Object's null parent.
    builder.addExport(empty);

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto info = readClass(*package, package->exports()[0]);
    REQUIRE_FALSE(info.has_value());
    CHECK(info.error().code() == ErrorCode::InvalidArgument);
}

TEST_CASE("below package version 62 a class carries no ClassWithin or ClassConfigName") {
    // The arm INV-1 says nothing about: the real-asset tier reads only
    // packages at 68 and 69, so this fixture is the whole coverage of it.
    const std::vector<std::uint8_t> bytes = twoClassPackage(1, 61);
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto info = readClass(*package, package->exports()[1]);
    REQUIRE(info.has_value());
    CHECK(info->within.kind() == ObjectReferenceKind::Null);
    CHECK(info->configName == 0);
    // The defaults still read, which is what proves the cursor did not
    // desynchronise by skipping two fields that were never written.
    REQUIRE(info->defaults.size() == 1);
}
