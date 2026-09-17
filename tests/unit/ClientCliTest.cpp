// UTA-0016: the ut-ants command line. apps/ut-ants/Cli.cpp is compiled into
// this binary, so it is tested without a window.
//
// NO TEST NAME CONTAINS A COMMA -- BakeCliTest.cpp says why.

#include "ut-ants/Cli.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using uta::client::Options;

namespace {

struct Parsed {
    std::optional<Options> options;
    std::string err;
};

Parsed parse(const std::vector<std::string_view>& args) {
    std::ostringstream err;
    Parsed parsed;
    parsed.options = uta::client::parseArguments(args, err);
    parsed.err = err.str();
    return parsed;
}

} // namespace

TEST_CASE("UTA-0016: ut-ants takes an install and then a bundle", "[client]") {
    const Parsed parsed = parse({"/games/UT", "bakes/map.utab"});
    REQUIRE(parsed.options.has_value());
    CHECK(parsed.options->install == std::filesystem::path("/games/UT"));
    CHECK(parsed.options->bundle == std::filesystem::path("bakes/map.utab"));
    CHECK_FALSE(parsed.options->frames.has_value());
    CHECK_FALSE(parsed.options->validation);
    CHECK_FALSE(parsed.options->windowed);
    CHECK_FALSE(parsed.options->help);
}

TEST_CASE("UTA-0153: --windowed asks for a window instead of fullscreen", "[client]") {
    const Parsed parsed = parse({"/games/UT", "map.utab", "--windowed"});
    REQUIRE(parsed.options.has_value());
    CHECK(parsed.options->windowed);
    CHECK(parsed.options->bundle == std::filesystem::path("map.utab"));
}

TEST_CASE("UTA-0016: options may come before or after the two paths", "[client]") {
    const Parsed parsed = parse({"/games/UT", "--frames", "120", "map.utab", "--validation"});
    REQUIRE(parsed.options.has_value());
    CHECK(parsed.options->bundle == std::filesystem::path("map.utab"));
    CHECK(parsed.options->frames == 120u);
    CHECK(parsed.options->validation);
}

TEST_CASE("UTA-0016: --frames takes a whole number above zero", "[client]") {
    for (const std::string_view bad : {"0", "-1", "3x", "", "seven", "99999999999"}) {
        INFO("--frames " << bad);
        const Parsed parsed = parse({"--frames", bad, "/games/UT", "map.utab"});
        CHECK_FALSE(parsed.options.has_value());
        CHECK(parsed.err.find("--frames") != std::string::npos);
    }
    CHECK_FALSE(parse({"/games/UT", "map.utab", "--frames"}).options.has_value());
    CHECK_FALSE(parse({"--frames", "2", "--frames", "3", "/games/UT", "map.utab"}).options.has_value());
}

TEST_CASE("UTA-0170: an install alone opens the map launcher", "[client]") {
    const Parsed parsed = parse({"--windowed", "/games/UT", "--tier", "low"});
    REQUIRE(parsed.options.has_value());
    CHECK(parsed.options->install == std::filesystem::path("/games/UT"));
    CHECK(parsed.options->bundle.empty());
    CHECK(parsed.options->windowed);
    CHECK(parsed.options->tier == uta::urender::Tier::Low);
}

TEST_CASE("UTA-0170: the launcher refuses --frames", "[client]") {
    const Parsed parsed = parse({"--frames", "10", "/games/UT"});
    CHECK_FALSE(parsed.options.has_value());
    CHECK(parsed.err.find("--frames") != std::string::npos);
}

TEST_CASE("UTA-0016: ut-ants refuses no paths or more than two", "[client]") {
    CHECK_FALSE(parse({}).options.has_value());
    CHECK_FALSE(parse({"/games/UT", "a.utab", "b.utab"}).options.has_value());
    const Parsed unknown = parse({"--fly", "/games/UT", "map.utab"});
    CHECK_FALSE(unknown.options.has_value());
    CHECK(unknown.err.find("--fly") != std::string::npos);
}

TEST_CASE("UTA-0051: --tier takes a tier name and nothing else", "[client]") {
    const Parsed high = parse({"--tier", "high", "/games/UT", "map.utab"});
    REQUIRE(high.options.has_value());
    CHECK(high.options->tier == uta::urender::Tier::High);
    CHECK(parse({"/games/UT", "map.utab"}).options->tier == std::nullopt);
    CHECK(parse({"--tier", "ULTRA", "/games/UT", "map.utab"}).options->tier == uta::urender::Tier::Ultra);

    const Parsed unknown = parse({"--tier", "max", "/games/UT", "map.utab"});
    CHECK_FALSE(unknown.options.has_value());
    CHECK(unknown.err.find("--tier") != std::string::npos);
    CHECK_FALSE(parse({"/games/UT", "map.utab", "--tier"}).options.has_value());
    CHECK_FALSE(parse({"--tier", "low", "--tier", "low", "/games/UT", "map.utab"}).options.has_value());
}

TEST_CASE("UTA-0016: --help needs no paths", "[client]") {
    const Parsed parsed = parse({"--help"});
    REQUIRE(parsed.options.has_value());
    CHECK(parsed.options->help);
}
