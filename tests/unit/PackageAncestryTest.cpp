// Walking a class's parent chain, and merging the defaults along it.
//
// Locks INV-6 (the walk terminates on any input: a cycle and a chain past the
// depth cap are both MalformedData), INV-7 (a parent the walk cannot reach
// ends it successfully, in the state that says which fact was true), INV-8
// (the merge keys on the property's name as text, case-insensitively, and on
// its array index) and INV-9 (every merged property carries the package its
// indices are relative to).
//
// Cross-package resolution is tested with a resolver backed by fixtures and no
// disk at all, which is why this item takes a resolver rather than reaching
// for the filesystem itself (SS 3.3).

#include "support/UnrealPackageBuilder.h"
#include "upkg/Class.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

using uta::ErrorCode;
using uta::test::asBytes;
using uta::test::ClassExportWriter;
using uta::test::ExportEntry;
using uta::test::ImportEntry;
using uta::test::TaggedPropertyWriter;
using uta::test::UnrealPackageBuilder;
using uta::upkg::AncestryEnd;
using uta::upkg::effectiveDefaults;
using uta::upkg::Package;
using uta::upkg::PackageResolver;
using uta::upkg::readAncestry;

namespace {

/// A resolver that supplies nothing. INV-7's first end state.
const PackageResolver NOTHING_AVAILABLE =
    [](std::string_view) -> uta::Result<const Package*> { return nullptr; };

/// Add a class export whose in-data fields are otherwise uninteresting.
void addClass(UnrealPackageBuilder& builder, std::int32_t nameIndex,
              std::int32_t tableSuper, std::vector<std::uint8_t> defaults) {
    ClassExportWriter writer;
    writer.setFriendlyName(nameIndex).setDefaults(std::move(defaults));

    ExportEntry entry;
    entry.objectClass = 0;
    entry.super = tableSuper;
    entry.objectName = nameIndex;
    entry.serialData = writer.build(68);
    builder.addExport(entry);
}

} // namespace

TEST_CASE("INV-6: a two-class cycle is MalformedData") {
    UnrealPackageBuilder builder;
    builder.addName("None");
    builder.addName("Alpha");
    builder.addName("Beta");

    // Each names the other. Both references are in range, so the package
    // opens -- the container validates the tables, not the graph they describe.
    addClass(builder, 1, 2, TaggedPropertyWriter{}.build(0));
    addClass(builder, 2, 1, TaggedPropertyWriter{}.build(0));

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto ancestry = readAncestry(*package, package->exports()[0], NOTHING_AVAILABLE);
    REQUIRE_FALSE(ancestry.has_value());
    CHECK(ancestry.error().code() == ErrorCode::MalformedData);
}

TEST_CASE("INV-6: a chain past the depth cap is MalformedData") {
    UnrealPackageBuilder builder;
    builder.addName("None");

    // Seventy classes, each the parent of the next. The deepest chain in the
    // reference install is 12, so a cap that fires here is evidence of a
    // malformed package rather than of an unusually deep hierarchy.
    constexpr std::int32_t COUNT = 70;
    for (std::int32_t i = 0; i < COUNT; ++i) {
        builder.addName("Class" + std::to_string(i));
        // Export 0 has no parent; export i names export i-1, which is `i` in
        // the format's one-based object reference.
        addClass(builder, i + 1, i == 0 ? 0 : i, TaggedPropertyWriter{}.build(0));
    }

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto ancestry =
        readAncestry(*package, package->exports()[COUNT - 1], NOTHING_AVAILABLE);
    REQUIRE_FALSE(ancestry.has_value());
    CHECK(ancestry.error().code() == ErrorCode::MalformedData);
}

TEST_CASE("a chain inside one package walks to its root") {
    UnrealPackageBuilder builder;
    builder.addName("None");
    builder.addName("Object");
    builder.addName("Actor");
    builder.addName("Pawn");

    addClass(builder, 1, 0, TaggedPropertyWriter{}.build(0)); // Object, no parent
    addClass(builder, 2, 1, TaggedPropertyWriter{}.build(0)); // Actor -> Object
    addClass(builder, 3, 2, TaggedPropertyWriter{}.build(0)); // Pawn  -> Actor

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto ancestry = readAncestry(*package, package->exports()[2], NOTHING_AVAILABLE);
    REQUIRE(ancestry.has_value());
    CHECK(ancestry->end == AncestryEnd::Root);
    // The class itself first, root last.
    REQUIRE(ancestry->chain.size() == 3);
    CHECK(ancestry->chain[0].entry == &package->exports()[2]);
    CHECK(ancestry->chain[2].entry == &package->exports()[0]);
}

