// UTA-0316: map-pic saves the picture a map ships with -- its LevelInfo's
// Screenshot texture -- as a PNG, for UT_MonsterHunt's server launcher.
//
// tools/map-pic/Cli.cpp is compiled into this binary. The picture is graded
// through previewOf, pixel by pixel against the fixture's own palette; the
// command line through a synthetic install on disk.
//
// NO TEST NAME CONTAINS A COMMA, AND NONE STARTS WITH A DASH -- BakeCliTest.cpp
// says why.

#include "BakeFixture.h"

#include "map-pic/Cli.h"
#include "support/UnrealPackageBuilder.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace uta::test::bake;

namespace {

TextureSpec spec(std::string name, std::string group, std::uint8_t seed) {
    TextureSpec texture;
    texture.name = std::move(name);
    texture.group = std::move(group);
    texture.picture = picture(seed);
    return texture;
}

/// What `picture` decodes to, opaque: each texel its palette entry's r, g and
/// b, and alpha 255 whatever the entry's own alpha says.
std::vector<std::byte> opaque(const Picture& picture) {
    std::vector<std::byte> rgba;
    for (const std::uint8_t index : picture.indices) {
        const auto& colour = picture.palette[index];
        for (int channel = 0; channel < 3; ++channel) rgba.push_back(std::byte{colour[channel]});
        rgba.push_back(std::byte{255});
    }
    return rgba;
}

/// A map whose LevelInfo carries `properties`.
MapBuilder& withLevelInfo(MapBuilder& map, std::vector<PropertySpec> properties) {
    return map.addActorOfClass("Engine", "LevelInfo", std::move(properties));
}

uta::Result<uta::mappic::Preview> previewOf(const Fixture& fixture) {
    MemoryPackages packages = memoryPackagesFor(fixture);
    const std::vector<std::uint8_t> bytes = fixture.map.build();
    const auto package = uta::upkg::Package::open(uta::test::asBytes(bytes));
    REQUIRE(package.has_value());
    return uta::mappic::previewOf(*package, MAP_NAME, packages.resolver());
}

struct Run {
    int code = -1;
    std::string out;
    std::string err;
};

Run run(const std::vector<std::string>& args) {
    const std::vector<std::string_view> views(args.begin(), args.end());
    std::ostringstream out;
    std::ostringstream err;
    Run result;
    result.code = uta::mappic::runCli(views, out, err);
    result.out = out.str();
    result.err = err.str();
    return result;
}

std::vector<unsigned char> readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::uint32_t bigEndianAt(const std::vector<unsigned char>& bytes, std::size_t at) {
    return (std::uint32_t{bytes[at]} << 24) | (std::uint32_t{bytes[at + 1]} << 16)
           | (std::uint32_t{bytes[at + 2]} << 8) | std::uint32_t{bytes[at + 3]};
}

} // namespace

TEST_CASE("UTA-0316: a Screenshot texture the map exports is its preview", "[mappic]") {
    Fixture fixture;
    const std::int32_t shot = fixture.map.addTexture(spec("Shot", "Screens", 3));
    withLevelInfo(fixture.map, {objectProperty("Screenshot", shot)});

    const auto preview = previewOf(fixture);
    REQUIRE(preview.has_value());
    INFO(preview->none);
    REQUIRE(preview->image.has_value());
    CHECK(preview->image->width == 4);
    CHECK(preview->image->height == 4);
    CHECK(preview->image->channels == 4);
    CHECK(preview->image->pixels == opaque(picture(3)));
}

TEST_CASE("UTA-0316: a Screenshot texture imported from another package is its preview", "[mappic]") {
    Fixture fixture;
    fixture.packageTextures = {spec("Shot", "Screens", 6)};
    const std::int32_t shot = fixture.map.importTexture("TexPkg", "Screens", "Shot");
    withLevelInfo(fixture.map, {objectProperty("Screenshot", shot)});

    const auto preview = previewOf(fixture);
    REQUIRE(preview.has_value());
    INFO(preview->none);
    REQUIRE(preview->image.has_value());
    CHECK(preview->image->pixels == opaque(picture(6)));
}

