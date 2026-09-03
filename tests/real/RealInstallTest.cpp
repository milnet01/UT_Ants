// The second test tier. Built only when UTA_REAL_ASSET_TESTS is ON, because
// S7 requires the default suite to pass with no Unreal Tournament present.
//
// What this tier is for: the synthetic fixtures prove we read what we wrote,
// which is a closed loop. Only real packages prove we read what Epic and the
// community actually shipped. Until upkg exists (UTA-0003) there is nothing
// to point at them, so this asserts the harness itself -- that the configured
// install is there and looks like one.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#ifndef UTA_UT_INSTALL_DIR
#error "UTA_UT_INSTALL_DIR must be defined for the real-asset tier"
#endif

namespace fs = std::filesystem;

TEST_CASE("the configured Unreal Tournament install is present", "[real-assets]") {
    const fs::path root{UTA_UT_INSTALL_DIR};
    REQUIRE(fs::exists(root));
    REQUIRE(fs::is_directory(root));
}

TEST_CASE("the install holds a Maps directory with packages in it", "[real-assets]") {
    const fs::path maps = fs::path{UTA_UT_INSTALL_DIR} / "Maps";
    REQUIRE(fs::exists(maps));

    int packages = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator(maps)) {
        if (entry.is_regular_file() && entry.path().extension() == ".unr") {
            ++packages;
        }
    }
    // No expected count: the library grows, and a census here would go stale
    // the first time a map is added.
    CHECK(packages > 0);
}
