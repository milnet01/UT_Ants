// Quality tiers and the render-scale controller --
// docs/specs/UTA-0051-quality-tiers.md.
//
// INTERNAL (includes Vulkan). There is no device in it, so the unit tier grades
// every function here (SS 4.1).

#pragma once

#include "urender/Renderer.h"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace uta::urender {

/// What a tier sets -- SS 4.1 and SS 4.2.
struct TierSettings {
    double minimumRenderScale;
    double frameTimeTargetMilliseconds;
    /// Equal to umat::TEXTURE_BUDGET_BYTES, which urender may not link
    /// (docs/design.md rule 2). A unit test holds the two equal (INV-2).
    std::uint64_t textureBudgetBytes;
};

/// 60 frames a second -- SS 3 decision 5.
inline constexpr double FRAME_TIME_TARGET_MILLISECONDS = 1000.0 / 60.0;
inline constexpr std::uint64_t TIER_TEXTURE_BUDGET_BYTES = 1024ull * 1024ull * 1024ull;

[[nodiscard]] constexpr TierSettings settingsOf(Tier tier) noexcept {
    switch (tier) {
    case Tier::Low: return {0.50, FRAME_TIME_TARGET_MILLISECONDS, TIER_TEXTURE_BUDGET_BYTES};
    case Tier::Medium: return {0.60, FRAME_TIME_TARGET_MILLISECONDS, TIER_TEXTURE_BUDGET_BYTES};
    case Tier::High: return {0.75, FRAME_TIME_TARGET_MILLISECONDS, TIER_TEXTURE_BUDGET_BYTES};
    case Tier::Ultra: return {1.00, FRAME_TIME_TARGET_MILLISECONDS, TIER_TEXTURE_BUDGET_BYTES};
    }
    return {1.00, FRAME_TIME_TARGET_MILLISECONDS, TIER_TEXTURE_BUDGET_BYTES}; // unreachable
}

/// One enumerator per visual feature a tier switches on. A feature reads its
/// tier only through `enabled`.
enum class Feature : std::uint8_t {
    ParallaxOcclusion, ///< UTA-0040
    Bloom,             ///< UTA-0053's emissive bloom
};

/// The lowest tier that switches `feature` on: one case per enumerator, and no
/// default case, so adding a feature means adding its row here.
[[nodiscard]] constexpr Tier minimumTier(Feature feature) noexcept {
    switch (feature) {
    case Feature::ParallaxOcclusion: return Tier::Medium; // UTA-0040 SS 4.4
    // UTA-0053: a chain of half-size-and-smaller passes, cheap enough for every tier.
    case Feature::Bloom: return Tier::Low;
    }
    return Tier::Low; // unreachable
}

/// UTA-0040 SS 4.4: the march's step counts, the fewest at a view straight on
/// and the most at a grazing one.
struct ParallaxSteps {
    std::uint32_t minimum;
    std::uint32_t maximum;
};

/// Within what shipping engines use -- Godot 8 to 32, HDRP 5 to 15, Tatarchuk 8
/// to 50 -- with the split between tiers a judgement. Low draws none.
[[nodiscard]] constexpr ParallaxSteps parallaxStepsOf(Tier tier) noexcept {
    switch (tier) {
    case Tier::Low: return {0, 0};
    case Tier::Medium: return {8, 16};
    case Tier::High: return {8, 32};
    case Tier::Ultra: return {16, 48};
    }
    return {0, 0}; // unreachable
}

[[nodiscard]] constexpr bool enabled(Feature feature, Tier tier) noexcept {
    return static_cast<std::uint8_t>(tier) >= static_cast<std::uint8_t>(minimumTier(feature));
}

/// SS 4.3's table: an integrated or CPU device is Low, whatever memory it
/// reports; otherwise `deviceLocalBytes` decides.
[[nodiscard]] Tier defaultTier(VkPhysicalDeviceType type, std::uint64_t deviceLocalBytes) noexcept;

/// How far one call moves the scale.
inline constexpr double RENDER_SCALE_STEP = 0.05;

/// The render scale after a frame of `measuredMilliseconds` -- INV-5. A frame
/// slower than the target lowers the scale a step, one faster than half the
/// target raises it a step, and anything between leaves it. Always within
/// `[minimumRenderScale, 1]`. It keeps no state: the renderer passes a frame
/// time averaged over recent frames.
[[nodiscard]] double nextRenderScale(double current, double measuredMilliseconds,
                                     const TierSettings& settings) noexcept;

} // namespace uta::urender