TEST_CASE("UTA-0316: a map naming no Screenshot has no preview and says so", "[mappic]") {
    Fixture fixture;
    fixture.map.addTexture(spec("Shot", "Screens", 3)); // present, but not named
    withLevelInfo(fixture.map, {strProperty("Title", "No Picture")});

    const auto preview = previewOf(fixture);
    REQUIRE(preview.has_value());
    CHECK_FALSE(preview->image.has_value());
    CHECK_FALSE(preview->none.empty());
}

TEST_CASE("UTA-0316: a Screenshot in a package the install lacks gives no preview", "[mappic]") {
    Fixture fixture;
    const std::int32_t shot = fixture.map.importTexture("GonePkg", "Screens", "Shot");
    withLevelInfo(fixture.map, {objectProperty("Screenshot", shot)});

    const auto preview = previewOf(fixture);
    REQUIRE(preview.has_value());
    CHECK_FALSE(preview->image.has_value());
    CHECK_FALSE(preview->none.empty());
}

TEST_CASE("UTA-0316: a Screenshot stored in a format other than palettised gives no preview",
          "[mappic]") {
    Fixture fixture;
    TextureSpec texture = spec("Shot", "Screens", 3);
    texture.format = true; // Format 1, not P8
    const std::int32_t shot = fixture.map.addTexture(texture);
    withLevelInfo(fixture.map, {objectProperty("Screenshot", shot)});

    const auto preview = previewOf(fixture);
    REQUIRE(preview.has_value());
    CHECK_FALSE(preview->image.has_value());
    CHECK_FALSE(preview->none.empty());
}

TEST_CASE("UTA-0316: map-pic writes the preview as a PNG and prints preview", "[mappic]") {
    Fixture fixture;
    const std::int32_t shot = fixture.map.addTexture(spec("Shot", "Screens", 3));
    withLevelInfo(fixture.map, {objectProperty("Screenshot", shot)});
    const TempDir dir;
    const fs::path map = writeInstall(dir.path(), fixture);
    const fs::path png = dir.path() / "out" / "shot.png";

    const Run result = run({dir.path().string(), map.string(), png.string()});
    INFO(result.err);
    REQUIRE(result.code == 0);
    CHECK(result.out == "preview\n");
    const std::vector<unsigned char> bytes = readAll(png);
    REQUIRE(bytes.size() > 24);
    const std::vector<unsigned char> signature(bytes.begin(), bytes.begin() + 8);
    CHECK(signature == std::vector<unsigned char>{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'});
    CHECK(bigEndianAt(bytes, 16) == 4); // IHDR width
    CHECK(bigEndianAt(bytes, 20) == 4); // IHDR height
}

TEST_CASE("UTA-0316: map-pic prints none and writes nothing for a map with no preview", "[mappic]") {
    Fixture fixture;
    withLevelInfo(fixture.map, {});
    const TempDir dir;
    const fs::path map = writeInstall(dir.path(), fixture);
    const fs::path png = dir.path() / "shot.png";

    const Run result = run({dir.path().string(), map.string(), png.string()});
    REQUIRE(result.code == 0);
    CHECK(result.out == "none\n");
    CHECK_FALSE(result.err.empty());
    CHECK_FALSE(fs::exists(png));
}

TEST_CASE("UTA-0316: map-pic refuses the wrong number of arguments", "[mappic]") {
    CHECK(run({}).code == 2);
    CHECK(run({"install", "map.unr"}).code == 2);
    CHECK(run({"install", "map.unr", "out.png", "extra"}).code == 2);
}

TEST_CASE("UTA-0316: map-pic fails on a map that does not open", "[mappic]") {
    Fixture fixture;
    withLevelInfo(fixture.map, {});
    const TempDir dir;
    writeInstall(dir.path(), fixture);
    const fs::path missing = dir.path() / "Maps" / "MH-Missing.unr";

    const Run result = run({dir.path().string(), missing.string(), (dir.path() / "x.png").string()});
    CHECK(result.code == 1);
    CHECK_FALSE(result.err.empty());
}
