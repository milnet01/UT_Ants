// UTA-0277: which pictures may move, and the player's answers.
//
// docs/specs/UTA-0277-per-tile-variation.md SS 4.2 and SS 4.5, INV-1, INV-2,
// INV-3, INV-6 and INV-8. Every picture is made here; none is masked, flat,
// liquid or parallax, so only SS 4.2 step 4 decides it (INV-2).
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "BakeFixture.h"

#include "core/Jobs.h"
#include "core/Sha256.h"
#include "support/UnrealPackageBuilder.h"
#include "ubake/Bake.h"
#include "ubake/Name.h"
#include "ubake/TileKind.h"
#include "ubundle/Bundle.h"
#include "ubundle/TileAnswers.h"
#include "umat/Library.h"
#include "umat/Material.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using uta::ubake::judgeTile;
using uta::ubake::pictureHash;
using uta::ubake::TileLimits;
using uta::ubake::tileScores;
using uta::ubundle::TileKind;
using namespace uta::test::bake;

namespace {

constexpr std::uint32_t SIDE = 128;

/// An RGBA picture whose grey at each texel `shade` gives.
template <typename Shade>
uta::umat::Image grey(Shade shade) {
    uta::umat::Image out{SIDE, SIDE, 4, std::vector<std::byte>(std::size_t{SIDE} * SIDE * 4)};
    for (std::uint32_t y = 0; y < SIDE; ++y)
        for (std::uint32_t x = 0; x < SIDE; ++x) {
            const auto value = static_cast<std::byte>(shade(x, y));
            std::byte* p = out.pixels.data() + (std::size_t{y} * SIDE + x) * 4;
            p[0] = p[1] = p[2] = value;
            p[3] = std::byte{255};
        }
    return out;
}

/// Whole-width bands: four dark rows in every sixteen, as mortar courses.
uta::umat::Image bands() {
    return grey([](std::uint32_t, std::uint32_t y) { return y % 16 < 4 ? 40 : 180; });
}

/// Seeded value noise: every texel its own pseudo-random grey, so nothing
/// lines up and nothing repeats.
uta::umat::Image noise(std::uint32_t seed) {
    return grey([seed](std::uint32_t x, std::uint32_t y) {
        std::uint32_t h = (x * 73856093u) ^ (y * 19349663u) ^ (seed * 83492791u);
        h ^= h << 13;
        h ^= h >> 17;
        h ^= h << 5;
        return 60 + static_cast<int>(h % 128);
    });
}

/// A grid of 3 x 3 bright spots, sixteen texels apart, on flat ground -- the
/// rivets SS 4.2's spots exists for.
uta::umat::Image spots() {
    return grey([](std::uint32_t x, std::uint32_t y) { return x % 16 < 3 && y % 16 < 3 ? 230 : 100; });
}

/// Limits under which lines can decide nothing, so only spots can.
constexpr TileLimits SPOTS_ONLY{1e9, 1e9, 0.5, 0.75};

std::string hexOf(const std::array<std::byte, 32>& hash) { return uta::ubake::detail::hex(hash); }

/// A bundle of two materials, the first wearing `a`'s hash, the second `b`'s.
uta::ubundle::Bundle twoMaterials(const std::array<std::byte, 32>& a, TileKind aKind,
                                  const std::array<std::byte, 32>& b, TileKind bKind) {
    uta::ubundle::Bundle bundle;
    bundle.materials.emplace();
    uta::ubundle::MaterialRecord first{"a"};
    first.tileKind = aKind;
    first.tileHash = a;
    uta::ubundle::MaterialRecord second{"b"};
    second.tileKind = bKind;
    second.tileHash = b;
    bundle.materials->push_back(first);
    bundle.materials->push_back(second);
    return bundle;
}

} // namespace

