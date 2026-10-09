// UTA-0256: the bake's added lamps -- INV-4, INV-5, INV-6, and INV-7's
// bundle half: off, a lamp's bundle is a bake without it.
//
// docs/specs/UTA-0256-added-lamps.md SS 4.3.
//
// THE FLAME LIST IS INJECTED, as BakeFlamesTest.cpp's is: no synthetic picture
// reproduces a reference FireTexture's still.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator, so
// a name carrying one silently matches nothing when run by name.

#include "BakeFixture.h"

#include "core/Jobs.h"
#include "ubake/Bake.h"
#include "ubake/Geometry.h"
#include "ubake/Lamps.h"
#include "ubundle/Bundle.h"
#include "umat/Fingerprint.h"
#include "upkg/Package.h"
#include "upkg/Texture.h"
#include "urecipe/Recipe.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

using namespace uta::test::bake;
using uta::JobSystem;
using uta::ubake::BakeResult;
using uta::ubake::PF_NOT_SOLID;
using uta::ubake::PF_TRANSLUCENT;
using uta::urecipe::AddedLamp;
using uta::urecipe::Recipe;
namespace detail = uta::ubake::detail;
namespace ubundle = uta::ubundle;

namespace {

using Square = std::array<std::array<float, 3>, 4>;

/// An upright square across x at `y`, from `z0` to `z1`, facing -y.
Square acrossX(float x0, float x1, float y, float z0, float z1) {
    return {{{x0, y, z0}, {x1, y, z0}, {x1, y, z1}, {x0, y, z1}}};
}

/// An upright square across y at `x`, from `z0` to `z1`, facing +x.
Square acrossY(float x, float y0, float y1, float z0, float z1) {
    return {{{x, y0, z0}, {x, y1, z0}, {x, y1, z1}, {x, y0, z1}}};
}

std::uint64_t fingerprintOf(const Picture& shown) {
    std::vector<std::byte> indices;
    for (const std::uint8_t index : shown.indices) indices.push_back(static_cast<std::byte>(index));
    uta::upkg::Mip mip;
    mip.pixels = indices;
    mip.width = shown.width;
    mip.height = shown.height;
    uta::upkg::Palette palette;
    for (const auto& colour : shown.palette) palette.entries.push_back({colour[0], colour[1], colour[2], colour[3]});
    const auto key = uta::umat::pictureFingerprint(mip, palette);
    REQUIRE(key.has_value());
    return *key;
}

/// The light "Lamp0" at (32, 32, 96), static so its bounce bakes; a floor of
/// nine 64-unit squares; "Brush1", a holder plate on the wall side x = 0 from
/// z = 64 to 80; "Brush2", a crossed pair of flame sheets standing on z = 80.
/// "Brush3" owns nothing. Surface 0 of the floor sits under the light.
struct Room {
    Fixture fixture;
    std::uint64_t torchKey = 0;
    std::vector<std::byte> bytes;
};

Room room() {
    Room out;
    MapBuilder& map = out.fixture.map;
    const Picture torchPicture = picture(5);
    out.torchKey = fingerprintOf(torchPicture);
    const std::int32_t wall = map.addTexture(TextureSpec{"Wall", "", picture(3), false});
    const std::int32_t torch = map.addTexture(TextureSpec{"Torch", "", torchPicture, false});
    const std::int32_t brushClass = map.importClass("Engine", "Brush");
    // Actor 0: the light.
    map.addActorOfClass("ActorPkg", "Lamp",
                        {vectorProperty("Location", 32.0F, 32.0F, 96.0F), boolProperty("bStatic", true),
                         byteProperty("LightRadius", 16)});
    // Actors 1 to 3: the brushes.
    map.addActor("Brush1", brushClass);
    map.addActor("Brush2", brushClass);
    map.addActor("Brush3", brushClass);
    for (int square = 0; square < 9; ++square) map.addSurface(wall);
    // The holder: a plate across y at x = 8, facing +x.
    map.addSurface(wall).shapeSurface(acrossY(8, 16, 48, 64, 80), {1, 0, 0}).ownSurface(1, 0);
    // The flame: crossed sheets standing on (32, 32, 80).
    map.addSurface(torch, PF_NOT_SOLID | PF_TRANSLUCENT)
        .shapeSurface(acrossX(16, 48, 32, 80, 112), {0, -1, 0})
        .ownSurface(2, 0);
    map.addSurface(torch, PF_NOT_SOLID | PF_TRANSLUCENT)
        .shapeSurface(acrossY(32, 16, 48, 80, 112), {1, 0, 0})
        .ownSurface(2, 1);
    out.bytes = asByteVector(map.build());
    return out;
}

Recipe recipeWith(std::vector<AddedLamp> lamps) {
    Recipe recipe;
    recipe.map = std::string(MAP_NAME);
    recipe.mapDigest = std::array<std::byte, 32>{};
    recipe.lamps = std::move(lamps);
    return recipe;
}

/// The standard lamp: Lamp0 with its holder and flame, moved to (160, 96, 96)
/// and turned a quarter.
AddedLamp quarterLamp() {
    return AddedLamp{.name = "east", .light = "Lamp0", .fitting = {"Brush1", "Brush2"}, .at = {160, 96, 96},
                     .yaw = 16384};
}

uta::Result<BakeResult> baked(const Room& room, const Recipe* recipe) {
    MemoryPackages packages = memoryPackagesFor(room.fixture);
    JobSystem jobs(2);
    const detail::CuratedLookup nothingCurated = [](std::uint64_t) -> const uta::umat::CuratedOverride* {
        return nullptr;
    };
    const std::uint64_t torchKey = room.torchKey;
    const detail::FlameLookup isFlame = [torchKey](std::uint64_t fingerprint) { return fingerprint == torchKey; };
    const auto opened = uta::upkg::Package::open(room.bytes);
    REQUIRE(opened.has_value());
    return detail::bake(*opened, MAP_NAME, packages.resolver(), jobs, nothingCurated,
                        uta::umat::TEXTURE_BUDGET_BYTES, nullptr, isFlame, recipe);
}

BakeResult bakedOk(const Room& room, const Recipe* recipe) {
    auto result = baked(room, recipe);
    if (!result.has_value()) FAIL("the bake was refused: " << result.error().message());
    return std::move(*result);
}

/// Each pair of a light below `below`, as its chart, light, and the texels of
/// its rectangle -- what INV-6 compares, whatever the atlas packing.
std::vector<std::tuple<std::size_t, std::uint32_t, std::vector<std::uint8_t>>> pairsBelow(
    const ubundle::ShadowMask& mask, std::uint32_t below) {
    std::vector<std::tuple<std::size_t, std::uint32_t, std::vector<std::uint8_t>>> out;
    for (std::size_t c = 0; c < mask.charts.size(); ++c) {
        const ubundle::MaskChart& chart = mask.charts[c];
        for (std::uint32_t k = chart.firstPair; k < chart.firstPair + chart.pairCount; ++k) {
            const ubundle::MaskPair& pair = mask.pairs[k];
            if (pair.light >= below) continue;
            std::vector<std::uint8_t> texels;
            if (pair.x != ubundle::MASK_ALL_LIT)
                for (std::uint32_t y = 0; y < chart.height; ++y)
                    for (std::uint32_t x = 0; x < chart.width; ++x)
                        texels.push_back(mask.texels[(pair.y + y) * mask.width + pair.x + x]);
            out.emplace_back(c, pair.light, std::move(texels));
        }
    }
    return out;
}

} // namespace

