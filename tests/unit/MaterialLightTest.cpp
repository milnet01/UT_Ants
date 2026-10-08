// The material light file -- docs/specs/UTA-0292-reference-path-tracer.md
// SS 4.2, INV-2. INV-3 and INV-4, which need a bake, are BakeCliTest's.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "ubake/MaterialLight.h"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

using namespace uta;
using ubake::MaterialLight;

namespace {

/// Equal bits, so a value one ulp off fails.
bool same(double a, double b) { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); }

} // namespace

TEST_CASE("UTA-0292 INV-2: the material light file reads back every bit", "[ubake][material-light]") {
    // Values whose shortest decimal form needs all seventeen digits, and an
    // id with a space in it.
    std::vector<MaterialLight> written(2);
    written[0].id = "GenTerra.Floor 3";
    written[0].albedo = {0.1 + 0.2, 1.0 / 3.0, 0.0490000000000001};
    written[0].own.emission = {2.0 / 7.0, 0.0, 1e-300};
    written[1].id = "GenFluid.Lava";
    written[1].albedo = {0.5, 0.5, 0.5};
    written[1].own.unlitGlows = true;

    std::stringstream file;
    ubake::writeMaterialLight(file, written);
    const auto read = ubake::readMaterialLight(file);
    REQUIRE(read.has_value());
    REQUIRE(read->size() == written.size());
    for (std::size_t i = 0; i < written.size(); ++i) {
        INFO("entry " << i);
        const MaterialLight& a = written[i];
        const MaterialLight& b = (*read)[i];
        CHECK(a.id == b.id);
        CHECK(same(a.albedo.r, b.albedo.r));
        CHECK(same(a.albedo.g, b.albedo.g));
        CHECK(same(a.albedo.b, b.albedo.b));
        CHECK(same(a.own.emission.r, b.own.emission.r));
        CHECK(same(a.own.emission.g, b.own.emission.g));
        CHECK(same(a.own.emission.b, b.own.emission.b));
        CHECK(a.own.unlitGlows == b.own.unlitGlows);
    }
}

TEST_CASE("UTA-0292: a material light line that does not read is refused", "[ubake][material-light]") {
    for (const char* line : {"no tab here 0.5 0.5 0.5 0 0 0 0", "\t0.5 0.5 0.5 0 0 0 0",
                             "A\t0.5 0.5 0.5 0 0 0", "A\t0.5 0.5 0.5 0 0 0 2", "A\t0.5 x 0.5 0 0 0 0"}) {
        INFO(line);
        std::stringstream file(std::string(line) + "\n");
        const auto read = ubake::readMaterialLight(file);
        REQUIRE_FALSE(read.has_value());
        CHECK(read.error().code() == ErrorCode::MalformedData);
    }
}