TEST_CASE("UTA-0277 INV-2: bands are Fixed and value noise is Shuffle", "[ubake][tilekind]") {
    const auto banded = tileScores(bands());
    const auto noisy = tileScores(noise(7));
    INFO("bands: lines " << banded.lines << " spots " << banded.spots);
    INFO("noise: lines " << noisy.lines << " spots " << noisy.spots);
    CHECK_FALSE(banded.flat);
    CHECK_FALSE(noisy.flat);
    CHECK(judgeTile(banded) == TileKind::Fixed);
    CHECK(judgeTile(noisy) == TileKind::Shuffle);
}

TEST_CASE("UTA-0277 SS 4.2: diagonal stripes count as lines", "[ubake][tilekind]") {
    // Hazard stripes and herringbone tiles run at 45 degrees, where a row or a
    // column mean sees nothing.
    const auto striped = tileScores(grey([](std::uint32_t x, std::uint32_t y) { return (x + y) % 32 < 16 ? 40 : 200; }));
    INFO("diagonal: lines " << striped.lines << " spots " << striped.spots);
    CHECK(striped.lines >= TileLimits{}.linesFixed);
    CHECK(judgeTile(striped, TileLimits{TileLimits{}.linesShuffle, TileLimits{}.linesFixed, 1e9, 1e9})
          == TileKind::Fixed);
}

TEST_CASE("UTA-0277 INV-2: a grid of spots is not Shuffle and spots alone decide it", "[ubake][tilekind]") {
    const auto spotted = tileScores(spots());
    const auto noisy = tileScores(noise(7));
    INFO("spots: lines " << spotted.lines << " spots " << spotted.spots);
    CHECK(judgeTile(spotted) != TileKind::Shuffle);
    // With lines unable to decide, the spots score alone keeps the grid Fixed
    // and lets the noise move -- so it is computed and not inverted.
    CHECK(judgeTile(spotted, SPOTS_ONLY) == TileKind::Fixed);
    CHECK(judgeTile(noisy, SPOTS_ONLY) == TileKind::Shuffle);
}

TEST_CASE("UTA-0277 SS 4.2 step 3: a flat picture is Fixed whatever the limits", "[ubake][tilekind]") {
    const auto flat = tileScores(grey([](std::uint32_t, std::uint32_t) { return 128; }));
    CHECK(flat.flat);
    CHECK(judgeTile(flat, TileLimits{1e9, 1e9, 1e9, 1e9}) == TileKind::Fixed);
}

TEST_CASE("UTA-0277 SS 4.2: each limit is honoured at its edge", "[ubake][tilekind]") {
    const uta::ubake::TileScores scores{false, 6.0, 0.6};
    CHECK(judgeTile(scores, TileLimits{6.0, 12.0, 0.7, 0.9}) == TileKind::Unsure);  // lines at linesShuffle
    CHECK(judgeTile(scores, TileLimits{6.5, 12.0, 0.6, 0.9}) == TileKind::Unsure);  // spots at spotsShuffle
    CHECK(judgeTile(scores, TileLimits{6.5, 12.0, 0.7, 0.9}) == TileKind::Shuffle);
    CHECK(judgeTile(scores, TileLimits{5.0, 6.0, 0.5, 0.9}) == TileKind::Fixed);    // lines at linesFixed
    CHECK(judgeTile(scores, TileLimits{5.0, 12.0, 0.5, 0.6}) == TileKind::Fixed);   // spots at spotsFixed
}

TEST_CASE("UTA-0277 INV-1: the scores are the recorded values on every compiler", "[ubake][tilekind]") {
    // Recorded on GCC 14; GitHub's three legs each compare the same bits.
    const auto first = tileScores(noise(7));
    const auto again = tileScores(noise(7));
    CHECK(first.lines == again.lines);
    CHECK(first.spots == again.spots);
    INFO("lines " << std::hexfloat << first.lines << " spots " << first.spots);
    CHECK(first.lines == 0x1.91d55377d0758p+0);
    CHECK(first.spots == 0x1.df0ea3f2a85e5p-3);
}

