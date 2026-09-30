// UTA-0129 INV-9: ut-bench's frame workload draws and reports --
// docs/specs/UTA-0129-benchmark-tool.md SS 4.4.
//
// tools/ut-bench/Cli.cpp and Frame.cpp are compiled into this binary. No case
// asserts a duration: a frame's time here says nothing about the machine.

#include "device/DeviceFixture.h"

#include "core/FileSystem.h"
#include "ubundle/Bundle.h"
#include "ut-bench/Cli.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace uta::test::render;
namespace stdfs = std::filesystem;

namespace {

std::size_t count(const std::string& text, std::string_view fragment) {
    std::size_t found = 0;
    for (std::size_t at = text.find(fragment); at != std::string::npos; at = text.find(fragment, at + 1)) ++found;
    return found;
}

/// A directory of this test's own, removed when it goes.
class Scratch {
public:
    Scratch() {
        static const unsigned long long salt = std::random_device{}();
        static std::atomic<int> counter{0};
        path_ = stdfs::temp_directory_path() / ("uta-benchframe-" + std::to_string(salt) + "-" + std::to_string(counter++));
        stdfs::create_directories(path_);
    }
    ~Scratch() {
        std::error_code ec;
        stdfs::remove_all(path_, ec);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    [[nodiscard]] const stdfs::path& path() const { return path_; }

private:
    stdfs::path path_;
};

} // namespace

TEST_CASE("UTA-0129 INV-9: ut-bench frame draws each view and each move and says what it measured", "[device]") {
    removeDisplay();
    const Scratch scratch;
    uta::ubundle::Geometry geometry;
    addSquare(geometry, 100, 0, 0, 30, "solid", PF_UNLIT);
    uta::ubundle::Bundle bundle = bundleOf(std::move(geometry));
    addSolidMaterial(bundle, "solid", Rgba{201, 121, 61, 255});
    const auto bytes = uta::ubundle::write(bundle);
    if (!bytes.has_value()) FAIL(bytes.error().message());
    const stdfs::path bundleFile = scratch.path() / "level.utab";
    requireOk(uta::fs::writeFileAtomically(bundleFile, *bytes));
    const stdfs::path cameras = scratch.path() / "cameras.txt";
    std::ofstream(cameras) << "0 0 0 0 0 0 90\n\n0 10 0 0 2000 0 90\n0 -10 5 0 -2000 0 100\n";

    const std::vector<std::string> args = {"frame",   "--cameras", cameras.string(), "--tier", "low", "--size",
                                           "64x48",   "--still",   "3",              "--steps", "4",  bundleFile.string()};
    const std::vector<std::string_view> views(args.begin(), args.end());
    std::ostringstream out, err;
    const int code = uta::bench::runCli(views, out, err);
    INFO(err.str());
    REQUIRE(code == 0);
    const std::string json = out.str();
    REQUIRE(json.size() > 2);
    CHECK(json.front() == '{');
    CHECK(json.substr(json.size() - 2) == "}\n");
    CHECK(count(json, "\n") == 1);
    CHECK(count(json, "\"workload\": \"frame\"") == 1);
    CHECK(count(json, "\"tier\": \"low\"") == 1);
    CHECK(count(json, "\"width\": 64, \"height\": 48") == 1);
    CHECK(count(json, "\"stillFrames\": 3, \"moveFrames\": 4") == 1);
    // The device is named, and so are the machine and the build.
    CHECK(count(json, "\"device\": \"") == 1);
    CHECK(count(json, "\"device\": \"\"") == 0);
    CHECK(count(json, "\"cpu\": ") == 1);
    CHECK(count(json, "\"compiler\": ") == 1);
    // Three cameras, the blank line skipped: three views and two moves.
    CHECK(count(json, "\"camera\": ") == 3);
    CHECK(count(json, "\"camera\": \"0 10 0 0 2000 0 90\"") == 1);
    CHECK(count(json, "\"from\": ") == 2);
    CHECK(count(json, "\"from\": 1, \"to\": 2") == 1);
    // Each view, each move, and the two totals carry the four figures.
    CHECK(count(json, "\"p99\": ") == 3 + 2 + 2);
    CHECK(count(json, "\"shadowTiles\": ") == 3 + 2 + 2);
    CHECK(count(err.str(), "over 3 frame(s)") == 3);
    CHECK(count(err.str(), "over 4 frame(s)") == 2);
    CHECK(count(err.str(), "over 9 frame(s)") == 1);
    CHECK(count(err.str(), "over 8 frame(s)") == 1);
}
