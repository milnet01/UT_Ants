// Quality tiers and the render-scale controller --
// docs/specs/UTA-0051-quality-tiers.md SS 4.3 and SS 4.4.

#include "urender/Tiers.h"

#include "core/Ascii.h"

#include <algorithm>
#include <array>
#include <utility>

namespace uta::urender {

namespace {

constexpr std::uint64_t GIB = 1024ull * 1024ull * 1024ull;

constexpr std::array<std::pair<Tier, std::string_view>, 4> NAMES = {{
    {Tier::Low, "low"}, {Tier::Medium, "medium"}, {Tier::High, "high"}, {Tier::Ultra, "ultra"}}};

} // namespace

std::optional<Tier> tierNamed(std::string_view name) noexcept {
    for (const auto& [tier, spelling] : NAMES)
        if (equalsIgnoringAsciiCase(name, spelling)) return tier;
    return std::nullopt;
}

std::string_view tierName(Tier tier) noexcept {
    for (const auto& [candidate, spelling] : NAMES)
        if (candidate == tier) return spelling;
    return "low"; // unreachable: every enumerator is named above
}

Tier defaultTier(VkPhysicalDeviceType type, std::uint64_t deviceLocalBytes) noexcept {
    // Integrated graphics reports system memory, so its figure says nothing.
    if (type == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU || type == VK_PHYSICAL_DEVICE_TYPE_CPU) return Tier::Low;
    // SS 4.3's table: half a GiB below the 2, 4 and 8 GB a card is sold with
    // (SS 3 decision 4), since a card reports a little less than that.
    if (deviceLocalBytes < GIB * 3 / 2) return Tier::Low;
    if (deviceLocalBytes < GIB * 7 / 2) return Tier::Medium;
    if (deviceLocalBytes < GIB * 15 / 2) return Tier::High;
    return Tier::Ultra;
}

double nextRenderScale(double current, double measuredMilliseconds, const TierSettings& settings) noexcept {
    const double floor = settings.minimumRenderScale;
    double next = std::clamp(current, floor, 1.0);
    if (measuredMilliseconds > settings.frameTimeTargetMilliseconds) {
        next -= RENDER_SCALE_STEP;
    } else if (measuredMilliseconds < settings.frameTimeTargetMilliseconds / 2) {
        next += RENDER_SCALE_STEP;
    }
    // Clamping last is what lands a run exactly on its bound.
    return std::clamp(next, floor, 1.0);
}

} // namespace uta::urender