TEST_CASE("UTA-0277 INV-1: the bake's tile kinds and hashes do not depend on the worker count",
          "[ubake][tilekind]") {
    const Fixture fixture = standardFixture();
    MemoryPackages packages = memoryPackagesFor(fixture);
    const std::vector<std::uint8_t> bytes = fixture.map.build();
    const auto map = uta::upkg::Package::open(uta::test::asBytes(bytes));
    REQUIRE(map.has_value());
    std::vector<std::vector<uta::ubundle::MaterialRecord>> baked;
    for (const std::size_t workers : {std::size_t{1}, std::size_t{8}}) {
        uta::JobSystem jobs(workers);
        const auto result = uta::ubake::detail::bake(*map, MAP_NAME, packages.resolver(), jobs, &uta::umat::curated,
                                                     uta::umat::TEXTURE_BUDGET_BYTES);
        REQUIRE(result.has_value());
        REQUIRE(result->bundle.materials.has_value());
        baked.push_back(*result->bundle.materials);
    }
    REQUIRE(baked[0].size() == baked[1].size());
    std::size_t judged = 0;
    for (std::size_t i = 0; i < baked[0].size(); ++i) {
        INFO(baked[0][i].id);
        CHECK(baked[0][i].tileKind == baked[1][i].tileKind);
        CHECK(baked[0][i].tileHash == baked[1][i].tileHash);
        if (baked[0][i].tileHash != std::array<std::byte, 32>{}) ++judged;
    }
    // A bake that judged nothing would compare nothing.
    CHECK(judged > 0);
}

TEST_CASE("UTA-0277 SS 4.5: the picture hash covers its size and its bytes", "[ubake][tilekind]") {
    const auto picture = noise(3);
    std::vector<std::byte> expected = {std::byte{128}, std::byte{0}, std::byte{0}, std::byte{0},
                                       std::byte{128}, std::byte{0}, std::byte{0}, std::byte{0}};
    expected.insert(expected.end(), picture.pixels.begin(), picture.pixels.end());
    CHECK(pictureHash(picture) == uta::sha256(expected));
    // The same bytes laid out at another size are another picture.
    auto wide = picture;
    wide.width = SIDE * 2;
    wide.height = SIDE / 2;
    CHECK(pictureHash(wide) != pictureHash(picture));
}

TEST_CASE("UTA-0277 INV-3: an answer wins over the scores both ways", "[ubundle][tilekind]") {
    const auto bandHash = pictureHash(bands());
    const auto noiseHash = pictureHash(noise(7));
    REQUIRE(judgeTile(tileScores(bands())) == TileKind::Fixed);
    REQUIRE(judgeTile(tileScores(noise(7))) == TileKind::Shuffle);
    auto bundle = twoMaterials(bandHash, TileKind::Fixed, noiseHash, TileKind::Shuffle);
    const auto answers = uta::ubundle::parseTileAnswers(hexOf(bandHash) + " shuffle\n" + hexOf(noiseHash)
                                                        + " fixed  # a.b.c\n");
    CHECK(answers.warnings.empty());
    CHECK(uta::ubundle::applyTileAnswers(bundle, answers) == 2);
    CHECK((*bundle.materials)[0].tileKind == TileKind::Shuffle);
    CHECK((*bundle.materials)[1].tileKind == TileKind::Fixed);
}

TEST_CASE("UTA-0277 INV-6: an answer reaches only the picture it names and never an excluded one",
          "[ubundle][tilekind]") {
    const auto bandHash = pictureHash(bands());
    const auto other = pictureHash(noise(9));
    // The second material is excluded: its hash is all zero.
    auto bundle = twoMaterials(bandHash, TileKind::Unsure, {}, TileKind::Fixed);
    const std::string zero(64, '0');
    const auto answers = uta::ubundle::parseTileAnswers(hexOf(other) + " shuffle\n" + zero + " shuffle\n");
    CHECK(uta::ubundle::applyTileAnswers(bundle, answers) == 0);
    CHECK((*bundle.materials)[0].tileKind == TileKind::Unsure);
    CHECK((*bundle.materials)[1].tileKind == TileKind::Fixed);
    // Changing the answer file changes the kind, with no bake in between.
    const auto later = uta::ubundle::parseTileAnswers(hexOf(bandHash) + " shuffle\n");
    CHECK(uta::ubundle::applyTileAnswers(bundle, later) == 1);
    CHECK((*bundle.materials)[0].tileKind == TileKind::Shuffle);
}

