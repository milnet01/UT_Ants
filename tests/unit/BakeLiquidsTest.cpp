// UTA-0105: the bake's liquid looks -- each Wet, Ice and Wave texture's own
// settings over its class's defaults, and a Wave's palette ramp.
//
// docs/specs/UTA-0105-shader-liquids.md SS 4.1 to SS 4.3, SS 6, INV-2, INV-3.
//
// THE FIRE PACKAGE IS A STAND-IN. firePackage() holds the five classes with
// the defaults this needs, so the default rule is graded by a value the
// fixture chose rather than one Fire.u happens to hold.
//
// NO TEST NAME CONTAINS A COMMA (BundleMaterialTest.cpp says why).

#include "BakeFixture.h"

#include "core/Jobs.h"
#include "ubake/Bake.h"
#include "ubake/LightModel.h"
#include "ubundle/Bundle.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

using namespace uta::test::bake;
using uta::JobSystem;
using uta::ubake::BakeResult;
using uta::ubundle::LiquidKind;
using uta::ubundle::LiquidLook;
namespace detail = uta::ubake::detail;

namespace {

/// A procedural texture: its size, no stored pixels, and its own settings.
TextureSpec procedural(std::string name, std::string className, std::uint8_t seed,
                       std::vector<std::pair<std::string, std::uint8_t>> bytes) {
    TextureSpec spec;
    spec.name = std::move(name);
    spec.picture = picture(seed);
    spec.className = std::move(className);
    spec.emptyLevel = true;
    spec.bytes = std::move(bytes);
    return spec;
}

/// Four colours from brightest to darkest, so a ramp taken in palette order
/// runs the wrong way.
const std::vector<std::array<std::uint8_t, 4>> REVERSED = {
    {250, 240, 230, 255}, {170, 160, 150, 255}, {90, 80, 70, 255}, {10, 20, 30, 255}};

const uta::ubundle::MaterialRecord& recordNamed(const BakeResult& result, const std::string& id) {
    REQUIRE(result.bundle.materials.has_value());
    const auto& records = *result.bundle.materials;
    const auto found = std::ranges::find(records, id, &uta::ubundle::MaterialRecord::id);
    REQUIRE(found != records.end());
    return *found;
}

std::array<float, 3> linear(const std::array<std::uint8_t, 4>& colour) {
    return {static_cast<float>(uta::ubake::linearOf(colour[0])), static_cast<float>(uta::ubake::linearOf(colour[1])),
            static_cast<float>(uta::ubake::linearOf(colour[2]))};
}

/// The fixture: one texture of each liquid class, a second Wet storing no
/// WaveAmp, a plain texture and a FireTexture, each on one surface.
BakeResult bakeLiquids(bool withFire) {
    Fixture fixture;
    MapBuilder& map = fixture.map;
    TextureSpec ripple = procedural("Ripple", "WaveTexture", 7,
                                    {{"WaveAmp", 128}, {"FX_Frequency", 9}, {"BumpMapLight", 50},
                                     {"BumpMapAngle", 170}, {"PhongSize", 32}});
    ripple.picture.palette = REVERSED;
    TextureSpec shield = procedural("Shield", "FireTexture", 8, {});
    shield.sparks = {{0, 200, 1, 3, 0, 0, 0, 0}};
    // A fire's still is heat, so its palette spans all 256 (BakeFlamesTest's shield).
    shield.picture.palette.clear();
    for (int heat = 0; heat < 256; ++heat)
        shield.picture.palette.push_back({0, std::uint8_t(heat / 2), std::uint8_t(heat), 255});
    // UTA-0270: the Laser's glass, one grey of 110 throughout, on no surface.
    TextureSpec mist{"Mist", "", picture(10), false};
    mist.picture.indices.assign(mist.picture.indices.size(), 0);
    mist.picture.palette = {{110, 110, 110, 255}};
    const auto glass = map.addTexture(mist);
    TextureSpec laser = procedural("Laser", "IceTexture", 6,
                                   {{"PanningStyle", 1}, {"HorizPanSpeed", 64}, {"Amplitude", 44}, {"Frequency", 11}});
    laser.glassTexture = glass;
    laser.moveIce = true;
    for (const TextureSpec& texture :
         {procedural("Pool", "WetTexture", 4, {{"WaveAmp", 200}, {"FX_Frequency", 8}}),
          procedural("Drip", "WetTexture", 5, {{"FX_Frequency", 3}}), laser,
          ripple, TextureSpec{"Wall", "", picture(9), false}, shield})
        map.addSurface(map.addTexture(texture));

    MemoryPackages packages = memoryPackagesFor(fixture);
    if (withFire) packages.add("fire", firePackage());
    JobSystem jobs(2);
    const detail::CuratedLookup nothingCurated = [](std::uint64_t) -> const uta::umat::CuratedOverride* {
        return nullptr;
    };
    const detail::FlameLookup noFlames = [](std::uint64_t) { return false; };
    // A Package views its bytes, so they outlive it.
    const std::vector<std::byte> bytes = asByteVector(map.build());
    const auto opened = uta::upkg::Package::open(bytes);
    REQUIRE(opened.has_value());
    auto baked = detail::bake(*opened, MAP_NAME, packages.resolver(), jobs, nothingCurated,
                              uta::umat::TEXTURE_BUDGET_BYTES, nullptr, noFlames);
    if (!baked.has_value()) FAIL("the bake was refused: " << baked.error().message());
    for (const auto& skipped : baked->skipped) UNSCOPED_INFO("skipped " << skipped.material << ": " << skipped.reason);
    REQUIRE(baked->skipped.empty());
    return std::move(*baked);
}

} // namespace

