// UTA-0011 INV-5: the golden bake keeps BAKER_REVISION honest.
//
// docs/specs/UTA-0011-map-baker.md SS 4.3. A synthetic map is baked, and the
// SHA-256 of the written bundle is compared with a value recorded beside the
// revision it was recorded under. A change to what the baker writes with no
// bump fails it; so does a bump with no re-recording. It runs on every CI leg,
// so a compiler that bakes different bytes fails it too.
//
// WHAT IT CANNOT SEE (SS 10): it reaches umat, umap and unav only as far as
// the fixture exercises them, so a change that only real content reaches
// passes.
//
// TO RE-RECORD: bump BAKER_REVISION in src/ubake/Name.h, run this case, copy
// the digest its failure prints into GOLDEN, and set RECORDED_UNDER to the new
// revision -- in the same commit as the change that moved the bytes.

#include "BakeFixture.h"

#include "core/Jobs.h"
#include "core/Sha256.h"
#include "support/UnrealPackageBuilder.h"
#include "ubake/Bake.h"
#include "ubake/Name.h"
#include "ubundle/Bundle.h"
#include "umat/Library.h"
#include "umat/Material.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>

using namespace uta::test::bake;

namespace {

// 8 and 9: UTA-0155 made textures whose palette lives in another package, then
// procedural textures from their SourceTexture. The fixture holds neither, so
// the digest is the one revision 7 recorded.
// 10: UTA-0156 gave lights UE1's falloff and effect shapes. The digest did not
// move, so the fixture does not reach that change -- the SS 10 case above.
// 11: UTA-0156 bounded cylinder light by its sphere again. Same digest, same reason.
// 12: UTA-0162 gave every LITE record its strip fields and bumped the format to
// 10, so the digest moved whether or not the fixture holds a row.
// 13: UTA-0156 faded cylinder light over its last tenth. Same digest as 12: the
// fixture holds no cylinder light.
// 14: UTA-0156 wrote ZONE and gave each vertex its zone, and bumped the format
// to 11, so the digest moved.
// 15: UTA-0015 gave each ZONE entry its fog flag and bumped the format to 12, so
// the digest moved.
// 16: UTA-0165 took UT99's FGetHSV colour and brightness. Same digest as 15: the
// fixture's light is white at brightness 255, where both curves give 1.
// 17: UTA-0165 gave every LITE record the level's brightness (UTA-0156 SS 4.5)
// and bumped the format to 13, so the digest moved.
// 18: UTA-0168 let probe rays through PF_NotSolid surfaces. The fixture holds
// none, so the digest is 17's.
// 19: UTA-0176 matched an import by class and made fire stills.
// 20: UTA-0164 wrote AOCC and bumped the format to 14, so the digest moved.
// 21: UTA-0187 took UT99's own falloff. Same digest as 20: as at 10, the
// fixture does not reach the falloff's shape.
// 22: UTA-0206 found a class UnrealI lacks in UnrealShare. Same digest as 21: the
// fixture imports nothing from UnrealI.
// 23: UTA-0212 kept probes within PROBE_REACH_MARGIN of the navigation points.
// Same digest as 22: the fixture places no navigation point, so no reach applies;
// BakeLightProbesTest grades the reach.
// 25: UTA-0226 gave a zero-area occlusion chart no inside. Same digest as 24:
// the fixture has no zero-area chart.
// 26: UTA-0246 stretched textures not a power of two a side. Same digest as
// 25: the fixture's pictures are all 4x4.
// 27: UTA-0247 read an Int tag declaring under four bytes as four. Same digest
// as 26: the fixture writes every tag's true size.
// 29, format 15: UTA-0263 gave each MATS record a flame byte. A new digest
// under the same revision: the bytes moved, the baker did not.
// 30: UTA-0263 baked flame looks and FLAM. The fixture holds no flame, but
// every bake now writes FLAM, empty where the level has none, so the digest moved.
// 31, format 16: UTA-0105 gave each MATS record a liquid byte and baked liquid
// looks. The fixture holds no liquid, so only the byte moved the digest.
// 32, format 17: UTA-0269 gave each GEOM and MOVR batch a pan rate. The
// fixture pans nothing, so only the rate's zero bytes moved the digest.
// 33: UTA-0161 sends a glowing or unlit surface's own light into the probes.
// The fixture has neither, so the digest is 32's.
// 34, format 18: UTA-0276 gave each ZONE entry its pan speeds and moved the
// pan constant to 35. The fixture pans nothing, so the zone bytes moved it.
// 35, format 19: UTA-0215 gave each ZONE entry its water flag and view tint.
// The fixture has no water, so the zone's zero bytes moved it.
// 36, format 20: UTA-0270 gave each Ice look its MoveIce and bakes its glass.
// The fixture has no Ice, so the format bump alone moved it.
// 37, format 20: UTA-0275 bakes each texture's DetailTexture as <id>:detail.
// The fixture names no detail, so the digest did not move.
// 38, format 20: UTA-0284 takes each occlusion chart's plane from its corners.
// The fixture's stored normals are exact, so the digest did not move.
// 39, format 21: UTA-0286 gives a non-flame FireTexture its fire look. The
// fixture has no FireTexture, so the format bump and the fire byte moved it.
// 39, format 22: UTA-0277 gives each MATS record its tile kind byte, every
// one Fixed until the judgement lands, so the format bump and the byte moved it.
// 40, format 22: UTA-0277 judges each material's tile kind and stores its
// picture hash.
// 41, format 22: UTA-0310 stops drawing a sheet an invisible zone portal
// covers. The fixture has no portal, so the digest did not move.
// 42, format 22: UTA-0314 masks every surface whose texture is bMasked. No
// fixture texture is, so the digest did not move.
// 42, format 23: UTA-0326 adds SMSK. Nothing bakes one yet, so the format
// bump alone moved it.
// 43, format 23: UTA-0326 bakes SMSK for the fixture's lamp.
// 44, format 23: UTA-0326 bakes a two-sided surface from both sides. The
// fixture has none, so the digest did not move.
// 45, format 23: UTA-0292 sends on four times a surface's albedo and takes sky
// light. The fixture's lamp sets no bStatic, so no light bakes into its probes,
// and it has no sky view: the digest did not move.
constexpr std::uint32_t RECORDED_UNDER = 45;
constexpr std::string_view GOLDEN =
    "7a61a9e422f32dba4162d3b922003e246d5c9cb99b31c7f8678712504e831aa3";

} // namespace

