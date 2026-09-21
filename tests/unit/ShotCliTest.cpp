// UTA-0199 -- the ut-shot command line, and what it takes from a capture folder.
//
// tools/ut-shot/Cli.cpp is compiled into this binary, so every case drives the
// options as a function with no process started and no Vulkan device. The
// drawing half stays in ut-shot's main.cpp and is run by hand.
//
// The case that matters most is the round trip: apps/ut-ants/Capture.cpp is
// compiled in too, so --from-capture is read back from what the WRITER
// actually produces rather than from a hand-typed copy of it. A field renamed
// at one end and not the other reddens here instead of silently redrawing the
// wrong frame.
//
// NO TEST NAME CONTAINS A COMMA, AND NONE STARTS WITH A DASH -- BakeCliTest.cpp
// says why.

#include "BakeFixture.h"

#include "ut-ants/Capture.h"
#include "ut-shot/Cli.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <clocale>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using uta::test::bake::TempDir;
using uta::urender::Tier;

namespace {

struct Parsed {
    std::optional<uta::shot::Options> options;
    std::string err;
};

Parsed parse(const std::vector<std::string>& args) {
    const std::vector<std::string_view> views(args.begin(), args.end());
    std::ostringstream err;
    Parsed result;
    result.options = uta::shot::parseOptions(views, err);
    result.err = err.str();
    return result;
}

/// A capture folder written by the real writer, so the reader is graded
/// against what F12 actually produces.
fs::path writeCaptureFolder(const fs::path& under, const uta::client::CaptureInfo& info) {
    fs::create_directories(under);
    std::ofstream(under / "details.txt") << uta::client::captureDetails(info);
    std::ofstream(under / "camera.txt") << info.cameraLine << "\n";
    return under;
}

uta::client::CaptureInfo sampleInfo() {
    uta::client::CaptureInfo info;
    info.map = "DM-Deck16][";
    info.bundleHash = "abc123";
    info.formatVersion = 14;
    info.bakerVersion = "21";
    info.commit = "74cd63a";
    info.tier = "low";
    info.renderWidth = 640;
    info.renderHeight = 360;
    info.renderScale = 0.5;
    info.windowWidth = 1280;
    info.windowHeight = 720;
    info.lightSeconds = 12.25;
    info.cameraLine = "100 200 300 0 16384 0 90";
    return info;
}

} // namespace

TEST_CASE("UTA-0199: the four positional arguments still read as they always have", "[shot]") {
    const auto parsed = parse({"map.utab", "320", "240", "out"});
    REQUIRE(parsed.options.has_value());
    CHECK(parsed.options->bundle == "map.utab");
    CHECK(parsed.options->width == 320);
    CHECK(parsed.options->height == 240);
    CHECK(parsed.options->prefix == "out");
    // Unset, so main.cpp draws at high and full scale as this tool always has.
    CHECK_FALSE(parsed.options->tier.has_value());
    CHECK_FALSE(parsed.options->renderScale.has_value());
    CHECK_FALSE(parsed.options->lightSeconds.has_value());
    CHECK_FALSE(parsed.options->cameraFile.has_value());
    CHECK(parsed.options->probes);
    CHECK_FALSE(parsed.options->linearOutput);
}

TEST_CASE("UTA-0199: the two switches ut-shot already had still set their fields", "[shot]") {
    const auto parsed = parse({"--linear", "--no-probes", "map.utab", "320", "240", "out"});
    REQUIRE(parsed.options.has_value());
    CHECK(parsed.options->linearOutput);
    CHECK_FALSE(parsed.options->probes);
}

TEST_CASE("UTA-0199: tier render scale and light time are taken from the command line", "[shot]") {
    const auto parsed =
        parse({"--tier", "low", "--render-scale", "0.5", "--light-time", "12.25", "map.utab",
               "320", "240", "out"});
    REQUIRE(parsed.options.has_value());
    CHECK(parsed.options->tier == Tier::Low);
    CHECK(parsed.options->renderScale == 0.5);
    CHECK(parsed.options->lightSeconds == 12.25);
}

