// UTA-0129's command line and its arithmetic -- INV-5 to INV-8 of
// docs/specs/UTA-0129-benchmark-tool.md.
//
// tools/ut-bench/Cli.cpp is compiled into this binary, so every case drives the
// command line as a function over a synthetic install on disk. No case asserts
// a duration.
//
// NO TEST NAME CONTAINS A COMMA, AND NONE STARTS WITH A DASH -- BakeCliTest.cpp
// says why.

#include "BakeFixture.h"

#include "ut-bench/Cli.h"
#include "ut-bench/Frame.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using uta::bench::detail::FrameSpread;
using uta::bench::detail::frameSpreadOf;
using uta::bench::detail::MapSummary;
using uta::bench::detail::Run;
using uta::bench::detail::Spread;
using uta::bench::detail::spreadOf;
using uta::bench::detail::summarise;
using uta::test::bake::standardFixture;
using uta::test::bake::TempDir;
using uta::test::bake::writeInstall;

namespace {

struct Ran {
    int code = -1;
    std::string out;
    std::string err;
};

Ran run(const std::vector<std::string>& args, const uta::bench::BuildInfo& build = {}) {
    const std::vector<std::string_view> views(args.begin(), args.end());
    std::ostringstream out;
    std::ostringstream err;
    Ran result;
    result.code = uta::bench::runCli(views, out, err, build);
    result.out = out.str();
    result.err = err.str();
    return result;
}

std::size_t count(const std::string& text, std::string_view fragment) {
    std::size_t found = 0;
    for (std::size_t at = text.find(fragment); at != std::string::npos; at = text.find(fragment, at + 1)) ++found;
    return found;
}

std::size_t filesIn(const fs::path& directory) {
    std::error_code ec;
    return static_cast<std::size_t>(std::distance(fs::directory_iterator(directory, ec), fs::directory_iterator()));
}

struct Install {
    TempDir dir;
    fs::path install;
    fs::path map;
    fs::path scratch;

    Install() {
        install = dir.path() / "install";
        map = writeInstall(install, standardFixture());
        scratch = dir.path() / "scratch";
        fs::create_directories(scratch);
    }
};

} // namespace

TEST_CASE("UTA-0129 INV-5: a spread is the smallest the median and the largest", "[bench]") {
    const Spread even = spreadOf({4, 1, 3, 2});
    CHECK(even.min == 1);
    CHECK(even.median == 2.5);
    CHECK(even.max == 4);
    const Spread odd = spreadOf({9, 1, 5});
    CHECK(odd.min == 1);
    CHECK(odd.median == 5);
    CHECK(odd.max == 9);
    const Spread one = spreadOf({7});
    CHECK(one.min == 7);
    CHECK(one.median == 7);
    CHECK(one.max == 7);
}

TEST_CASE("UTA-0129 INV-6: a map's summary holds its phases its share and whether its runs agree", "[bench]") {
    // Three runs. `bake` holds `rooms`; the first run alone has `fit-budget`.
    const std::vector<Run> runs = {
        {10.0, "aa", {{"name", 0, 2.0, 1}, {"bake", 0, 6.0, 1}, {"rooms", 1, 4.0, 3}, {"fit-budget", 0, 1.0, 1}}},
        {8.0, "aa", {{"name", 0, 1.0, 1}, {"bake", 0, 6.0, 1}, {"rooms", 1, 5.0, 3}}},
        {12.0, "aa", {{"name", 0, 3.0, 1}, {"bake", 0, 8.0, 1}, {"rooms", 1, 3.0, 3}}},
    };
    const MapSummary summary = summarise(runs);
    CHECK(summary.identical);
    CHECK(summary.total.min == 8.0);
    CHECK(summary.total.median == 10.0);
    CHECK(summary.total.max == 12.0);
    // Each run's total less its depth-0 phases: 10 - 9, 8 - 7, 12 - 11. A
    // depth-1 phase counted in would make them negative.
    CHECK(summary.unattributed.min == 1.0);
    CHECK(summary.unattributed.max == 1.0);

    REQUIRE(summary.phases.size() == 4);
    CHECK(summary.phases[0].name == "name");
    CHECK(summary.phases[0].seconds.min == 1.0);
    CHECK(summary.phases[0].seconds.median == 2.0);
    CHECK(summary.phases[0].share == 1.0 / 8.0); // its minimum over the total's minimum
    CHECK(summary.phases[1].name == "bake");
    CHECK(summary.phases[1].depth == 0);
    CHECK(summary.phases[2].name == "rooms");
    CHECK(summary.phases[2].depth == 1);
    CHECK(summary.phases[2].calls == 3);
    CHECK(summary.phases[2].seconds.min == 3.0);
    CHECK(summary.phases[2].seconds.max == 5.0);
    // A phase only some runs have is summarised over those runs.
    CHECK(summary.phases[3].name == "fit-budget");
    CHECK(summary.phases[3].seconds.min == 1.0);
    CHECK(summary.phases[3].seconds.max == 1.0);

    // The third run alone differs: comparing only the first two would miss it.
    std::vector<Run> differing = runs;
    differing[2].bundleSha256 = "bb";
    CHECK_FALSE(summarise(differing).identical);
}

