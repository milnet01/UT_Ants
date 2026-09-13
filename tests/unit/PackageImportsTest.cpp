// UTA-0070: which packages a package imports from, as a supported call.
//
// The rule under test is upkg/Package.h's: an import whose outer is null names
// a package, and one with an outer names an object inside one.

#include "support/UnrealPackageBuilder.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

using uta::test::asBytes;
using uta::test::ImportEntry;
using uta::test::UnrealPackageBuilder;
using uta::upkg::Package;

namespace {

// Name indices into the table every fixture below starts with.
constexpr std::int32_t NAME_CORE = 1;
constexpr std::int32_t NAME_CLASS = 2;
constexpr std::int32_t NAME_PACKAGE = 3;
constexpr std::int32_t NAME_ENGINE = 4;
constexpr std::int32_t NAME_ACTOR = 5;
constexpr std::int32_t NAME_TEXPKG = 6;
constexpr std::int32_t NAME_METAL = 7;
constexpr std::int32_t NAME_TEXTURE = 8;
constexpr std::int32_t NAME_PLATE = 9;

UnrealPackageBuilder namedPackage() {
    UnrealPackageBuilder builder;
    builder.addName("None").addName("Core").addName("Class").addName("Package")
        .addName("Engine").addName("Actor").addName("TexPkg").addName("Metal")
        .addName("Texture").addName("Plate");
    return builder;
}

/// The raw reference to import `index`.
constexpr std::int32_t importRef(std::int32_t index) {
    return -(index + 1);
}

} // namespace

TEST_CASE("UTA-0070: only imports whose outer is null are packages", "[package-imports]") {
    UnrealPackageBuilder builder = namedPackage();
    builder.addImport(ImportEntry{NAME_CORE, NAME_PACKAGE, 0, NAME_ENGINE})                // 0 Engine
        .addImport(ImportEntry{NAME_CORE, NAME_CLASS, importRef(0), NAME_ACTOR})          // 1 Engine.Actor
        .addImport(ImportEntry{NAME_CORE, NAME_PACKAGE, 0, NAME_TEXPKG})                  // 2 TexPkg
        .addImport(ImportEntry{NAME_CORE, NAME_PACKAGE, importRef(2), NAME_METAL})        // 3 TexPkg.Metal
        .addImport(ImportEntry{NAME_ENGINE, NAME_TEXTURE, importRef(3), NAME_PLATE});     // 4 TexPkg.Metal.Plate

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto names = uta::upkg::importedPackages(*package);
    REQUIRE(names.has_value());
    // The group Metal is written with a Package class but has an outer, so it
    // is an object inside TexPkg rather than a package of its own.
    CHECK(*names == std::vector<std::string_view>{"Engine", "TexPkg"});
}

TEST_CASE("UTA-0070: a package imported twice is named once in table order", "[package-imports]") {
    UnrealPackageBuilder builder = namedPackage();
    builder.addImport(ImportEntry{NAME_CORE, NAME_PACKAGE, 0, NAME_TEXPKG})
        .addImport(ImportEntry{NAME_CORE, NAME_PACKAGE, 0, NAME_ENGINE})
        .addImport(ImportEntry{NAME_CORE, NAME_PACKAGE, 0, NAME_TEXPKG});

    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto names = uta::upkg::importedPackages(*package);
    REQUIRE(names.has_value());
    CHECK(*names == std::vector<std::string_view>{"TexPkg", "Engine"});
}

TEST_CASE("UTA-0070: a package with no imports needs no packages", "[package-imports]") {
    UnrealPackageBuilder builder = namedPackage();
    const std::vector<std::uint8_t> bytes = builder.build();
    const auto package = Package::open(asBytes(bytes));
    REQUIRE(package.has_value());

    const auto names = uta::upkg::importedPackages(*package);
    REQUIRE(names.has_value());
    CHECK(names->empty());
}
