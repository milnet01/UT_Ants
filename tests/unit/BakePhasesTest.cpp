// UTA-0129 INV-3: a bake records the steps it ran --
// docs/specs/UTA-0129-benchmark-tool.md SS 4.2.
//
// Names and depths only. No case here reads a duration: a fixture bake's are
// too short to say anything.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "BakeFixture.h"

#include "core/Jobs.h"
#include "ubake/Bake.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using uta::test::bake::standardFixture;
using uta::test::bake::TempDir;
using uta::test::bake::writeInstall;

namespace {

using Row = std::pair<std::string, unsigned>;

std::vector<Row> rowsOf(const std::vector<uta::Phase>& phases) {
    std::vector<Row> rows;
    for (const uta::Phase& phase : phases) rows.emplace_back(phase.name, phase.depth);
    return rows;
}

} // namespace

TEST_CASE("UTA-0129 INV-3: a bake lists its steps in order and a cached one stops at the clashes", "[ubake][timing]") {
    const TempDir dir;
    const std::filesystem::path install = dir.path() / "install";
    uta::ubake::BakeRequest request;
    request.install = install;
    request.map = writeInstall(install, standardFixture());
    request.outDir = dir.path() / "out";
    uta::JobSystem jobs(2);

    const auto written = uta::ubake::bakeToDirectory(request, jobs);
    REQUIRE(written.has_value());
    REQUIRE(written->verdict == uta::ubake::Verdict::Written);
    const std::vector<Row> before = {{"open-install", 0}, {"read-map", 0}, {"name", 0}, {"closure", 0}, {"clashes", 0}};
    std::vector<Row> whole = before;
    for (const Row& row : std::vector<Row>{{"bake", 0},         {"level", 1},    {"rooms", 1},        {"nav", 1},
                                           {"wiring", 1},       {"actors", 1},   {"movers", 1},       {"materials", 1},
                                           {"geometry", 1},     {"strips", 1},   {"mover-shapes", 1}, {"collision", 1},
                                           {"light-probes", 1}, {"occlusion", 1}, {"budget", 1},      {"encode", 0},
                                           {"write-file", 0}})
        whole.push_back(row);
    CHECK(rowsOf(written->phases) == whole);
    for (const uta::Phase& phase : written->phases) {
        CAPTURE(phase.name);
        CHECK(phase.calls == 1);
        CHECK(phase.seconds >= 0);
    }
    // detail::bake's own list is the same steps, one shallower.
    REQUIRE(written->result.has_value());
    REQUIRE(written->result->phases.size() == 14);
    CHECK(written->result->phases.front().name == "level");
    CHECK(written->result->phases.front().depth == 0);

    const auto cached = uta::ubake::bakeToDirectory(request, jobs);
    REQUIRE(cached.has_value());
    REQUIRE(cached->verdict == uta::ubake::Verdict::Cached);
    CHECK(rowsOf(cached->phases) == before);
}
