// A still picture of a FireTexture -- UTA-0176. FireStill.h says what is
// adapted from SurrealEngine and what is altered.

#include "FireStill.h"

#include "Install.h"
#include "ufire/Fire.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace uta::ubake {

std::vector<std::byte> fireStill(std::uint32_t width, std::uint32_t height, std::span<const upkg::Spark> sparks,
                                 const FireSettings& settings) {
    if (width == 0 || height == 0) return {};
    std::vector<ufire::Spark> converted;
    converted.reserve(sparks.size());
    for (const upkg::Spark& spark : sparks)
        converted.push_back({spark.type, spark.heat, spark.x, spark.y, spark.byteA, spark.byteB, spark.byteC,
                             spark.byteD});
    ufire::Fire fire(width, height, std::move(converted),
                     {.renderHeat = settings.renderHeat, .rising = settings.rising,
                      .sparksLimit = settings.sparksLimit},
                     ufire::Turning::Scatter);
    for (int frame = 0; frame < FIRE_STILL_FRAMES; ++frame) fire.step();
    std::vector<std::byte> indices(fire.heat().size());
    std::ranges::transform(fire.heat(), indices.begin(), [](std::uint8_t heat) { return std::byte{heat}; });
    return indices;
}

FireSettings fireSettingsOf(const upkg::Package& holder, std::span<const upkg::Property> properties) {
    // The first property of the name and type counts, as the bake always read it.
    std::optional<std::uint8_t> renderHeat;
    std::optional<bool> rising;
    std::optional<std::int32_t> sparksLimit;
    std::optional<float> maxFrameRate;
    const auto take = [](auto& slot, const upkg::PropertyValue& value) {
        using T = typename std::remove_reference_t<decltype(slot)>::value_type;
        if (const auto* const held = std::get_if<T>(&value); held && !slot.has_value()) slot = *held;
    };
    for (const upkg::Property& property : properties) {
        const auto name = holder.name(property.nameIndex);
        if (!name.has_value()) continue;
        const std::string folded = detail::fold(*name);
        if (folded == "renderheat") take(renderHeat, property.value);
        else if (folded == "brising") take(rising, property.value);
        else if (folded == "sparkslimit") take(sparksLimit, property.value);
        else if (folded == "maxframerate") take(maxFrameRate, property.value);
    }
    return {.renderHeat = renderHeat.value_or(0),
            .rising = rising.value_or(false),
            .sparksLimit = sparksLimit.value_or(0),
            .maxFrameRate = maxFrameRate.value_or(0.0f)};
}

} // namespace uta::ubake