TEST_CASE("UTA-0199: a tier name that is not one is refused rather than ignored", "[shot]") {
    const auto parsed = parse({"--tier", "lowest", "map.utab", "320", "240", "out"});
    CHECK_FALSE(parsed.options.has_value());
    CHECK_THAT(parsed.err, Catch::Matchers::ContainsSubstring("lowest"));
}

TEST_CASE("UTA-0199: a render scale of zero is refused because clamping cannot rescue it", "[shot]") {
    CHECK_FALSE(parse({"--render-scale", "0", "map.utab", "320", "240", "out"}).options.has_value());
    CHECK_FALSE(parse({"--render-scale", "-1", "map.utab", "320", "240", "out"}).options.has_value());
    CHECK_FALSE(parse({"--render-scale", "half", "map.utab", "320", "240", "out"}).options.has_value());
}

TEST_CASE("UTA-0199: an option wanting a value is refused when it ends the line", "[shot]") {
    const auto parsed = parse({"--tier"});
    CHECK_FALSE(parsed.options.has_value());
    CHECK_THAT(parsed.err, Catch::Matchers::ContainsSubstring("needs a value"));
}

TEST_CASE("UTA-0199: an unknown option is refused rather than taken for a bundle", "[shot]") {
    const auto parsed = parse({"--scale", "0.5", "map.utab", "320", "240", "out"});
    CHECK_FALSE(parsed.options.has_value());
    CHECK_THAT(parsed.err, Catch::Matchers::ContainsSubstring("--scale"));
}

TEST_CASE("UTA-0199: a zero width is refused rather than drawn", "[shot]") {
    CHECK_FALSE(parse({"map.utab", "0", "240", "out"}).options.has_value());
    CHECK_FALSE(parse({"map.utab", "320", "0", "out"}).options.has_value());
    CHECK_FALSE(parse({"map.utab", "wide", "240", "out"}).options.has_value());
}

TEST_CASE("UTA-0199: too few or too many positional arguments are refused", "[shot]") {
    CHECK_FALSE(parse({"map.utab", "320", "240"}).options.has_value());
    CHECK_FALSE(parse({"map.utab", "320", "240", "out", "extra"}).options.has_value());
}

// The round trip: read back what the writer actually wrote.
TEST_CASE("UTA-0199: a capture folder's own fields are read back off the writer's file", "[shot]") {
    const TempDir temp;
    const auto folder = writeCaptureFolder(temp.path() / "shot", sampleInfo());

    const auto parsed = parse({"--from-capture", folder.string(), "map.utab", "out"});
    REQUIRE(parsed.options.has_value());
    CHECK(parsed.options->tier == Tier::Low);
    CHECK(parsed.options->renderScale == 0.5);
    CHECK(parsed.options->lightSeconds == 12.25);
    // The size is the capture's own, so neither is typed out again.
    CHECK(parsed.options->width == 640);
    CHECK(parsed.options->height == 360);
    CHECK(parsed.options->bundle == "map.utab");
    CHECK(parsed.options->prefix == "out");
    REQUIRE(parsed.options->cameraFile.has_value());
    CHECK(fs::path(*parsed.options->cameraFile) == folder / "camera.txt");
}

TEST_CASE("UTA-0199: an explicit option beats the capture so a view redraws at another tier", "[shot]") {
    const TempDir temp;
    const auto folder = writeCaptureFolder(temp.path() / "shot", sampleInfo());

    // Both orders, since a reader should not have to know which wins by position.
    for (const std::vector<std::string> args :
         {std::vector<std::string>{"--from-capture", folder.string(), "--tier", "ultra",
                                   "map.utab", "out"},
          std::vector<std::string>{"--tier", "ultra", "--from-capture", folder.string(),
                                   "map.utab", "out"}}) {
        const auto parsed = parse(args);
        REQUIRE(parsed.options.has_value());
        CHECK(parsed.options->tier == Tier::Ultra);
        // The fields not overridden still come from the capture.
        CHECK(parsed.options->renderScale == 0.5);
        CHECK(parsed.options->lightSeconds == 12.25);
        CHECK(parsed.options->width == 640);
    }
}

