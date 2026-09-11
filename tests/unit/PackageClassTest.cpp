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
//
// And docs/specs/UTA-0110-lights-and-placements.md INV-3: where resolveClass
// finds a class, and the two ways it does not.

#include "support/UnrealPackageBuilder.h"
#include "upkg/Class.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using uta::ErrorCode;
using uta::test::asBytes;
using uta::test::ClassExportWriter;
using uta::test::ExportEntry;
using uta::test::ImportEntry;
using uta::test::TaggedPropertyWriter;
using uta::test::UnrealPackageBuilder;
using uta::upkg::AncestryEnd;
using uta::upkg::ObjectReference;
using uta::upkg::ObjectReferenceKind;
using uta::upkg::Package;
using uta::upkg::PackageResolver;
using uta::upkg::readClass;
using uta::upkg::resolveClass;

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

// ------------------------------------------------------------ resolveClass

namespace {

/// A package holding one root class export named `className`.
std::vector<std::uint8_t> packageOfClass(std::string_view className) {
    UnrealPackageBuilder builder;
    builder.addName("None");
    builder.addName(className);
    ClassExportWriter writer;
    writer.setFriendlyName(1).setDefaults(TaggedPropertyWriter{}.build(NAME_NONE));
    ExportEntry entry;
    entry.objectClass = 0;
    entry.objectName = 1;
    entry.serialData = writer.build(68);
    builder.addExport(entry);
    return builder.build();
}

/// A map exporting a class `Local`, and importing `BotPack.PULSEGUN` -- which
/// differs in case from the class package's own spelling on both names.
/// Import 0 is the package, import 1 the class; export 0 is Local.
std::vector<std::uint8_t> mapWithClasses() {
    UnrealPackageBuilder builder;
    builder.addName("None");     // 0
    builder.addName("Core");     // 1
    builder.addName("Package");  // 2
    builder.addName("Class");    // 3
    builder.addName("BotPack");  // 4
    builder.addName("PULSEGUN"); // 5
    builder.addName("Local");    // 6
    builder.addImport(ImportEntry{1, 2, 0, 4});
    builder.addImport(ImportEntry{1, 3, -1, 5});
    ClassExportWriter writer;
    writer.setFriendlyName(6).setDefaults(TaggedPropertyWriter{}.build(NAME_NONE));
    ExportEntry local;
    local.objectClass = 0;
    local.objectName = 6;
    local.serialData = writer.build(68);
    builder.addExport(local);
    return builder.build();
}

const ObjectReference LOCAL_CLASS{1};     // export 0
const ObjectReference IMPORTED_CLASS{-2}; // import 1

/// A resolver answering exactly one name, so a name reaching it unfolded misses.
PackageResolver answering(std::string name, const Package* package) {
    return [name = std::move(name), package](std::string_view asked) -> uta::Result<const Package*> {
        return asked == name ? package : nullptr;
    };
}

} // namespace

TEST_CASE("INV-3: resolveClass finds a class the map exports") {
    const std::vector<std::uint8_t> bytes = mapWithClasses();
    const auto map = Package::open(asBytes(bytes));
    REQUIRE(map.has_value());

    const auto site = resolveClass(*map, "dm-test", LOCAL_CLASS, answering("", nullptr));
    REQUIRE(site.has_value());
    CHECK(site->end == AncestryEnd::Root);
    CHECK(site->package == "dm-test");
    CHECK(site->name == "Local");
    CHECK(site->resolved.package == &*map);
    CHECK(site->resolved.entry == &map->exports()[0]);
}

TEST_CASE("INV-3: resolveClass finds an imported class whatever the case of its names") {
    const std::vector<std::uint8_t> mapBytes = mapWithClasses();
    const std::vector<std::uint8_t> homeBytes = packageOfClass("PulseGun");
    const auto map = Package::open(asBytes(mapBytes));
    const auto home = Package::open(asBytes(homeBytes));
    REQUIRE(map.has_value());
    REQUIRE(home.has_value());

    // The resolver answers `botpack` only, and the class package spells the
    // class `PulseGun` where the map spells it `PULSEGUN`.
    const auto site = resolveClass(*map, "dm-test", IMPORTED_CLASS, answering("botpack", &*home));
    REQUIRE(site.has_value());
    CHECK(site->end == AncestryEnd::Root);
    CHECK(site->package == "botpack");
    CHECK(site->name == "PULSEGUN");
    CHECK(site->resolved.package == &*home);
    CHECK(site->resolved.entry == &home->exports()[0]);
}

TEST_CASE("INV-3: a package the resolver does not supply is PackageMissing") {
    const std::vector<std::uint8_t> bytes = mapWithClasses();
    const auto map = Package::open(asBytes(bytes));
    REQUIRE(map.has_value());

    const auto site = resolveClass(*map, "dm-test", IMPORTED_CLASS, answering("", nullptr));
    REQUIRE(site.has_value());
    CHECK(site->end == AncestryEnd::PackageMissing);
    CHECK(site->package == "botpack");
    CHECK(site->name == "PULSEGUN");
    CHECK(site->resolved.package == nullptr);
    CHECK(site->resolved.entry == nullptr);
}

TEST_CASE("INV-3: a package holding no such class is ClassMissing") {
    const std::vector<std::uint8_t> mapBytes = mapWithClasses();
    const std::vector<std::uint8_t> homeBytes = packageOfClass("Enforcer");
    const auto map = Package::open(asBytes(mapBytes));
    const auto home = Package::open(asBytes(homeBytes));
    REQUIRE(map.has_value());
    REQUIRE(home.has_value());

    const auto site = resolveClass(*map, "dm-test", IMPORTED_CLASS, answering("botpack", &*home));
    REQUIRE(site.has_value());
    CHECK(site->end == AncestryEnd::ClassMissing);
    CHECK(site->package == "botpack");
    CHECK(site->resolved.package == nullptr);
    CHECK(site->resolved.entry == nullptr);
}