TEST_CASE("UTA-0277 INV-8: a malformed answers line is a warning and the good lines apply",
          "[ubundle][tilekind]") {
    const auto bandHash = pictureHash(bands());
    const auto noiseHash = pictureHash(noise(7));
    const auto thirdHash = pictureHash(noise(11));
    const std::string text = "# answered 2026-10-05\n"
                             "\n"
                             + hexOf(bandHash) + " shuffle\n"     // line 3: good
                             + "not-a-hash shuffle\n"            // line 4: malformed
                             + hexOf(noiseHash) + " maybe\n"     // line 5: malformed
                             + hexOf(thirdHash) + " shuffle\n"   // line 6: conflicts with line 8
                             + hexOf(bandHash) + " shuffle\n"     // line 7: repeats line 3, no conflict
                             + hexOf(thirdHash) + " fixed\n";    // line 8
    const auto answers = uta::ubundle::parseTileAnswers(text);
    REQUIRE(answers.warnings.size() == 3);
    CHECK(answers.warnings[0].find("line 4") != std::string::npos);
    CHECK(answers.warnings[1].find("line 5") != std::string::npos);
    CHECK(answers.warnings[2].find("line 8") != std::string::npos);
    REQUIRE(answers.answers.size() == 1);
    CHECK(answers.answers[0].hash == bandHash);
    CHECK(answers.answers[0].kind == TileKind::Shuffle);
}

TEST_CASE("UTA-0277 SS 4.2 step 1: a material no surface spans two repeats of is excluded",
          "[ubake][tilekind]") {
    // Three textures: one worn by two floor squares, one by one, and one only
    // by a 4-unit square, which a 4-texel picture covers once.
    Fixture fixture;
    MapBuilder& map = fixture.map;
    const std::int32_t once = map.addTexture(TextureSpec{"Once", "Base", picture(1), false});
    const std::int32_t twice = map.addTexture(TextureSpec{"Twice", "Base", picture(2), false});
    const std::int32_t small = map.addTexture(TextureSpec{"Small", "Base", picture(3), false});
    map.addSurface(twice).addSurface(once).addSurface(twice).addSurface(small);
    map.shapeSurface({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}}}, {0, 0, 1});
    MemoryPackages packages = memoryPackagesFor(fixture);
    const std::vector<std::uint8_t> bytes = fixture.map.build();
    const auto package = uta::upkg::Package::open(uta::test::asBytes(bytes));
    REQUIRE(package.has_value());
    const TileLimits unsure{0, 1e300, 0, 1e300};
    uta::JobSystem jobs(2);
    const auto result = uta::ubake::detail::bake(*package, MAP_NAME, packages.resolver(), jobs, &uta::umat::curated,
                                                 uta::umat::TEXTURE_BUDGET_BYTES, nullptr, uta::umat::isFlame,
                                                 nullptr, unsure);
    REQUIRE(result.has_value());
    REQUIRE(result->bundle.materials.has_value());
    for (const uta::ubundle::MaterialRecord& record : *result->bundle.materials) {
        INFO(record.id);
        const bool isSmall = record.id.find("small") != std::string::npos;
        CHECK(record.tileKind == (isSmall ? TileKind::Fixed : TileKind::Unsure));
        CHECK((record.tileHash == std::array<std::byte, 32>{}) == isSmall);
    }
    // Most surfaces first, though "once" sorts before "twice" by id.
    REQUIRE(result->tileQuestions.size() == 2);
    CHECK(result->tileQuestions[0].material.find("twice") != std::string::npos);
    CHECK(result->tileQuestions[0].surfaces == 2);
    CHECK(result->tileQuestions[1].material.find("once") != std::string::npos);
    CHECK(result->tileQuestions[1].surfaces == 1);
}
