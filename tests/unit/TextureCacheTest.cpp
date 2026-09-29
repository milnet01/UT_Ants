// The texture cache on its own -- UTA-0148 finding (2), src/ubake/TextureCache.h.
//
// That a bake's output is the same with the cache cold, warm or off is
// BakeCliTest's end-to-end case. These are the rules under it: a stored
// material comes back exactly, any input generate reads changes the key, a
// damaged file is a miss rather than a wrong material, and trim removes the
// least recently used files first.

#include "BakeFixture.h"
#include "ubake/TextureCache.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

using uta::test::bake::TempDir;
using uta::ubake::TextureCache;
using uta::ubake::textureCacheKey;

namespace {

uta::umat::Image picture(std::uint8_t shade) {
    uta::umat::Image image;
    image.width = 4;
    image.height = 4;
    image.channels = 4;
    image.pixels.assign(64, std::byte{shade});
    return image;
}

/// One BC7 map of a 4x4 picture: one 16-byte block, as expectedBlockBytes asks.
uta::umat::Material material(std::string id, std::byte fill) {
    uta::umat::Material made;
    made.id = std::move(id);
    made.metallic = true;
    made.parallaxDepth = 3;
    uta::ubundle::CompressedTexture map;
    map.name = made.id + ":albedo";
    map.format = uta::ubundle::BlockFormat::BC7;
    map.width = 4;
    map.height = 4;
    map.sourceWidth = 4;
    map.sourceHeight = 4;
    map.mipCount = 1;
    map.blocks.assign(16, fill);
    made.maps.push_back(std::move(map));
    return made;
}

std::vector<fs::path> filesUnder(const fs::path& directory) {
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(directory))
        if (entry.is_regular_file()) files.push_back(entry.path());
    return files;
}

} // namespace

TEST_CASE("UTA-0148: a stored material comes back exactly, and only for its own key and id",
          "[ubake]") {
    const TempDir dir;
    TextureCache cache(dir.path());
    const auto key = textureCacheKey("pkg.wall", picture(10), {});
    CHECK_FALSE(cache.find(key, "pkg.wall").has_value());

    const auto stored = material("pkg.wall", std::byte{7});
    cache.store(key, stored);
    const auto found = cache.find(key, "pkg.wall");
    REQUIRE(found.has_value());
    CHECK(found->id == stored.id);
    CHECK(found->metallic == stored.metallic);
    CHECK(found->parallaxDepth == stored.parallaxDepth);
    REQUIRE(found->maps.size() == 1);
    CHECK(found->maps[0].name == stored.maps[0].name);
    CHECK(found->maps[0].format == stored.maps[0].format);
    CHECK(found->maps[0].width == 4);
    CHECK(found->maps[0].sourceWidth == 4);
    CHECK(found->maps[0].mipCount == 1);
    CHECK(found->maps[0].blocks == stored.maps[0].blocks);

    // A different id under the same key is not served this material.
    CHECK_FALSE(cache.find(key, "pkg.other").has_value());
    CHECK(cache.hits() == 1);
    CHECK(cache.misses() == 2);
}

TEST_CASE("UTA-0148: every input generate reads changes the key", "[ubake]") {
    const uta::umat::MaterialSettings base{};
    const auto key = textureCacheKey("pkg.wall", picture(10), base);
    CHECK(key == textureCacheKey("pkg.wall", picture(10), base));

    CHECK(key != textureCacheKey("pkg.wall#masked", picture(10), base));
    CHECK(key != textureCacheKey("pkg.wall", picture(11), base));
    auto wider = picture(10);
    wider.width = 8;
    wider.height = 2;
    CHECK(key != textureCacheKey("pkg.wall", wider, base));

    auto changed = base;
    changed.requestedUpscale = 1;
    CHECK(key != textureCacheKey("pkg.wall", picture(10), changed));
    changed = base;
    changed.metallic = !base.metallic;
    CHECK(key != textureCacheKey("pkg.wall", picture(10), changed));
    changed = base;
    changed.baseRoughness = 10;
    CHECK(key != textureCacheKey("pkg.wall", picture(10), changed));
    changed = base;
    changed.emissive = !base.emissive;
    CHECK(key != textureCacheKey("pkg.wall", picture(10), changed));
    changed = base;
    changed.emissiveThreshold = 1;
    CHECK(key != textureCacheKey("pkg.wall", picture(10), changed));
    changed = base;
    changed.parallaxDepth = 0;
    CHECK(key != textureCacheKey("pkg.wall", picture(10), changed));
}