TEST_CASE("UTA-0129 INV-7: ut-bench prints one object naming the machine and build and every map", "[bench][cli]") {
    const Install fixture;
    const std::string missing = (fixture.dir.path() / "no-such-map.unr").string();
    const Ran ran = run({"bake", "--install", fixture.install.string(), "--scratch", fixture.scratch.string(), "--runs", "2",
                         "--workers", "2", fixture.map.string(), missing, fixture.map.string()});
    // The middle map is refused, the other two are still baked, and the code says so.
    CHECK(ran.code == 1);
    REQUIRE(ran.out.size() > 2);
    CHECK(ran.out.front() == '{');
    CHECK(ran.out.substr(ran.out.size() - 2) == "}\n");
    CHECK(count(ran.out, "\n") == 1);
    CHECK(count(ran.out, "\"schema\": 1") == 1);
    CHECK(count(ran.out, "\"workload\": \"bake\"") == 1);
    CHECK(count(ran.out, "\"runs\": 2") == 1);
    CHECK(count(ran.out, "\"workers\": 2") == 1);
    CHECK(count(ran.out, "\"textureCache\": false") == 1);
    for (const std::string_view key : {"cpu", "logicalCores", "os", "load1", "compiler", "buildType", "sanitizer",
                                       "commit", "bakerVersion"}) {
        INFO("key: " << key);
        CHECK(count(ran.out, "\"" + std::string(key) + "\": ") == 1);
        CHECK(count(ran.out, "\"" + std::string(key) + "\": \"\"") == 0); // present and not empty
    }
    // What the caller could not say is "unknown", never left out.
    CHECK(count(ran.out, "\"compiler\": \"unknown\"") == 1);
    CHECK(count(ran.out, "\"commit\": \"unknown\"") == 1);
    CHECK(count(ran.out, "\"sanitizer\": \"none\"") == 1);

    CHECK(count(ran.out, "\"map\": ") == 3);
    CHECK(count(ran.out, "\"verdict\": \"written\"") == 2);
    CHECK(count(ran.out, "\"verdict\": \"refused\"") == 1);
    CHECK(count(ran.out, "\"error\": ") == 1);
    CHECK(count(ran.out, "\"identical\": true") == 2);
    CHECK(count(ran.out, "\"bundleSha256\": ") == 2);
    CHECK(count(ran.out, "\"unattributed\": ") == 2);
    CHECK(count(ran.out, "\"name\": \"light-probes\", \"depth\": 1, \"calls\": 1") == 2);
    // In the order given: written, refused, written.
    const std::size_t refused = ran.out.find("\"verdict\": \"refused\"");
    CHECK(ran.out.find("\"verdict\": \"written\"") < refused);
    CHECK(ran.out.rfind("\"verdict\": \"written\"") > refused);

    // What main.cpp knows, it says.
    const Ran named = run({"bake", "--install", fixture.install.string(), "--scratch", fixture.scratch.string(), "--runs",
                           "1", fixture.map.string()},
                          {"GNU 14.2.1", "Debug", "thread", "abc1234"});
    CHECK(named.code == 0);
    CHECK(count(named.out, "\"compiler\": \"GNU 14.2.1\"") == 1);
    CHECK(count(named.out, "\"commit\": \"abc1234\"") == 1);
    CHECK(count(named.err, "warning: built as Debug") == 1);
    CHECK(count(named.err, "warning: built with the thread sanitizer") == 1);
}