TEST_CASE("UTA-0105 INV-2: each liquid class carries its own settings over its class defaults",
          "[ubake][bake][liquids]") {
    const BakeResult result = bakeLiquids(true);
    CHECK(result.skippedLiquids.empty());

    SECTION("a Wet texture carries its stored WaveAmp and FX_Frequency") {
        const auto& look = recordNamed(result, "dm-fixture.pool").liquid;
        REQUIRE(look.has_value());
        CHECK(look->kind == LiquidKind::Wet);
        CHECK(look->amplitude == 200);
        CHECK(look->frequency == 8);
        CHECK(look->size == std::array<std::uint16_t, 2>{4, 4});
        CHECK(look->pan == std::array<std::uint8_t, 2>{128, 128});
        CHECK(look->ramp == LiquidLook{}.ramp);
    }
    SECTION("a WaveAmp the texture does not store is its class's default") {
        // A bake reading stored settings alone gives 0 here.
        const auto& look = recordNamed(result, "dm-fixture.drip").liquid;
        REQUIRE(look.has_value());
        CHECK(look->amplitude == FIRE_DEFAULT_WAVEAMP);
        CHECK(look->frequency == 3);
    }
    SECTION("an Ice texture carries its panning and its unstored pan speed's default") {
        const auto& look = recordNamed(result, "dm-fixture.laser").liquid;
        REQUIRE(look.has_value());
        CHECK(look->kind == LiquidKind::Ice);
        CHECK(look->panning == 1);
        CHECK(look->pan == std::array<std::uint8_t, 2>{64, 128});
        CHECK(look->amplitude == 44);
        CHECK(look->frequency == 11);
        CHECK(int(look->moveIce) == 1); // UTA-0270
    }
    SECTION("a Wave texture carries its bump settings") {
        const auto& look = recordNamed(result, "dm-fixture.ripple").liquid;
        REQUIRE(look.has_value());
        CHECK(look->kind == LiquidKind::Wave);
        CHECK(look->amplitude == 128);
        CHECK(look->frequency == 9);
        CHECK(look->bump == std::array<std::uint8_t, 3>{50, 170, 32});
    }
    SECTION("a plain texture and a FireTexture carry none") {
        CHECK_FALSE(recordNamed(result, "dm-fixture.wall").liquid.has_value());
        CHECK_FALSE(recordNamed(result, "dm-fixture.shield").liquid.has_value());
    }
}

TEST_CASE("UTA-0105 INV-3: a Wave texture's ramp runs from its darkest colour to its brightest",
          "[ubake][bake][liquids]") {
    // Four colours over eight entries: positions 0 0 1 1 2 2 3 3 of the luma
    // order, which runs opposite to the palette's.
    const BakeResult result = bakeLiquids(true);
    const auto& look = recordNamed(result, "dm-fixture.ripple").liquid;
    REQUIRE(look.has_value());
    const std::array<std::size_t, 8> paletteEntry{3, 3, 2, 2, 1, 1, 0, 0};
    for (std::size_t i = 0; i < 8; ++i) CHECK(look->ramp[i] == linear(REVERSED[paletteEntry[i]]));
}

TEST_CASE("UTA-0105 SS 6: a liquid whose class is not in the install keeps its still and is reported",
          "[ubake][bake][liquids]") {
    const BakeResult result = bakeLiquids(false);
    CHECK_FALSE(recordNamed(result, "dm-fixture.pool").liquid.has_value());
    REQUIRE(result.skippedLiquids.size() == 4);
    CHECK(std::ranges::any_of(result.skippedLiquids, [](const auto& s) { return s.material == "dm-fixture.pool"; }));
    CHECK_FALSE(result.skippedLiquids[0].reason.empty());
}

TEST_CASE("UTA-0270: an Ice texture's glass bakes as a one-level BC4 picture of its grey", "[ubake][bake][liquids]") {
    const BakeResult result = bakeLiquids(true);
    REQUIRE(result.bundle.textures.has_value());
    const auto& textures = *result.bundle.textures;
    const auto found = std::ranges::find(textures, std::string("dm-fixture.laser:glass"),
                                         &uta::ubundle::CompressedTexture::name);
    REQUIRE(found != textures.end());
    CHECK(found->format == uta::ubundle::BlockFormat::BC4);
    CHECK(found->mipCount == 1);
    CHECK(found->width == 4);
    CHECK(found->height == 4);
    REQUIRE(found->blocks.size() == 8);
    // BC4: two endpoints, then sixteen 3-bit indices. Every texel 110.
    const auto e0 = int(found->blocks[0]), e1 = int(found->blocks[1]);
    std::uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= std::uint64_t(found->blocks[2 + i]) << (8 * i);
    for (int t = 0; t < 16; ++t) {
        const int index = int((bits >> (3 * t)) & 7);
        int value = 0;
        if (index == 0) value = e0;
        else if (index == 1) value = e1;
        else if (e0 > e1) value = ((8 - index) * e0 + (index - 1) * e1) / 7;
        else if (index < 6) value = ((6 - index) * e0 + (index - 1) * e1) / 5;
        else value = index == 6 ? 0 : 255;
        CHECK(std::abs(value - 110) <= 1);
    }
    // A liquid naming no glass bakes none.
    CHECK(std::ranges::find(textures, std::string("dm-fixture.pool:glass"), &uta::ubundle::CompressedTexture::name)
          == textures.end());
}