namespace {

/// A package whose one class's parent is an import in another package.
/// `packageNameIndex` names that package, so a test can spell it differently
/// from the resolver's own key.
std::vector<std::uint8_t> packageWithImportedParent(const std::string& parentPackage) {
    UnrealPackageBuilder builder;
    builder.addName("None");        // 0
    builder.addName("Core");        // 1
    builder.addName("Class");       // 2
    builder.addName(parentPackage); // 3 -- the package the parent lives in
    builder.addName("Actor");       // 4 -- the parent class
    builder.addName("Brute");       // 5 -- the local class
    builder.addName("Health");      // 6

    // Import 0 is the PACKAGE: its outer is null, which is what makes it the
    // root of the outer chain the walk follows.
    ImportEntry packageImport;
    packageImport.classPackage = 1;
    packageImport.className = 2;
    packageImport.outer = 0;
    packageImport.objectName = 3;
    builder.addImport(packageImport);

    // Import 1 is the CLASS, whose outer names import 0.
    ImportEntry classImport;
    classImport.classPackage = 1;
    classImport.className = 2;
    classImport.outer = -1; // the import at index 0
    classImport.objectName = 4;
    builder.addImport(classImport);

    // The local class's parent is import 1.
    addClass(builder, 5, -2, TaggedPropertyWriter{}.addInt(6, 250).build(0));
    return builder.build();
}

/// A package holding one class of the given name, for a resolver to return.
std::vector<std::uint8_t> packageHolding(const std::string& className,
                                         std::int32_t health) {
    UnrealPackageBuilder builder;
    builder.addName("None");     // 0
    builder.addName(className);  // 1
    builder.addName("Health");   // 2
    addClass(builder, 1, 0, TaggedPropertyWriter{}.addInt(2, health).build(0));
    return builder.build();
}

} // namespace

TEST_CASE("INV-7: a resolver that supplies nothing ends the walk PackageMissing") {
    const std::vector<std::uint8_t> bytes = packageWithImportedParent("Engine");
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto ancestry = readAncestry(*package, package->exports()[0], NOTHING_AVAILABLE);
    // Not an error: an install that lacks a mod is the ordinary case.
    REQUIRE(ancestry.has_value());
    CHECK(ancestry->end == AncestryEnd::PackageMissing);
    CHECK(ancestry->missingPackage == "Engine");
    CHECK(ancestry->missingClass == "Actor");
}

TEST_CASE("INV-7: a package that opened without the class ends the walk ClassMissing") {
    const std::vector<std::uint8_t> bytes = packageWithImportedParent("Engine");
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    // The package IS available; it simply does not hold Actor.
    const std::vector<std::uint8_t> otherBytes = packageHolding("Decoration", 1);
    const auto other = Package::open(asBytes(otherBytes));
    REQUIRE(other.has_value());

    const PackageResolver resolver =
        [&](std::string_view) -> uta::Result<const Package*> { return &*other; };

    const auto ancestry = readAncestry(*package, package->exports()[0], resolver);
    REQUIRE(ancestry.has_value());
    CHECK(ancestry->end == AncestryEnd::ClassMissing);
    CHECK(ancestry->missingClass == "Actor");
    // EMPTY on purpose: naming a package that is present would report the
    // opposite of what happened. A reader that treats both ends alike passes
    // whichever of these two cases is written alone, which is why they are
    // separate tests.
    CHECK(ancestry->missingPackage.empty());
}

TEST_CASE("INV-7: an import naming its package in another case still resolves") {
    // The import spells it one way and the resolver keys it another. The walk
    // folds before calling, so a resolver may assume folded input -- without
    // that rule one side folds, the other does an exact lookup, and an
    // installed mod is reported absent.
    const std::vector<std::uint8_t> bytes = packageWithImportedParent("BotPack");
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const std::vector<std::uint8_t> parentBytes = packageHolding("Actor", 100);
    const auto parent = Package::open(asBytes(parentBytes));
    REQUIRE(parent.has_value());

    bool sawFolded = false;
    const PackageResolver resolver =
        [&](std::string_view name) -> uta::Result<const Package*> {
        sawFolded = (name == "botpack");
        return name == "botpack" ? &*parent : nullptr;
    };

    const auto ancestry = readAncestry(*package, package->exports()[0], resolver);
    REQUIRE(ancestry.has_value());
    CHECK(sawFolded);
    CHECK(ancestry->end == AncestryEnd::Root);
    REQUIRE(ancestry->chain.size() == 2);
}