TEST_CASE("UTA-0129 INV-7: wrong arguments exit 2 and print nothing on standard output", "[bench][cli]") {
    const Install fixture;
    const std::string install = fixture.install.string();
    const std::string scratch = fixture.scratch.string();
    const std::string map = fixture.map.string();
    const std::vector<std::vector<std::string>> wrong = {
        {"bake", "--install", install, map},                                       // no --scratch
        {"bake", "--install", install, "--scratch", scratch, "--runs", "0", map},  // --runs 0
        {"bake", "--install", install, "--scratch", scratch},                      // no map
        {"--install", install, "--scratch", scratch, map},                         // no workload
        {"bake", "--install", install, "--scratch", scratch, "--runs", "two", map},
        {"bake", "--install", install, "--scratch", scratch, "--fast", map},
    };
    for (const std::vector<std::string>& args : wrong) {
        CAPTURE(args.size(), args.back());
        const Ran ran = run(args);
        CHECK(ran.code == 2);
        CHECK(ran.out.empty());
        CHECK_FALSE(ran.err.empty());
    }
    CHECK(filesIn(fixture.scratch) == 0);

    const Ran help = run({"--help"});
    CHECK(help.code == 0);
    CHECK(help.out.empty());
}

TEST_CASE("UTA-0129 INV-10: a frame's spread is its smallest median 99th percentile and largest", "[bench]") {
    std::vector<double> hundred;
    for (int i = 100; i >= 1; --i) hundred.push_back(i); // 100 down to 1
    const FrameSpread spread = frameSpreadOf(hundred);
    CHECK(spread.min == 1);
    CHECK(spread.median == 50.5);
    CHECK(spread.p99 == 99); // nearest rank: the 99th of 100
    CHECK(spread.max == 100);
    // Fewer than a hundred frames: the 99th percentile is the slowest.
    const FrameSpread few = frameSpreadOf({3, 9, 1});
    CHECK(few.median == 3);
    CHECK(few.p99 == 9);
    CHECK(frameSpreadOf({}).max == 0);
}

TEST_CASE("UTA-0129 INV-10: wrong frame arguments exit 2 and a missing bundle exits 1", "[bench][cli]") {
    // None of these reaches a Vulkan device: the arguments and the bundle are read first.
    const TempDir dir;
    const std::string cameras = (dir.path() / "cameras.txt").string();
    const std::string missing = (dir.path() / "no-such.utab").string();
    const std::vector<std::vector<std::string>> wrong = {
        {"frame", "--cameras", cameras},                                   // no bundle
        {"frame", missing},                                                // no --cameras
        {"frame", "--cameras", cameras, "--size", "640", missing},         // not <w>x<h>
        {"frame", "--cameras", cameras, "--size", "0x480", missing},
        {"frame", "--cameras", cameras, "--tier", "fastest", missing},
        {"frame", "--cameras", cameras, "--still", "0", missing},
        {"frame", "--cameras", cameras, missing, missing},                 // two bundles
    };
    for (const std::vector<std::string>& args : wrong) {
        CAPTURE(args.size(), args.back());
        const Ran ran = run(args);
        CHECK(ran.code == 2);
        CHECK(ran.out.empty());
        CHECK_FALSE(ran.err.empty());
    }
    const Ran absent = run({"frame", "--cameras", cameras, missing});
    CHECK(absent.code == 1);
    CHECK(absent.out.empty());
}

TEST_CASE("UTA-0129 INV-8: ut-bench leaves nothing in its scratch directory", "[bench][cli]") {
    const Install fixture;
    REQUIRE(filesIn(fixture.scratch) == 0);
    const std::string missing = (fixture.dir.path() / "no-such-map.unr").string();
    const Ran ran = run({"bake", "--install", fixture.install.string(), "--scratch", fixture.scratch.string(), "--runs", "3",
                         fixture.map.string(), missing});
    CHECK(ran.code == 1);
    CHECK(count(ran.out, "\"verdict\": \"written\"") == 1);
    CHECK(filesIn(fixture.scratch) == 0);
}
