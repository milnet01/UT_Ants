// UTA-0263: the bake's flames -- each flame material's look, and the sheets
// that leave GEOM to become FLAM records.
//
// docs/specs/UTA-0263-shader-flames.md SS 4.2, SS 4.3, SS 4.5, SS 6, INV-4.
//
// THE FLAME LIST IS INJECTED. The real list is keyed by the stills of the
// reference install's FireTextures, which no synthetic picture reproduces, so
// the bake is told which fingerprint is a flame, as BakeTest tells it which is
// curated.

#include "BakeFixture.h"

#include "core/Jobs.h"
#include "ubake/Bake.h"
#include "ubake/Geometry.h"
#include "ubake/LightModel.h"
#include "ubundle/Bundle.h"
#include "umat/Fingerprint.h"
#include "upkg/Package.h"
#include "upkg/Texture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

using namespace uta::test::bake;
using uta::JobSystem;
using uta::ubake::BakeResult;
using uta::ubake::PF_MASKED;
using uta::ubake::PF_NOT_SOLID;
using uta::ubake::PF_TRANSLUCENT;
namespace detail = uta::ubake::detail;

namespace {

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

using Square = std::array<std::array<float, 3>, 4>;

/// An upright square across x, facing -y, standing on z = 0.
Square acrossX(float x0, float x1, float y, float top) {
    return {{{x0, y, 0}, {x1, y, 0}, {x1, y, top}, {x0, y, top}}};
}

/// An upright square across y, facing +x, standing on z = 0.
Square acrossY(float x, float y0, float y1, float top) {
    return {{{x, y0, 0}, {x, y1, 0}, {x, y1, top}, {x, y0, top}}};
}

/// Whether GEOM draws a corner that `where` accepts.
bool draws(const BakeResult& result, const std::function<bool(const std::array<float, 3>&)>& where) {
    REQUIRE(result.bundle.geometry.has_value());
    return std::ranges::any_of(result.bundle.geometry->vertices,
                               [&](const uta::ubundle::GeometryVertex& vertex) { return where(vertex.position); });
}

const uta::ubundle::MaterialRecord& recordNamed(const BakeResult& result, const std::string& id) {
    REQUIRE(result.bundle.materials.has_value());
    const auto& records = *result.bundle.materials;
    const auto found = std::ranges::find(records, id, &uta::ubundle::MaterialRecord::id);
    REQUIRE(found != records.end());
    return *found;
}

} // namespace

