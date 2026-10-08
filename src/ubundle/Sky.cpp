// Where the level's sky is seen from -- Sky.h.

#include "ubundle/Sky.h"

#include <algorithm>
#include <cctype>
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
    std::ranges::transform(out, out.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
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

} // namespace uta::ubundle
