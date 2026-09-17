// The level's sky -- UTA-0163. Sky.h says what is drawn and what is not.

#include "urender/Sky.h"

#include "urender/Placement.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numbers>
#include <string>
#include <string_view>
#include <variant>

namespace uta::urender {
namespace {

bool isSkyZone(const ubundle::ActorClass& actorClass) {
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

std::optional<SkyView> skyViewOf(const ubundle::Bundle& bundle) {
    if (!bundle.placements) return std::nullopt;
    const ubundle::Placements& placements = *bundle.placements;
    std::optional<SkyView> any, highDetail;
    for (const ubundle::ActorPlacement& actor : placements.actors) {
        if (actor.classIndex >= placements.classes.size() || !isSkyZone(placements.classes[actor.classIndex]))
            continue;
        SkyView view;
        bool high = false;
        // The class's effective defaults, then the actor's own list over them.
        const auto read = [&](const std::vector<ubundle::PropertyRecord>& properties) {
            for (const ubundle::PropertyRecord& property : properties) {
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

std::array<std::int32_t, 3> skyFaceRotation(std::uint32_t face) noexcept {
    // +X, -X, +Y, -Y, +Z, -Z, as shadows.glsl orders a point light's faces.
    static constexpr std::array<std::array<std::int32_t, 3>, 6> ROTATIONS{{
        {0, 0, 0}, {0, 32768, 0}, {0, 16384, 0}, {0, 49152, 0}, {16384, 0, 0}, {-16384, 0, 0}}};
    return ROTATIONS[face % 6];
}

Camera skyFaceCamera(const SkyView& sky, std::uint32_t face, std::uint32_t width, std::uint32_t height) noexcept {
    Camera camera;
    camera.location = sky.location;
    camera.rotation = skyFaceRotation(face);
    // The shorter side spans 90 degrees; the vertical field follows from it.
    const double side = std::min(width, height);
    camera.verticalFovDegrees =
        static_cast<float>(2.0 * std::atan(static_cast<double>(height) / side) * 180.0 / std::numbers::pi);
    return camera;
}

gpu::ShadowFace skyFaceSample(std::uint32_t face) noexcept {
    Camera camera;
    camera.rotation = skyFaceRotation(face);
    camera.verticalFovDegrees = 90.0f;
    const std::uint32_t columnIndex = face % SKY_COLUMNS, rowIndex = face / SKY_COLUMNS;
    const auto column = static_cast<float>(columnIndex), row = static_cast<float>(rowIndex);
    return {multiply(projectionOf(camera, 1, 1), viewOf(camera)),
            {column / SKY_COLUMNS, row / SKY_ROWS, 1.0f / SKY_COLUMNS, 1.0f / SKY_ROWS}};
}

} // namespace uta::urender