TEST_CASE("UTA-0256 INV-4: placedPoint turns about the pivot and then moves", "[ubake][lamp]") {
    const auto point = uta::ubake::placedPoint({10, 0, 5}, {0, 0, 0}, {100, 200, 300}, 16384);
    CHECK(point[0] == Catch::Approx(100).margin(1e-4));
    CHECK(point[1] == Catch::Approx(210).margin(1e-4));
    CHECK(point[2] == Catch::Approx(305));
    const auto offPivot = uta::ubake::placedPoint({12, 2, 0}, {2, 2, 0}, {0, 0, 0}, 32768);
    CHECK(offPivot[0] == Catch::Approx(-10).margin(1e-4));
    CHECK(offPivot[1] == Catch::Approx(0).margin(1e-4));
}

TEST_CASE("UTA-0256 INV-4: a lamp copies its light and its fitting moved and turned", "[ubake][lamp]") {
    const Room fixture = room();
    const Recipe recipe = recipeWith({quarterLamp()});
    const BakeResult with = bakedOk(fixture, &recipe);
    const BakeResult without = bakedOk(fixture, nullptr);

    REQUIRE(with.bundle.lamps.has_value());
    REQUIRE(with.bundle.lamps->size() == 1);
    const ubundle::AddedLamp& lamp = with.bundle.lamps->front();
    REQUIRE(without.bundle.lights->size() == 1);
    const ubundle::Light& original = without.bundle.lights->front();

    SECTION("the light is the template's but for its place and yaw") {
        CHECK(lamp.light.location == std::array<float, 3>{160, 96, 96});
        CHECK(lamp.light.rotation[1] == original.rotation[1] + 16384);
        CHECK(lamp.light.exportIndex == original.exportIndex);
        CHECK(lamp.light.brightness == original.brightness);
        CHECK(lamp.light.radius == original.radius);
        CHECK(lamp.light.hue == original.hue);
        CHECK(lamp.light.levelBrightness == original.levelBrightness);
    }
    SECTION("the holder plate is turned a quarter about the light") {
        // Corner (8, 16, 64) is (-24, -16) from the light; a quarter turn puts it
        // at (16, -24), so at (176, 72, 64). Its +x normal turns to +y.
        REQUIRE_FALSE(lamp.shape.indices.empty());
        const auto near = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
            return std::abs(a[0] - b[0]) < 1e-3 && std::abs(a[1] - b[1]) < 1e-3 && std::abs(a[2] - b[2]) < 1e-3;
        };
        CHECK(std::ranges::any_of(lamp.shape.vertices,
                                  [&](const ubundle::GeometryVertex& v) { return near(v.position, {176, 72, 64}); }));
        for (const ubundle::GeometryVertex& vertex : lamp.shape.vertices) {
            CHECK(vertex.normal[0] == Catch::Approx(0).margin(1e-5));
            CHECK(vertex.normal[1] == Catch::Approx(1).margin(1e-5));
            CHECK(vertex.position[1] == Catch::Approx(72).margin(1e-3)); // the plate turned across x
        }
    }
    SECTION("the crossed sheets give one flame at the moved foot") {
        REQUIRE(lamp.flames.size() == 1);
        CHECK(lamp.flames[0].base[0] == Catch::Approx(160).margin(1e-3));
        CHECK(lamp.flames[0].base[1] == Catch::Approx(96).margin(1e-3));
        CHECK(lamp.flames[0].base[2] == Catch::Approx(80).margin(1e-3));
        CHECK(lamp.flames[0].light == -1);
        REQUIRE(without.bundle.flames->size() == 1);
        CHECK(lamp.flames[0].width == without.bundle.flames->front().width);
    }
}