TEST_CASE("UTA-0199: a capture folder that is not there is refused with its name", "[shot]") {
    const TempDir temp;
    const auto missing = (temp.path() / "absent").string();
    const auto parsed = parse({"--from-capture", missing, "map.utab", "out"});
    CHECK_FALSE(parsed.options.has_value());
    CHECK_THAT(parsed.err, Catch::Matchers::ContainsSubstring(missing));
}

TEST_CASE("UTA-0199: a capture with no readable size is refused rather than guessed at", "[shot]") {
    const TempDir temp;
    const auto folder = temp.path() / "shot";
    fs::create_directories(folder);
    std::ofstream(folder / "details.txt") << "tier: low\nrender-scale: 0.500\n";

    const auto parsed = parse({"--from-capture", folder.string(), "map.utab", "out"});
    CHECK_FALSE(parsed.options.has_value());
    CHECK_THAT(parsed.err, Catch::Matchers::ContainsSubstring("render-size"));
}

TEST_CASE("UTA-0199: --from-capture takes two positional arguments and not four", "[shot]") {
    const TempDir temp;
    const auto folder = writeCaptureFolder(temp.path() / "shot", sampleInfo());
    CHECK_FALSE(
        parse({"--from-capture", folder.string(), "map.utab", "320", "240", "out"}).options.has_value());
}

TEST_CASE("UTA-0199: an unreadable field is left unset rather than read as zero", "[shot]") {
    // A zero light time is a real value; an unparsable one must not become it.
    const auto details = uta::shot::parseCaptureDetails(
        "tier: nonsense\nrender-scale: half\nlight-seconds: soon\nrender-size: widexhigh\n");
    CHECK_FALSE(details.tier.has_value());
    CHECK_FALSE(details.renderScale.has_value());
    CHECK_FALSE(details.lightSeconds.has_value());
    CHECK_FALSE(details.width.has_value());
}

TEST_CASE("UTA-0199: a key the reader does not know is passed over", "[shot]") {
    const auto details = uta::shot::parseCaptureDetails(
        "map: DM-Deck16][\ncommit: 74cd63a\nsomething-new: 5\nlight-seconds: 0.000\n");
    REQUIRE(details.lightSeconds.has_value());
    CHECK(*details.lightSeconds == 0.0);
}

// The writer uses std::format, which always writes a full stop; a reader using
// stod would read the LOCALE's separator instead and silently reject the
// field, leaving the frame to redraw at the default. from_chars does not.
TEST_CASE("UTA-0199: a decimal field reads the same under a comma locale", "[shot]") {
    // Restore whatever the process had, so no other test inherits this.
    const std::string had = std::setlocale(LC_NUMERIC, nullptr) ?: "C";
    struct Restore {
        const std::string& back;
        ~Restore() { std::setlocale(LC_NUMERIC, back.c_str()); }
    } restore{had};

    const char* const commaLocales[] = {"de_DE.UTF-8", "de_DE", "fr_FR.UTF-8", "German_Germany"};
    bool set = false;
    for (const char* name : commaLocales)
        if (std::setlocale(LC_NUMERIC, name)) {
            set = true;
            break;
        }
    if (!set) SKIP("no comma-decimal locale is installed, so this machine cannot show the fault");

    const auto details = uta::shot::parseCaptureDetails("render-scale: 0.500\nlight-seconds: 12.250\n");
    REQUIRE(details.renderScale.has_value());
    CHECK(*details.renderScale == 0.5);
    REQUIRE(details.lightSeconds.has_value());
    CHECK(*details.lightSeconds == 12.25);
}