TEST_CASE("UTA-0263 INV-4: flame sheets leave GEOM as FLAM records, crossed sheets merged",
          "[ubake][bake][flames]") {
    constexpr std::uint32_t SHEET = PF_NOT_SOLID | PF_TRANSLUCENT;
    const Picture torchPicture = picture(5);
    const std::uint64_t torchKey = fingerprintOf(torchPicture);

    // A FireTexture whose still is not on the list: a shield, as UT99 draws
    // shields with the same class and sparks as fire.
    TextureSpec shield;
    shield.name = "Shield";
    shield.picture = picture(6); // its size; the level stores no pixels
    shield.picture.palette.clear();
    for (int heat = 0; heat < 256; ++heat)
        shield.picture.palette.push_back({0, std::uint8_t(heat / 2), std::uint8_t(heat), 255});
    shield.className = "FireTexture";
    shield.emptyLevel = true;
    shield.sparks = {{0, 200, 1, 3, 0, 0, 0, 0}};

    Fixture fixture;
    MapBuilder& map = fixture.map;
    const std::int32_t torch = map.addTexture(TextureSpec{"Torch", "", torchPicture, false});
    const std::int32_t notFlame = map.addTexture(shield);
    // Surface 0: a lone torch sheet, 32 wide and 64 tall, its foot at (16, 200, 0).
    map.addSurface(torch, SHEET).shapeSurface(acrossX(0, 32, 200, 64), {0, -1, 0});
    // Surfaces 1 and 2: a crossed pair standing on (400, 0, 0).
    map.addSurface(torch, SHEET).shapeSurface(acrossX(384, 416, 0, 64), {0, -1, 0});
    map.addSurface(torch, SHEET).shapeSurface(acrossY(400, -16, 16, 64), {1, 0, 0});
    // Surface 3: a solid flame surface -- translucent, but PF_NOT_SOLID unset.
    map.addSurface(torch, PF_TRANSLUCENT).shapeSurface(acrossX(800, 832, 0, 64), {0, -1, 0});
    // Surface 4: a sheet of a FireTexture that is not a flame.
    map.addSurface(notFlame, SHEET).shapeSurface(acrossX(1200, 1232, 0, 64), {0, -1, 0});
    // Surface 5: a flame sheet lying flat, at z = 40 -- SS 6's degenerate sheet.
    map.addSurface(torch, SHEET);
    // Surface 6: not solid, but neither translucent nor masked -- not a sheet.
    map.addSurface(torch, PF_NOT_SOLID).shapeSurface(acrossX(1600, 1632, 0, 64), {0, -1, 0});
    // Surface 7: a masked sheet, which wears the masked variant.
    map.addSurface(torch, PF_NOT_SOLID | PF_MASKED).shapeSurface(acrossX(2000, 2032, 0, 64), {0, -1, 0});
    // A lamp beside the lone torch, within 1.5 x its height of the foot; the
    // crossed pair has none so near.
    map.addActorOfClass("ActorPkg", "Lamp", {vectorProperty("Location", 16.0F, 200.0F, 32.0F)});

    MemoryPackages packages = memoryPackagesFor(fixture);
    JobSystem jobs(2);
    const detail::CuratedLookup nothingCurated = [](std::uint64_t) -> const uta::umat::CuratedOverride* {
        return nullptr;
    };
    const detail::FlameLookup isFlame = [torchKey](std::uint64_t fingerprint) { return fingerprint == torchKey; };
    // A Package views its bytes, so they outlive it.
    const std::vector<std::byte> bytes = asByteVector(map.build());
    const auto opened = uta::upkg::Package::open(bytes);
    REQUIRE(opened.has_value());
    auto baked = detail::bake(*opened, MAP_NAME, packages.resolver(), jobs, nothingCurated,
                              uta::umat::TEXTURE_BUDGET_BYTES, nullptr, isFlame);
    if (!baked.has_value()) FAIL("the bake was refused: " << baked.error().message());
    const BakeResult& result = *baked;
    for (const auto& skipped : result.skipped) UNSCOPED_INFO("skipped " << skipped.material << ": " << skipped.reason);
    REQUIRE(result.skipped.empty());

    SECTION("a flame material carries its palette at eight heats, as linear RGB; another does not") {
        // SS 4.2. The torch's palette has four entries, so heat 0 is entry 0
        // and every heat from 36 up is past the end, which entry 3 stands for.
        const auto& look = recordNamed(result, "dm-fixture.torch").flame;
        REQUIRE(look.has_value());
        const auto linear = [](const std::array<std::uint8_t, 4>& colour) {
            return std::array<float, 3>{static_cast<float>(uta::ubake::linearOf(colour[0])),
                                        static_cast<float>(uta::ubake::linearOf(colour[1])),
                                        static_cast<float>(uta::ubake::linearOf(colour[2]))};
        };
        CHECK(look->ramp[0] == linear(torchPicture.palette[0]));
        for (std::size_t i = 1; i < look->ramp.size(); ++i) CHECK(look->ramp[i] == linear(torchPicture.palette[3]));
        CHECK_FALSE(recordNamed(result, "dm-fixture.shield").flame.has_value());
    }

    SECTION("the lone, crossed and masked sheets are three records; the rest stay in GEOM") {
        REQUIRE(result.bundle.flames.has_value());
        const auto& flames = *result.bundle.flames;
        // Without the merge the crossed pair is two records, and this is four.
        REQUIRE(flames.size() == 3);
        REQUIRE(result.bundle.materials.has_value());
        const auto& materials = *result.bundle.materials;
        CHECK(materials[flames[0].material].id == "dm-fixture.torch");
        CHECK(materials[flames[1].material].id == "dm-fixture.torch");
        CHECK(materials[flames[2].material].id == "dm-fixture.torch#masked");

        const auto& lone = flames[0];
        CHECK(lone.seed == 0);
        CHECK(lone.base == std::array<float, 3>{16, 200, 0});
        CHECK(lone.width == 32);
        CHECK(lone.height == 64);
        CHECK(lone.light == 0); // SS 4.5: the lamp, 32 from the foot

        const auto& crossed = flames[1];
        CHECK(crossed.seed == 1); // the lower surface's
        CHECK(crossed.base == std::array<float, 3>{400, 0, 0});
        CHECK(crossed.width == 32);
        CHECK(crossed.height == 64);
        CHECK(crossed.light == -1);

        const auto& masked = flames[2];
        CHECK(masked.seed == 7);
        CHECK(masked.base == std::array<float, 3>{2016, 0, 0});

        // The sheets are not drawn twice; the solid, non-flame and flat ones are.
        CHECK_FALSE(draws(result, [](const auto& p) { return p[1] == 200 && p[0] <= 32; }));
        CHECK_FALSE(draws(result, [](const auto& p) { return p[0] >= 384 && p[0] <= 416; }));
        CHECK(draws(result, [](const auto& p) { return p[0] == 800; }));
        CHECK(draws(result, [](const auto& p) { return p[0] == 1200; }));
        CHECK(draws(result, [](const auto& p) { return p[2] == 40; }));
        CHECK(draws(result, [](const auto& p) { return p[0] == 1600; }));
        CHECK_FALSE(draws(result, [](const auto& p) { return p[0] >= 2000; }));
    }

    SECTION("a flat flame sheet makes no record and is reported") {
        // SS 6.
        REQUIRE(result.skippedFlames.size() == 1);
        CHECK(result.skippedFlames[0].surface == 5);
        CHECK_FALSE(result.skippedFlames[0].reason.empty());
    }
}