TEST_CASE("UTA-0148: a damaged cache file is a miss, never a wrong material", "[ubake]") {
    const TempDir dir;
    TextureCache cache(dir.path());
    const auto key = textureCacheKey("pkg.wall", picture(10), {});
    cache.store(key, material("pkg.wall", std::byte{7}));
    const auto files = filesUnder(dir.path());
    REQUIRE(files.size() == 1);

    // Cut short: the last block byte is gone.
    fs::resize_file(files[0], fs::file_size(files[0]) - 1);
    CHECK_FALSE(cache.find(key, "pkg.wall").has_value());

    // Rewritten, then one block byte past what the fields require.
    cache.store(key, material("pkg.wall", std::byte{7}));
    {
        std::ofstream grow(files[0], std::ios::binary | std::ios::app);
        grow.put('\0');
    }
    CHECK_FALSE(cache.find(key, "pkg.wall").has_value());
}

TEST_CASE("UTA-0148: trim removes the least recently used files until under the cap", "[ubake]") {
    const TempDir dir;
    // Three one-map materials, all the same size; a cap that holds two.
    TextureCache sizing(dir.path() / "sizing");
    sizing.store(textureCacheKey("a", picture(1), {}), material("a", std::byte{1}));
    const std::uint64_t one = fs::file_size(filesUnder(dir.path() / "sizing").at(0));

    const fs::path root = dir.path() / "cache";
    TextureCache cache(root, 2 * one);
    const auto keyA = textureCacheKey("a", picture(1), {});
    const auto keyB = textureCacheKey("b", picture(2), {});
    const auto keyC = textureCacheKey("c", picture(3), {});
    cache.store(keyA, material("a", std::byte{1}));
    cache.store(keyB, material("b", std::byte{2}));
    cache.store(keyC, material("c", std::byte{3}));
    REQUIRE(filesUnder(root).size() == 3);

    // Which file is whose: age them all, then the one a find marks new is it.
    const auto now = fs::file_time_type::clock::now();
    const auto fileOf = [&](std::string_view id, const uta::ubake::TextureCacheKey& key) {
        for (const fs::path& file : filesUnder(root)) fs::last_write_time(file, now - std::chrono::hours(100));
        REQUIRE(cache.find(key, id).has_value());
        for (const fs::path& file : filesUnder(root))
            if (fs::last_write_time(file) > now - std::chrono::hours(50)) return file;
        return fs::path{};
    };
    const fs::path fileA = fileOf("a", keyA);
    const fs::path fileB = fileOf("b", keyB);
    const fs::path fileC = fileOf("c", keyC);
    REQUIRE_FALSE(fileA.empty());
    REQUIRE_FALSE(fileB.empty());
    REQUIRE_FALSE(fileC.empty());

    // B least recently used, then C, then A.
    fs::last_write_time(fileB, now - std::chrono::hours(3));
    fs::last_write_time(fileC, now - std::chrono::hours(2));
    fs::last_write_time(fileA, now - std::chrono::hours(1));

    cache.trim();
    CHECK_FALSE(fs::exists(fileB));
    CHECK(fs::exists(fileC));
    CHECK(fs::exists(fileA));
}

TEST_CASE("UTA-0148: a stored map whose blocks do not fit its fields is not served", "[ubake]") {
    const TempDir dir;
    TextureCache cache(dir.path());
    const auto key = textureCacheKey("pkg.wall", picture(10), {});
    auto wrong = material("pkg.wall", std::byte{7});
    wrong.maps[0].blocks.resize(15); // a 4x4 BC7 level is one 16-byte block
    cache.store(key, wrong);
    CHECK_FALSE(cache.find(key, "pkg.wall").has_value());
}