TEST_CASE("UTA-0256 INV-5: a lamp that cannot be built is refused naming it", "[ubake][lamp]") {
    const Room fixture = room();
    const auto refused = [&fixture](AddedLamp lamp, const std::string& says) {
        const Recipe recipe = recipeWith({quarterLamp(), std::move(lamp)});
        const auto result = baked(fixture, &recipe);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == uta::ErrorCode::MalformedData);
        const std::string message(result.error().message());
        INFO(message);
        CHECK(message.find("recipe lamp bad: " + says) != std::string::npos);
    };
    AddedLamp bad = quarterLamp();
    bad.name = "bad";
    SECTION("a light the map lacks") {
        bad.light = "Lamp9";
        refused(bad, "Lamp9 is not a light of the map");
    }
    SECTION("a light that is a brush") {
        bad.light = "Brush1";
        refused(bad, "Brush1 is not a light of the map");
    }
    SECTION("a brush the map lacks") {
        bad.fitting = {"Brush1", "Brush9"};
        refused(bad, "Brush9 is not in the map");
    }
    SECTION("a brush that gives nothing") {
        bad.fitting = {"Brush1", "Brush3"};
        refused(bad, "Brush3 gives neither a drawn surface nor a flame");
    }
}

TEST_CASE("UTA-0256 INV-6: the map's own bake is untouched by a lamp", "[ubake][lamp]") {
    const Room fixture = room();
    const Recipe recipe = recipeWith({quarterLamp()});
    const BakeResult with = bakedOk(fixture, &recipe);
    const BakeResult without = bakedOk(fixture, nullptr);

    CHECK_FALSE(without.bundle.lamps.has_value());
    // GEOM and FLAM: the fitting's originals stay, nothing is added.
    CHECK(with.bundle.geometry->vertices.size() == without.bundle.geometry->vertices.size());
    CHECK(with.bundle.geometry->indices == without.bundle.geometry->indices);
    CHECK(with.bundle.flames->size() == without.bundle.flames->size());
    CHECK(with.bundle.lights->size() == without.bundle.lights->size());
    REQUIRE(with.bundle.occlusion.has_value());
    CHECK(with.bundle.occlusion->texels == without.bundle.occlusion->texels);

    // LPRB: the map's cubes are the same; the lamp's bounce is apart.
    const auto& probes = *with.bundle.lightProbes;
    REQUIRE(probes.probes.size() == without.bundle.lightProbes->probes.size());
    REQUIRE_FALSE(probes.probes.empty());
    for (std::size_t p = 0; p < probes.probes.size(); ++p) {
        CAPTURE(p);
        CHECK(probes.probes[p].cell == without.bundle.lightProbes->probes[p].cell);
        CHECK(probes.probes[p].cube == without.bundle.lightProbes->probes[p].cube);
    }
    REQUIRE(probes.added.size() == probes.probes.size());
    float most = 0;
    for (const auto& cube : probes.added)
        for (const auto& face : cube) most = std::max({most, face[0], face[1], face[2]});
    CHECK(most > 0);

    // SMSK: every pair of the map's own light is as it was; the lamp has its own.
    REQUIRE(with.bundle.shadowMask.has_value());
    REQUIRE(without.bundle.shadowMask.has_value());
    const auto lite = static_cast<std::uint32_t>(with.bundle.lights->size());
    CHECK(pairsBelow(*with.bundle.shadowMask, lite) == pairsBelow(*without.bundle.shadowMask, lite));
    CHECK(std::ranges::any_of(with.bundle.shadowMask->pairs,
                              [lite](const ubundle::MaskPair& pair) { return pair.light == lite; }));
}

