// UTA-0113's lookup cases -- INV-6.
//
// docs/specs/UTA-0113-recipe-format.md SS 4.3, over scratch directories
// standing in for the player's data directory and the shipped recipes.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "BakeFixture.h"

#include "core/Error.h"
#include "core/FileSystem.h"
#include "urecipe/Lookup.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace stdfs = std::filesystem;
using uta::ErrorCode;
using uta::test::bake::TempDir;
namespace urecipe = uta::urecipe;

namespace {

/// A recipe for `map` whose friendly name says where it lives.
std::string recipeText(std::string_view map, std::string_view name, std::string_view extra = {}) {
    return "ut-ants recipe 1\n[map]\nfile = " + std::string(map) + "\nname = \"" + std::string(name) + "\"\n"
           + std::string(extra);
}

void writeText(const stdfs::path& path, const std::string& text) {
    stdfs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::array<std::byte, 32> digestOf(std::byte fill) {
    std::array<std::byte, 32> digest{};
    digest.fill(fill);
    return digest;
}

struct Places {
    TempDir dir;
    urecipe::Sources sources;

    Places() {
        sources.player = dir.path() / "data" / "recipes";
        sources.shipped = dir.path() / "shipped";
    }
};

std::string foundName(const urecipe::Sources& sources, std::string_view map = "dm-x") {
    const auto found = urecipe::find(sources, map, digestOf(std::byte{0x11}));
    if (!found.has_value()) FAIL("refused: " << found.error().message());
    REQUIRE(found->has_value());
    return (*found)->recipe.friendlyName;
}

} // namespace

TEST_CASE("INV-6: no recipe anywhere is no recipe", "[urecipe][lookup]") {
    const Places places;
    const auto found = urecipe::find(places.sources, "dm-x", digestOf(std::byte{0x11}));
    REQUIRE(found.has_value());
    CHECK_FALSE(found->has_value());
}

TEST_CASE("INV-6: the named recipe beats the player's which beats the shipped one", "[urecipe][lookup]") {
    Places places;
    writeText(places.sources.shipped / "dm-x.recipe", recipeText("dm-x", "shipped"));
    CHECK(foundName(places.sources) == "shipped");

    writeText(places.sources.player / "dm-x.recipe", recipeText("DM-X", "player"));
    CHECK(foundName(places.sources) == "player");

    const stdfs::path named = places.dir.path() / "elsewhere" / "mine.txt";
    writeText(named, recipeText("dm-x", "named"));
    places.sources.named = named;
    CHECK(foundName(places.sources) == "named");

    // The path the recipe came from rides along.
    const auto found = urecipe::find(places.sources, "dm-x", digestOf(std::byte{0x11}));
    REQUIRE(found.has_value());
    REQUIRE(found->has_value());
    CHECK((*found)->path == named);
}

TEST_CASE("INV-6: an empty directory is not looked in", "[urecipe][lookup]") {
    Places places;
    writeText(places.sources.shipped / "dm-x.recipe", recipeText("dm-x", "shipped"));
    places.sources.player.clear();
    CHECK(foundName(places.sources) == "shipped");
}

TEST_CASE("INV-6: a recipe for another map is refused", "[urecipe][lookup]") {
    Places places;
    writeText(places.sources.player / "dm-x.recipe", recipeText("dm-y", "player"));
    const auto found = urecipe::find(places.sources, "dm-x", digestOf(std::byte{0x11}));
    REQUIRE_FALSE(found.has_value());
    CHECK(found.error().code() == ErrorCode::InvalidArgument);
    CHECK(std::string(found.error().message()).find("dm-y") != std::string::npos);
}

TEST_CASE("INV-6: a recipe whose sha256 the map does not have is refused", "[urecipe][lookup]") {
    Places places;
    const std::string elevens(64, '1');
    const std::string twos(64, '2');
    writeText(places.sources.player / "dm-x.recipe",
              recipeText("dm-x", "player").replace(std::string_view("ut-ants recipe 1\n[map]\n").size(), 0,
                                                    "sha256 = " + twos + "\n"));
    const auto refused = urecipe::find(places.sources, "dm-x", digestOf(std::byte{0x11}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code() == ErrorCode::InvalidArgument);

    writeText(places.sources.player / "dm-x.recipe",
              recipeText("dm-x", "player").replace(std::string_view("ut-ants recipe 1\n[map]\n").size(), 0,
                                                    "sha256 = " + elevens + "\n"));
    CHECK(foundName(places.sources) == "player");
}

TEST_CASE("INV-6: a named recipe that does not exist is refused", "[urecipe][lookup]") {
    Places places;
    writeText(places.sources.player / "dm-x.recipe", recipeText("dm-x", "player"));
    places.sources.named = places.dir.path() / "missing.recipe";
    const auto found = urecipe::find(places.sources, "dm-x", digestOf(std::byte{0x11}));
    REQUIRE_FALSE(found.has_value());
    CHECK(found.error().code() == ErrorCode::NotFound);
}

TEST_CASE("INV-6: a recipe that does not parse is refused naming its file", "[urecipe][lookup]") {
    Places places;
    writeText(places.sources.shipped / "dm-x.recipe", recipeText("dm-x", "shipped", "colour = red\n"));
    const auto found = urecipe::find(places.sources, "dm-x", digestOf(std::byte{0x11}));
    REQUIRE_FALSE(found.has_value());
    CHECK(found.error().code() == ErrorCode::MalformedData);
    const std::string message(found.error().message());
    CHECK(message.find("dm-x.recipe") != std::string::npos);
    CHECK(message.find("recipe line 5") != std::string::npos);
}

TEST_CASE("INV-6: the player's recipes live under the data directory", "[urecipe][lookup]") {
    const auto data = uta::fs::dataDirectory();
    const urecipe::Sources sources = urecipe::standardSources(std::nullopt);
    if (data.has_value()) CHECK(sources.player == *data / "recipes");
    else CHECK(sources.player.empty());
    CHECK(sources.shipped == urecipe::shippedDirectory());
    CHECK(sources.shipped.filename() == "recipes");
}
