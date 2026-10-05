// UTA-0286: the bake's fire looks -- a non-flame FireTexture's sparks,
// settings and palette, which the renderer replays.
//
// docs/specs/UTA-0286-replayed-fire-textures.md SS 4.3, SS 6, INV-2.
//
// The flame rule is graded by baking one fixture twice: once with no flame on
// the list, once with every picture on it. Only the list differs.
//
// NO TEST NAME CONTAINS A COMMA (BundleMaterialTest.cpp says why).

#include "BakeFixture.h"

#include "core/Jobs.h"
#include "ubake/Bake.h"
#include "ubake/Geometry.h"
#include "ubundle/Bundle.h"
#include "upkg/Package.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace uta::test::bake;
using uta::JobSystem;
using uta::ubake::BakeResult;
using uta::ubundle::FireSpark;
namespace detail = uta::ubake::detail;

namespace {

/// A FireTexture: its size, no stored pixels, its sparks, and a palette
/// spanning all 256 heats whose entries all differ.
TextureSpec fireTexture(std::string name, std::uint8_t seed, std::vector<std::array<std::uint8_t, 8>> sparks) {
    TextureSpec spec;
    spec.name = std::move(name);
    spec.picture = picture(seed);
    spec.className = "FireTexture";
    spec.emptyLevel = true;
    spec.sparks = std::move(sparks);
    spec.picture.palette.clear();
    for (int heat = 0; heat < 256; ++heat)
        spec.picture.palette.push_back({std::uint8_t(heat), std::uint8_t(255 - heat), std::uint8_t(heat / 3), 255});
    return spec;
}

const uta::ubundle::MaterialRecord& recordNamed(const BakeResult& result, const std::string& id) {
    REQUIRE(result.bundle.materials.has_value());
    const auto& records = *result.bundle.materials;
    const auto found = std::ranges::find(records, id, &uta::ubundle::MaterialRecord::id);
    REQUIRE(found != records.end());
    return *found;
}

/// Spray stores every setting and is drawn masked; Quiet stores none; Wall is
/// a plain texture; Pool a WetTexture.
BakeResult bakeFires(bool everythingAFlame) {
    Fixture fixture;
    MapBuilder& map = fixture.map;
    TextureSpec spray = fireTexture("Spray", 4, {{25, 200, 4, 5, 6, 7, 8, 9}, {13, 150, 10, 11, 12, 13, 14, 15}});
    spray.renderHeat = 230;
    spray.rising = true;
    spray.sparksLimit = 64;
    spray.maxFrameRate = 20.0f;
    TextureSpec pool;
    pool.name = "Pool";
    pool.picture = picture(7);
    pool.className = "WetTexture";
    pool.emptyLevel = true;
    map.addSurface(map.addTexture(spray), uta::ubake::PF_MASKED);
    map.addSurface(map.addTexture(fireTexture("Quiet", 5, {{0, 200, 1, 3, 0, 0, 0, 0}})));
    map.addSurface(map.addTexture(TextureSpec{"Wall", "", picture(9), false}));
    map.addSurface(map.addTexture(pool));

    MemoryPackages packages = memoryPackagesFor(fixture);
    packages.add("fire", firePackage());
    JobSystem jobs(2);
    const detail::CuratedLookup nothingCurated = [](std::uint64_t) -> const uta::umat::CuratedOverride* {
        return nullptr;
    };
    const detail::FlameLookup isFlame = [everythingAFlame](std::uint64_t) { return everythingAFlame; };
    // A Package views its bytes, so they outlive it.
    const std::vector<std::byte> bytes = asByteVector(map.build());
    const auto opened = uta::upkg::Package::open(bytes);
    REQUIRE(opened.has_value());
    auto baked = detail::bake(*opened, MAP_NAME, packages.resolver(), jobs, nothingCurated,
                              uta::umat::TEXTURE_BUDGET_BYTES, nullptr, isFlame);
    if (!baked.has_value()) FAIL("the bake was refused: " << baked.error().message());
    for (const auto& skipped : baked->skipped) UNSCOPED_INFO("skipped " << skipped.material << ": " << skipped.reason);
    REQUIRE(baked->skipped.empty());
    return std::move(*baked);
}

} // namespace

TEST_CASE("UTA-0286 INV-2: a non-flame FireTexture carries its sparks and settings and palette",
          "[ubake][bake][fire]") {
    const BakeResult result = bakeFires(false);
    CHECK(result.skippedFires.empty());

    const auto& spray = recordNamed(result, "dm-fixture.spray#masked");
    REQUIRE(spray.fire.has_value());
    const Picture shape = picture(4);
    CHECK(spray.fire->size == std::array<std::uint16_t, 2>{std::uint16_t(shape.width), std::uint16_t(shape.height)});
    CHECK(spray.fire->renderHeat == 230);
    CHECK(spray.fire->rising == 1);
    CHECK(spray.fire->masked == 1);
    CHECK(spray.fire->sparksLimit == 64);
    CHECK(spray.fire->maxFrameRate == 20.0f);
    CHECK(spray.fire->sparks
          == std::vector<FireSpark>{{25, 200, 4, 5, 6, 7, 8, 9}, {13, 150, 10, 11, 12, 13, 14, 15}});
    CHECK(spray.fire->palette[0] == std::array<std::uint8_t, 3>{0, 255, 0});
    CHECK(spray.fire->palette[255] == std::array<std::uint8_t, 3>{255, 0, 85});
    CHECK_FALSE(spray.flame.has_value());

    // A setting it does not store reads 0 or false, and an unmasked surface
    // wears the opaque variant.
    const auto& quiet = recordNamed(result, "dm-fixture.quiet");
    REQUIRE(quiet.fire.has_value());
    CHECK(quiet.fire->renderHeat == 0);
    CHECK(quiet.fire->rising == 0);
    CHECK(quiet.fire->masked == 0);
    CHECK(quiet.fire->sparksLimit == 0);
    CHECK(quiet.fire->maxFrameRate == 0.0f);

    CHECK_FALSE(recordNamed(result, "dm-fixture.wall").fire.has_value());
    CHECK_FALSE(recordNamed(result, "dm-fixture.pool").fire.has_value());
}

TEST_CASE("UTA-0286 INV-2: a FireTexture the flame list names carries no fire look", "[ubake][bake][fire]") {
    const BakeResult result = bakeFires(true);
    const auto& spray = recordNamed(result, "dm-fixture.spray#masked");
    CHECK(spray.flame.has_value());
    CHECK_FALSE(spray.fire.has_value());
    CHECK_FALSE(recordNamed(result, "dm-fixture.quiet").fire.has_value());
}