TEST_CASE("UTA-0256 INV-7: off leaves a lamp's bundle as a bake without it", "[ubake][lamp]") {
    const Room fixture = room();
    const Recipe recipe = recipeWith({quarterLamp()});
    BakeResult with = bakedOk(fixture, &recipe);
    const BakeResult without = bakedOk(fixture, nullptr);
    ubundle::applyAddedLamps(with.bundle, false);

    CHECK_FALSE(with.bundle.lamps.has_value());
    CHECK(with.bundle.lights->size() == without.bundle.lights->size());
    CHECK(with.bundle.flames->size() == without.bundle.flames->size());
    CHECK(with.bundle.movers->size() == without.bundle.movers->size());
    CHECK(with.bundle.lightProbes->added.empty());
    for (std::size_t p = 0; p < with.bundle.lightProbes->probes.size(); ++p)
        CHECK(with.bundle.lightProbes->probes[p].cube == without.bundle.lightProbes->probes[p].cube);
    const auto all = std::numeric_limits<std::uint32_t>::max();
    CHECK(pairsBelow(*with.bundle.shadowMask, all) == pairsBelow(*without.bundle.shadowMask, all));
    CHECK(with.bundle.shadowMask->pairs.size() == without.bundle.shadowMask->pairs.size());
}

TEST_CASE("UTA-0256 INV-7: on folds a lamp into what the renderer draws", "[ubake][lamp]") {
    const Room fixture = room();
    const Recipe recipe = recipeWith({quarterLamp()});
    BakeResult with = bakedOk(fixture, &recipe);
    const auto added = with.bundle.lightProbes->added;
    const auto before = with.bundle.lightProbes->probes;
    const std::size_t lite = with.bundle.lights->size();
    const std::size_t flames = with.bundle.flames->size();
    const std::size_t movers = with.bundle.movers->size();
    ubundle::applyAddedLamps(with.bundle, true);

    CHECK_FALSE(with.bundle.lamps.has_value());
    REQUIRE(with.bundle.lights->size() == lite + 1);
    CHECK(with.bundle.lights->back().location == std::array<float, 3>{160, 96, 96});
    REQUIRE(with.bundle.flames->size() == flames + 1);
    CHECK(with.bundle.flames->back().light == static_cast<std::int32_t>(lite));
    REQUIRE(with.bundle.movers->size() == movers + 1);
    CHECK(with.bundle.movers->back().location == std::array<float, 3>{0, 0, 0});
    CHECK(with.bundle.lightProbes->added.empty());
    for (std::size_t p = 0; p < before.size(); ++p)
        for (std::size_t face = 0; face < 6; ++face)
            CHECK(with.bundle.lightProbes->probes[p].cube[face][0] == before[p].cube[face][0] + added[p][face][0]);
}
