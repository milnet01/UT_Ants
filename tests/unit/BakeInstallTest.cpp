// UTA-0011's install cases: the search order, the resolver, and the check.
//
// docs/specs/UTA-0011-map-baker.md SS 4.2, SS 4.9, INV-11 and INV-12. Every
// install here is a synthetic one written to a temporary directory.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "BakeFixture.h"

#include "support/UnrealPackageBuilder.h"
#include "ubake/Install.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using uta::test::bake::classPackage;
using uta::test::bake::TempDir;
using uta::test::bake::tinyPackage;
using uta::test::bake::writeFile;
using uta::ubake::checkInstall;
using uta::ubake::CheckReport;
using uta::ubake::Install;

namespace {

const std::vector<std::string_view> REQUIRED = {"Core", "Engine", "Botpack"};

const std::vector<std::uint8_t> NOT_A_PACKAGE = {'n', 'o', 't', ' ', 'a', ' ', 'p', 'k', 'g'};

void writeRequired(const fs::path& root, std::string_view except = {}) {
    for (const std::string_view name : REQUIRED)
        if (name != except) writeFile(root / "System" / (std::string(name) + ".u"), tinyPackage(name));
}

Install openInstall(const fs::path& root) {
    auto install = Install::open(root);
    REQUIRE(install.has_value());
    return std::move(*install);
}

/// The name of the first export of the package `name` resolves to -- what
/// tells two synthetic packages apart.
std::string resolvedExport(Install& install, std::string_view name) {
    const auto resolved = install.resolver()(name);
    REQUIRE(resolved.has_value());
    REQUIRE(*resolved != nullptr);
    const uta::upkg::Package& package = **resolved;
    REQUIRE_FALSE(package.exports().empty());
    return std::string(package.name(package.exports()[0].objectName).value_or("?"));
}

} // namespace

TEST_CASE("checkInstall passes an install holding Core and Engine and Botpack",
          "[ubake][install]") {
    // INV-11.
    const TempDir dir;
    writeRequired(dir.path());
    const CheckReport report = checkInstall(dir.path());
    CHECK(report.ok);
    CHECK(report.problems.empty());
}

TEST_CASE("checkInstall names each missing package", "[ubake][install]") {
    // INV-11: one problem per package removed, naming it.
    for (const std::string_view missing : REQUIRED) {
        INFO("missing: " << missing);
        const TempDir dir;
        writeRequired(dir.path(), missing);
        const CheckReport report = checkInstall(dir.path());
        CHECK_FALSE(report.ok);
        REQUIRE(report.problems.size() == 1);
        const std::string file = "System/" + std::string(missing) + ".u";
        CHECK(report.problems[0].what == file);
        CHECK(report.problems[0].why
              == file + " is missing, so this is not an Unreal Tournament install.");
    }
}

TEST_CASE("checkInstall carries upkg's message for a package that does not open",
          "[ubake][install]") {
    // INV-11. The message is upkg's own, not a paraphrase of it.
    const auto refusal = uta::upkg::Package::open(uta::test::asBytes(NOT_A_PACKAGE));
    REQUIRE_FALSE(refusal.has_value());

    const TempDir dir;
    writeRequired(dir.path(), "Botpack");
    writeFile(dir.path() / "System" / "Botpack.u", NOT_A_PACKAGE);
    const CheckReport report = checkInstall(dir.path());
    CHECK_FALSE(report.ok);
    REQUIRE(report.problems.size() == 1);
    CHECK(report.problems[0].why == refusal.error().message());
    CHECK(report.problems[0].what.ends_with("Botpack.u"));
}

TEST_CASE("checkInstall refuses a path that is not a directory", "[ubake][install]") {
    const TempDir dir;
    writeFile(dir.path() / "a-file", tinyPackage("Core"));
    for (const fs::path& root : {dir.path() / "a-file", dir.path() / "nothing-here"}) {
        const CheckReport report = checkInstall(root);
        CHECK_FALSE(report.ok);
        REQUIRE(report.problems.size() == 1);
        CHECK(report.problems[0].why.find("not a directory") != std::string::npos);
    }
}

TEST_CASE("an earlier search directory shadows a later one", "[ubake][install]") {
    // INV-12: Textures comes before Sounds in UT99's order, so the texture
    // package wins -- and the sound package is found once it is alone, which
    // is what shows the lookup is by name and not by name and extension.
    const TempDir both;
    writeFile(both.path() / "Textures" / "wonderland.utx", tinyPackage("FromTextures"));
    writeFile(both.path() / "Sounds" / "wonderland.uax", tinyPackage("FromSounds"));
    Install install = openInstall(both.path());
    CHECK(resolvedExport(install, "wonderland") == "FromTextures");
    CHECK(install.pathOf("wonderland").filename() == "wonderland.utx");

    const TempDir alone;
    writeFile(alone.path() / "Sounds" / "wonderland.uax", tinyPackage("FromSounds"));
    Install soundsOnly = openInstall(alone.path());
    CHECK(resolvedExport(soundsOnly, "wonderland") == "FromSounds");
}

TEST_CASE("search directories and extensions match whatever their case", "[ubake][install]") {
    // SS 4.2, as ut-dump's SystemPackages matches. Each resolver call folds
    // its input, so an unfolded name resolves too.
    const TempDir dir;
    writeFile(dir.path() / "TEXTURES" / "WonderLand.UTX", tinyPackage("Shouting"));
    Install install = openInstall(dir.path());
    CHECK(resolvedExport(install, "WONDERLAND") == "Shouting");
    CHECK(resolvedExport(install, "wonderland") == "Shouting");
}

TEST_CASE("a file that does not open resolves to nothing and keeps its path",
          "[ubake][install]") {
    const TempDir dir;
    writeFile(dir.path() / "System" / "Broken.u", NOT_A_PACKAGE);
    Install install = openInstall(dir.path());
    const auto resolved = install.resolver()("broken");
    REQUIRE(resolved.has_value());
    CHECK(*resolved == nullptr);
    CHECK(install.pathOf("broken").filename() == "Broken.u");
    CHECK(install.bytesOf("broken").empty());
}

TEST_CASE("a resolver keeps working after its Install is moved", "[ubake][install]") {
    // Install.h promises this; a resolver capturing `this` would dangle.
    const TempDir dir;
    writeFile(dir.path() / "System" / "ActorPkg.u", classPackage("Lamp"));
    Install first = openInstall(dir.path());
    const uta::upkg::PackageResolver resolver = first.resolver();
    Install moved = std::move(first);

    const auto resolved = resolver("actorpkg");
    REQUIRE(resolved.has_value());
    REQUIRE(*resolved != nullptr);
    CHECK_FALSE(moved.bytesOf("actorpkg").empty());
}
