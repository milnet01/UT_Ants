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

constexpr std::uint32_t RECORDED_UNDER = 4; // UTA-0119 added MOVR
constexpr std::string_view GOLDEN =
    "329be7a6945e80f637c9e105f9d49162c77278c245ed6a6160a259441aaeb1d6";

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

    const auto written = uta::ubundle::write(result->bundle);
    REQUIRE(written.has_value());
    const std::string digest = uta::ubake::detail::hex(uta::sha256(*written));
    INFO("the golden bake's digest: " << digest);
    CHECK(uta::ubake::BAKER_REVISION == RECORDED_UNDER);
    CHECK(digest == GOLDEN);
}