TEST_CASE("INV-8 and INV-9: a child overrides its parent across a package boundary") {
    // The two packages place `Health` at DIFFERENT name-table indices -- 6 in
    // the child, 2 in the parent. A merge keyed on the name index silently
    // fails to override and the caller gets the base class's value.
    const std::vector<std::uint8_t> bytes = packageWithImportedParent("Engine");
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const std::vector<std::uint8_t> parentBytes = packageHolding("Actor", 100);
    const auto parent = Package::open(asBytes(parentBytes));
    REQUIRE(parent.has_value());

    const PackageResolver resolver =
        [&](std::string_view) -> uta::Result<const Package*> { return &*parent; };

    const auto ancestry = readAncestry(*package, package->exports()[0], resolver);
    REQUIRE(ancestry.has_value());
    REQUIRE(ancestry->end == AncestryEnd::Root);

    const auto merged = effectiveDefaults(*ancestry);
    REQUIRE(merged.has_value());
    REQUIRE(merged->size() == 1);
    CHECK((*merged)[0].name == "Health");
    CHECK(std::get<std::int32_t>((*merged)[0].property.value) == 250);
    // INV-9: the value came from the child, so its indices are relative to the
    // child's package. An object-valued default carrying the wrong origin
    // cannot be resolved at all.
    CHECK((*merged)[0].origin == &*package);
}

TEST_CASE("INV-8: a child spelling a property differently in case still overrides") {
    UnrealPackageBuilder builder;
    builder.addName("None");    // 0
    builder.addName("Base");    // 1
    builder.addName("Derived"); // 2
    builder.addName("Health");  // 3
    builder.addName("HEALTH");  // 4 -- the same property, shouted

    addClass(builder, 1, 0, TaggedPropertyWriter{}.addInt(3, 100).build(0));
    addClass(builder, 2, 1, TaggedPropertyWriter{}.addInt(4, 250).build(0));

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto ancestry = readAncestry(*package, package->exports()[1], NOTHING_AVAILABLE);
    REQUIRE(ancestry.has_value());

    const auto merged = effectiveDefaults(*ancestry);
    REQUIRE(merged.has_value());
    // One entry, not two: the engine's names are case-insensitive, so a child
    // spelling it differently must override rather than add.
    REQUIRE(merged->size() == 1);
    CHECK(std::get<std::int32_t>((*merged)[0].property.value) == 250);
    // The spelling of the class nearest the leaf that set it -- the one an
    // author last wrote.
    CHECK((*merged)[0].name == "HEALTH");
}

TEST_CASE("INV-8: two array elements of one property both survive the merge") {
    UnrealPackageBuilder builder;
    builder.addName("None");
    builder.addName("Base");
    builder.addName("Derived");
    builder.addName("Slot");

    // The parent sets element 0 and the child sets element 1. A merge that
    // ignores the array index collapses them and one value is lost.
    addClass(builder, 1, 0, TaggedPropertyWriter{}.addIntAt(3, 0, 11).build(0));
    addClass(builder, 2, 1, TaggedPropertyWriter{}.addIntAt(3, 1, 22).build(0));

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto ancestry = readAncestry(*package, package->exports()[1], NOTHING_AVAILABLE);
    REQUIRE(ancestry.has_value());

    const auto merged = effectiveDefaults(*ancestry);
    REQUIRE(merged.has_value());
    REQUIRE(merged->size() == 2);
    CHECK(std::get<std::int32_t>((*merged)[0].property.value) == 11);
    CHECK(std::get<std::int32_t>((*merged)[1].property.value) == 22);
}

TEST_CASE("an incomplete ancestry still merges over the part that resolved") {
    const std::vector<std::uint8_t> bytes = packageWithImportedParent("Engine");
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto ancestry = readAncestry(*package, package->exports()[0], NOTHING_AVAILABLE);
    REQUIRE(ancestry.has_value());
    REQUIRE(ancestry->end == AncestryEnd::PackageMissing);

    // Honest but incomplete, and the caller knows which case it is from
    // Ancestry::end.
    const auto merged = effectiveDefaults(*ancestry);
    REQUIRE(merged.has_value());
    REQUIRE(merged->size() == 1);
    CHECK(std::get<std::int32_t>((*merged)[0].property.value) == 250);
}
