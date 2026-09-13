// UTA-0013's third quarantine check -- the ut-origin command line.
//
// tools/ut-origin/Cli.cpp is compiled into this binary, so every case drives
// the command line as a function over a file on disk, with no process started.
// docs/specs/UTA-0008-bundle-container-and-origin.md SS 4.5 fixes the failure
// direction: anything but a readable header carrying Authored is not authored.
//
// NO TEST NAME CONTAINS A COMMA, AND NONE STARTS WITH A DASH -- BakeCliTest.cpp
// says why.

#include "BakeFixture.h"

#include "ubundle/Bundle.h"
#include "ut-origin/Cli.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using uta::test::bake::TempDir;
using uta::ubundle::Origin;

namespace {

struct Run {
    int code = -1;
    std::string out;
    std::string err;
};

Run run(const std::vector<std::string>& args) {
    const std::vector<std::string_view> views(args.begin(), args.end());
    std::ostringstream out;
    std::ostringstream err;
    Run result;
    result.code = uta::origin::runCli(views, out, err);
    result.out = out.str();
    result.err = err.str();
    return result;
}

std::vector<std::byte> bundleBytes(Origin origin) {
    uta::ubundle::Bundle bundle;
    bundle.header.origin = origin;
    auto bytes = uta::ubundle::write(bundle);
    REQUIRE(bytes.has_value());
    return *bytes;
}

fs::path writeFile(const TempDir& dir, const std::vector<std::byte>& bytes) {
    const fs::path path = dir.path() / "bundle.utab";
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return path;
}

} // namespace

TEST_CASE("ut-origin accepts a bundle whose header marks it authored", "[origin]") {
    const TempDir dir;
    const Run result = run({writeFile(dir, bundleBytes(Origin::Authored)).string()});
    CHECK(result.code == 0);
    CHECK(result.out == "authored\n");
}

TEST_CASE("ut-origin refuses a bundle whose header marks it derived", "[origin]") {
    const TempDir dir;
    const Run result = run({writeFile(dir, bundleBytes(Origin::Derived)).string()});
    CHECK(result.code == 1);
    CHECK(result.out == "derived\n");
}

TEST_CASE("ut-origin refuses a header it cannot read and says why", "[origin]") {
    // Every case starts from an AUTHORED header, so the refusal can only come
    // from the one byte each section breaks.
    auto bytes = bundleBytes(Origin::Authored);
    SECTION("a file shorter than the header") { bytes.resize(uta::ubundle::HEADER_SIZE - 1); }
    SECTION("a wrong magic") { bytes[0] = std::byte{'X'}; }
    SECTION("an unsupported format version") {
        bytes[4] = std::byte{static_cast<unsigned char>(uta::ubundle::FORMAT_VERSION + 1)};
    }
    SECTION("an origin byte that is neither 0 nor 1") { bytes[8] = std::byte{2}; }

    const TempDir dir;
    const Run result = run({writeFile(dir, bytes).string()});
    CHECK(result.code == 1);
    CHECK(result.out == "unreadable\n");
    CHECK_FALSE(result.err.empty());
}

TEST_CASE("ut-origin refuses a file that does not exist", "[origin]") {
    const TempDir dir;
    const Run result = run({(dir.path() / "absent.utab").string()});
    CHECK(result.code == 1);
    CHECK(result.out == "unreadable\n");
    CHECK_FALSE(result.err.empty());
}

TEST_CASE("ut-origin takes exactly one path", "[origin]") {
    CHECK(run({}).code == 2);
    CHECK(run({"one.utab", "two.utab"}).code == 2);
}
