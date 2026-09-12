// UTA-0014 INV-3 -- docs/specs/UTA-0014-vulkan-draw-path.md SS 4.4.
//
// Device selection rejects a device lacking any SS 4.4 requirement, names the
// first requirement it failed, and never selects a device that fails one.
// Graded on synthetic feature sets, so no device is needed.
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "urender/Device.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <functional>
#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using uta::urender::DeviceCandidate;
using uta::urender::firstMissingRequirement;
using uta::urender::selectDevice;

namespace {

DeviceCandidate qualifying(std::string name, VkPhysicalDeviceType type = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
    DeviceCandidate c;
    c.name = std::move(name);
    c.type = type;
    c.apiVersion = VK_API_VERSION_1_3;
    c.graphicsQueue = true;
    c.v13.dynamicRendering = VK_TRUE;
    c.v13.synchronization2 = VK_TRUE;
    c.v12.runtimeDescriptorArray = VK_TRUE;
    c.v12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    c.v12.descriptorBindingPartiallyBound = VK_TRUE;
    c.v12.descriptorBindingVariableDescriptorCount = VK_TRUE;
    c.core.textureCompressionBC = VK_TRUE;
    return c;
}

struct Missing {
    std::string requirement;
    std::function<void(DeviceCandidate&)> remove;
};

/// One row per SS 4.4 requirement, each taking exactly that one away. The
/// names are the spec table's, so a requirement checked under another name --
/// or not checked -- fails its row.
const std::vector<Missing>& missingOne() {
    static const std::vector<Missing> rows = {
        {"Vulkan 1.3", [](DeviceCandidate& c) { c.apiVersion = VK_API_VERSION_1_2; }},
        {"a graphics queue", [](DeviceCandidate& c) { c.graphicsQueue = false; }},
        {"present support", [](DeviceCandidate& c) { c.presentSupport = false; }},
        {"dynamicRendering", [](DeviceCandidate& c) { c.v13.dynamicRendering = VK_FALSE; }},
        {"synchronization2", [](DeviceCandidate& c) { c.v13.synchronization2 = VK_FALSE; }},
        {"runtimeDescriptorArray", [](DeviceCandidate& c) { c.v12.runtimeDescriptorArray = VK_FALSE; }},
        {"shaderSampledImageArrayNonUniformIndexing",
         [](DeviceCandidate& c) { c.v12.shaderSampledImageArrayNonUniformIndexing = VK_FALSE; }},
        {"descriptorBindingPartiallyBound",
         [](DeviceCandidate& c) { c.v12.descriptorBindingPartiallyBound = VK_FALSE; }},
        {"descriptorBindingVariableDescriptorCount",
         [](DeviceCandidate& c) { c.v12.descriptorBindingVariableDescriptorCount = VK_FALSE; }},
        {"textureCompressionBC", [](DeviceCandidate& c) { c.core.textureCompressionBC = VK_FALSE; }},
    };
    return rows;
}

} // namespace

TEST_CASE("INV-3: a device meeting every requirement is selected", "[render]") {
    const std::vector candidates = {qualifying("good")};
    CHECK(firstMissingRequirement(candidates[0]).empty());
    const auto chosen = selectDevice(candidates);
    REQUIRE(chosen.has_value());
    CHECK(*chosen == 0);
}

TEST_CASE("INV-3: a device lacking any one requirement is refused and the refusal names it", "[render]") {
    for (const Missing& row : missingOne()) {
        CAPTURE(row.requirement);
        DeviceCandidate candidate = qualifying("Example GPU");
        row.remove(candidate);

        CHECK_THAT(std::string(firstMissingRequirement(candidate)), ContainsSubstring(row.requirement));

        const std::vector candidates = {candidate};
        const auto chosen = selectDevice(candidates);
        REQUIRE_FALSE(chosen.has_value());
        CHECK(chosen.error().code() == uta::ErrorCode::NotFound);
        CHECK_THAT(std::string(chosen.error().message()), ContainsSubstring("Example GPU"));
        CHECK_THAT(std::string(chosen.error().message()), ContainsSubstring(row.requirement));
    }
}

TEST_CASE("INV-3: a failing device is never selected over a qualifying one", "[render]") {
    // The failing device is the PREFERRED type, so a selector that ranked
    // before it filtered would pick it.
    DeviceCandidate failing = qualifying("fast but no BC", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
    failing.core.textureCompressionBC = VK_FALSE;
    const std::vector candidates = {failing, qualifying("software", VK_PHYSICAL_DEVICE_TYPE_CPU)};
    const auto chosen = selectDevice(candidates);
    REQUIRE(chosen.has_value());
    CHECK(*chosen == 1);
}

TEST_CASE("INV-3: among qualifying devices a discrete GPU is preferred to a CPU driver", "[render]") {
    const std::vector candidates = {qualifying("software", VK_PHYSICAL_DEVICE_TYPE_CPU),
                                    qualifying("discrete", VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)};
    const auto chosen = selectDevice(candidates);
    REQUIRE(chosen.has_value());
    CHECK(*chosen == 1);
}

TEST_CASE("the surfaceless path never requires present support", "[render]") {
    // SS 4.4: present support is queried on the presenting path only, so a
    // candidate that was never asked is not refused for lacking it.
    DeviceCandidate candidate = qualifying("headless");
    candidate.presentSupport.reset();
    CHECK(firstMissingRequirement(candidate).empty());
}

TEST_CASE("an instance with no physical device is refused as having none", "[render]") {
    const auto chosen = selectDevice({});
    REQUIRE_FALSE(chosen.has_value());
    CHECK_THAT(std::string(chosen.error().message()), ContainsSubstring("no physical device"));
}