TEST_CASE("the golden bake hashes to the value recorded for BAKER_REVISION",
          "[ubake][golden]") {
    const Fixture fixture = standardFixture();
    MemoryPackages packages = memoryPackagesFor(fixture);
    const std::vector<std::uint8_t> bytes = fixture.map.build();
    const auto map = uta::upkg::Package::open(uta::test::asBytes(bytes));
    REQUIRE(map.has_value());

    uta::JobSystem jobs(2);
    const auto result = uta::ubake::detail::bake(*map, MAP_NAME, packages.resolver(), jobs,
                                                 &uta::umat::curated,
                                                 uta::umat::TEXTURE_BUDGET_BYTES);
    REQUIRE(result.has_value());
    // A golden bake that made no material would pin nothing umat does.
    REQUIRE(result->bundle.materials.has_value());
    REQUIRE(result->bundle.materials->size() == STANDARD_MATERIALS.size());
    // Nor would one that placed no actor, lit none, or shaped no mover pin
    // UTA-0110's sections or UTA-0119's.
    REQUIRE(result->bundle.placements.has_value());
    REQUIRE(result->bundle.placements->actors.size() == 2);
    REQUIRE(result->bundle.lights.has_value());
    REQUIRE(result->bundle.lights->size() == 1);
    REQUIRE(result->bundle.movers.has_value());
    REQUIRE(result->bundle.movers->size() == 1);
    // Nor one that baked no collision UTA-0111's: the level's tree has the
    // fixture Model's five nodes, its floor and a square for each of its four
    // surfaces, and the mover's tree its brush's one.
    REQUIRE(result->bundle.collision.has_value());
    CHECK(result->bundle.collision->level.nodes.size() == 5);
    REQUIRE(result->bundle.collision->movers.size() == 1);
    CHECK(result->bundle.collision->movers[0].tree.nodes.size() == 1);
    // And UTA-0112's LPRB. How many probes it places is the fixture's to
    // decide; the digest below pins their values.
    REQUIRE(result->bundle.lightProbes.has_value());
    INFO("the golden bake's probe count: " << result->bundle.lightProbes->probes.size());
    // And UTA-0326's SMSK, for the fixture's one lamp.
    REQUIRE(result->bundle.shadowMask.has_value());

    const auto written = uta::ubundle::write(result->bundle);
    REQUIRE(written.has_value());
    const std::string digest = uta::ubake::detail::hex(uta::sha256(*written));
    INFO("the golden bake's digest: " << digest);
    CHECK(uta::ubake::BAKER_REVISION == RECORDED_UNDER);
    CHECK(digest == GOLDEN);
}
