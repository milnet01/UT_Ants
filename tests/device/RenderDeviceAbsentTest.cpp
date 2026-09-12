// UTA-0014 INV-5 -- docs/specs/UTA-0014-vulkan-draw-path.md SS 4.4.
//
// Where no Vulkan device qualifies, the renderer refuses and says why -- which
// is what makes every `device` test FAIL on such a machine rather than skip:
// each one gets its Renderer through requireRenderer.
//
// THIS IS THE ONE TEST THAT RULE DOES NOT GOVERN, AND IT CARRIES THE LABEL
// `device-absent` INSTEAD. It asserts the refusal, so it is run with no driver
// on purpose and must PASS there. It runs on every leg, needing no driver by
// construction.
//
// The driver search path is pointed at a file that does not exist, for this
// scope only, so the loader finds no driver. Measured on UTA-0014's probe:
// vkCreateInstance returns VK_ERROR_INCOMPATIBLE_DRIVER (-9).
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "device/DeviceFixture.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

using Catch::Matchers::ContainsSubstring;
using namespace uta::test::render;

TEST_CASE("INV-5: with no Vulkan driver the renderer refuses and names why", "[device-absent]") {
    removeDisplay();
    // VK_DRIVER_FILES is the current loader's variable, VK_ICD_FILENAMES the
    // older name it still honours. Both, so neither loader finds a driver.
    const EnvScope drivers("VK_DRIVER_FILES", "uta-no-such-vulkan-driver.json");
    const EnvScope icds("VK_ICD_FILENAMES", "uta-no-such-vulkan-driver.json");
    const EnvScope added("VK_ADD_DRIVER_FILES", nullptr);

    uta::urender::Config config;
    config.width = 8;
    config.height = 8;
    const auto renderer = uta::urender::Renderer::create(config);

    REQUIRE_FALSE(renderer.has_value());
    CHECK(renderer.error().code() == uta::ErrorCode::NotFound);
    CHECK_THAT(std::string(renderer.error().message()), ContainsSubstring("vkCreateInstance"));
}
