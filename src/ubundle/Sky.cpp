// Where the level's sky is seen from -- Sky.h.

#include "ubundle/Sky.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <variant>

namespace uta::ubundle {
namespace {

bool isSkyZone(const ActorClass& actorClass) {
    constexpr std::string_view SKY = "engine.skyzoneinfo";
    return actorClass.path == SKY || std::ranges::find(actorClass.ancestry, SKY) != actorClass.ancestry.end();
}

/// A property's name folded, since a map spells it as its author typed it.
std::string folded(std::string_view text) {
    std::string out(text);
    // ASCII by hand: std::tolower follows the program's locale (review-code
    // 2026-10-15).
    std::ranges::transform(out, out.begin(), [](char ch) { return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + 32) : ch; });
    return out;
}

} // namespace

std::optional<SkyView> skyViewOf(const Placements& placements) {
    std::optional<SkyView> any, highDetail;
    for (const ActorPlacement& actor : placements.actors) {
        if (actor.classIndex >= placements.classes.size() || !isSkyZone(placements.classes[actor.classIndex]))
            continue;
        SkyView view;
        bool high = false;
        // The class's effective defaults, then the actor's own list over them.
        const auto read = [&](const std::vector<PropertyRecord>& properties) {
            for (const PropertyRecord& property : properties) {
                if (property.arrayIndex != 0) continue;
                const std::string name = folded(property.name);
                if (const auto* location = std::get_if<std::array<float, 3>>(&property.value);
                    location && name == "location")
                    view.location = *location;
                if (const auto* flag = std::get_if<bool>(&property.value); flag && name == "bhighdetail") high = *flag;
            }
        };
        read(placements.classes[actor.classIndex].defaults);
        read(actor.properties);
        any = view;
        if (high) highDetail = view;
    }
    return highDetail ? highDetail : any;
}

std::optional<SkyView> skyViewOf(const Bundle& bundle) {
    if (!bundle.placements) return std::nullopt;
    return skyViewOf(*bundle.placements);
}

std::array<float, 3> paintedSkyLight(const PaintedSky& sky, const std::array<double, 3>& d) noexcept {
    const double height = std::sqrt(std::clamp(std::abs(d[2]), 0.0, 1.0));
    // smoothstep(0.02, 0.25, d.z): the clouds thin out toward the horizon.
    const double x = std::clamp((d[2] - 0.02) / (0.25 - 0.02), 0.0, 1.0);
    const double cover = sky.clouds * (x * x * (3 - 2 * x));
    // UTA-0389: below the horizon, the sea -- the sky mirrored by Schlick's
    // share, over the top mixed toward the horizon.
    const double c5 = std::pow(1.0 + std::min(d[2], 0.0), 5.0);
    const double mirrored = d[2] < 0 ? PAINTED_SEA_R0 + (1 - PAINTED_SEA_R0) * c5 : 1.0;
    std::array<float, 3> out{};
    for (std::size_t c = 0; c < 3; ++c) {
        const double above = sky.horizon[c] + (sky.top[c] - sky.horizon[c]) * height;
        const double sea = sky.top[c] + (sky.horizon[c] - sky.top[c]) * PAINTED_SEA_MIX;
        const double gradient = sea + (above - sea) * mirrored;
        out[c] = static_cast<float>(gradient + (PAINTED_SKY_CLOUD - gradient) * cover);
    }
    return out;
}

} // namespace uta::ubundle
