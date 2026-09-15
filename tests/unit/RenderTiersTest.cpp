// UTA-0051 INV-1, INV-2, INV-3 and INV-5 -- docs/specs/UTA-0051-quality-tiers.md.
// Every function graded here is device-free (SS 4.1).
//
// NO TEST NAME CONTAINS A COMMA. Catch2 treats one as a filter separator.

#include "urender/Tiers.h"

#include "umat/Material.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using uta::urender::Tier;
using uta::urender::TierSettings;
using uta::urender::defaultTier;
using uta::urender::nextRenderScale;
using uta::urender::settingsOf;

namespace {

constexpr Tier ALL[] = {Tier::Low, Tier::Medium, Tier::High, Tier::Ultra};
constexpr std::uint64_t GIB = 1024ull * 1024ull * 1024ull;

} // namespace

TEST_CASE("UTA-0051 INV-1: each tier's floor and target are the spec's", "[render]") {
    CHECK(settingsOf(Tier::Low).minimumRenderScale == 0.50);
    CHECK(settingsOf(Tier::Medium).minimumRenderScale == 0.60);
    CHECK(settingsOf(Tier::High).minimumRenderScale == 0.75);
    CHECK(settingsOf(Tier::Ultra).minimumRenderScale == 1.00);
    for (const Tier tier : ALL) CHECK(settingsOf(tier).frameTimeTargetMilliseconds == 1000.0 / 60.0);
    CHECK(settingsOf(Tier::Low).minimumRenderScale <= settingsOf(Tier::Medium).minimumRenderScale);
    CHECK(settingsOf(Tier::Medium).minimumRenderScale <= settingsOf(Tier::High).minimumRenderScale);
    CHECK(settingsOf(Tier::High).minimumRenderScale <= settingsOf(Tier::Ultra).minimumRenderScale);
}

TEST_CASE("UTA-0051 INV-2: every tier's texture figure is umat's budget", "[render]") {
    for (const Tier tier : ALL) CHECK(settingsOf(tier).textureBudgetBytes == uta::umat::TEXTURE_BUDGET_BYTES);
}

TEST_CASE("UTA-0051 INV-3: the default tier follows the device type and memory", "[render]") {
    const VkPhysicalDeviceType discrete = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    // Each pair differs only in the threshold under test.
    CHECK(defaultTier(discrete, GIB * 3 / 2 - 1) == Tier::Low);
    CHECK(defaultTier(discrete, GIB * 3 / 2) == Tier::Medium);
    CHECK(defaultTier(discrete, GIB * 7 / 2 - 1) == Tier::Medium);
    CHECK(defaultTier(discrete, GIB * 7 / 2) == Tier::High);
    CHECK(defaultTier(discrete, GIB * 15 / 2 - 1) == Tier::High);
    CHECK(defaultTier(discrete, GIB * 15 / 2) == Tier::Ultra);
    // Type decides before memory.
    CHECK(defaultTier(discrete, 16 * GIB) == Tier::Ultra);
    CHECK(defaultTier(VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU, 16 * GIB) == Tier::Low);
    CHECK(defaultTier(VK_PHYSICAL_DEVICE_TYPE_CPU, 16 * GIB) == Tier::Low);
}

TEST_CASE("UTA-0051 INV-5: the controller reaches its bounds and never moves against a frame", "[render]") {
    for (const Tier tier : ALL) {
        INFO("tier " << uta::urender::tierName(tier));
        const TierSettings settings = settingsOf(tier);
        const double slow = settings.frameTimeTargetMilliseconds * 2;
        const double fast = settings.frameTimeTargetMilliseconds / 4;

        double scale = 1;
        for (int call = 0; call < 120; ++call) {
            const double next = nextRenderScale(scale, slow, settings);
            CHECK(next <= scale);
            CHECK(next >= settings.minimumRenderScale);
            scale = next;
        }
        CHECK(scale == settings.minimumRenderScale);

        for (int call = 0; call < 120; ++call) {
            const double next = nextRenderScale(scale, fast, settings);
            CHECK(next >= scale);
            CHECK(next <= 1.0);
            scale = next;
        }
        CHECK(scale == 1.0);
    }
}

TEST_CASE("UTA-0051: a frame between half the target and the target leaves the scale", "[render]") {
    const TierSettings settings = settingsOf(Tier::Low);
    CHECK(nextRenderScale(0.8, settings.frameTimeTargetMilliseconds * 0.75, settings) == 0.8);
}

TEST_CASE("UTA-0053: emissive bloom is drawn at every tier", "[render]") {
    using uta::urender::Feature;
    CHECK(uta::urender::minimumTier(Feature::Bloom) == Tier::Low);
    CHECK(uta::urender::enabled(Feature::Bloom, Tier::Low));
    CHECK(uta::urender::enabled(Feature::Bloom, Tier::Ultra));
}

TEST_CASE("UTA-0040 INV-4: parallax occlusion starts at Medium with the spec's step counts", "[render]") {
    using uta::urender::Feature;
    using uta::urender::parallaxStepsOf;
    CHECK(uta::urender::minimumTier(Feature::ParallaxOcclusion) == Tier::Medium);
    CHECK_FALSE(uta::urender::enabled(Feature::ParallaxOcclusion, Tier::Low));
    CHECK(uta::urender::enabled(Feature::ParallaxOcclusion, Tier::Medium));

    CHECK(parallaxStepsOf(Tier::Low).minimum == 0);
    CHECK(parallaxStepsOf(Tier::Low).maximum == 0);
    CHECK(parallaxStepsOf(Tier::Medium).minimum == 8);
    CHECK(parallaxStepsOf(Tier::Medium).maximum == 16);
    CHECK(parallaxStepsOf(Tier::High).minimum == 8);
    CHECK(parallaxStepsOf(Tier::High).maximum == 32);
    CHECK(parallaxStepsOf(Tier::Ultra).minimum == 16);
    CHECK(parallaxStepsOf(Tier::Ultra).maximum == 48);
}

TEST_CASE("UTA-0051: tier names read case-insensitively and write lower case", "[render]") {
    CHECK(uta::urender::tierNamed("low") == Tier::Low);
    CHECK(uta::urender::tierNamed("Medium") == Tier::Medium);
    CHECK(uta::urender::tierNamed("HIGH") == Tier::High);
    CHECK(uta::urender::tierNamed("ultra") == Tier::Ultra);
    CHECK_FALSE(uta::urender::tierNamed("").has_value());
    CHECK_FALSE(uta::urender::tierNamed("lowest").has_value());
    for (const Tier tier : ALL) CHECK(uta::urender::tierNamed(uta::urender::tierName(tier)) == tier);
}
