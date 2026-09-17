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
constexpr std::uint32_t RECORDED_UNDER = 17;
constexpr std::string_view GOLDEN =
    "4b7ad1383c34f57e19728216d120e49d4cad53f02ef0e4daebbffc5a860ae2d4";

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

    const auto written = uta::ubundle::write(result->bundle);
    REQUIRE(written.has_value());
    const std::string digest = uta::ubake::detail::hex(uta::sha256(*written));
    INFO("the golden bake's digest: " << digest);
    CHECK(uta::ubake::BAKER_REVISION == RECORDED_UNDER);
    CHECK(digest == GOLDEN);
}
