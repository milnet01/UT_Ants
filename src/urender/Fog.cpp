// The fog's CPU half -- Fog.h.

#include "urender/Fog.h"

#include <algorithm>

namespace uta::urender {

VolumeLightChoice volumeLights(std::span<const gpu::Light> lights, std::span<const std::uint8_t> lightZones,
                               std::uint8_t cameraZone, std::span<const ubundle::Zone> zones,
                               const std::array<float, 3>& eye) {
    VolumeLightChoice out;
    const auto foggy = [zones](std::size_t zone) {
        if (zones.empty()) return false;
        return zones[zone < zones.size() ? zone : 0].fog != 0;
    };
    if (!foggy(cameraZone)) return out;

    struct Candidate {
        double distanceSquared;
        std::uint32_t index;
    };
    std::vector<Candidate> candidates;
    for (std::size_t i = 0; i < lights.size(); ++i) {
        if (lights[i].volumeRadius == 0 || !foggy(i < lightZones.size() ? lightZones[i] : 0)) continue;
        double d2 = 0;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const double d = static_cast<double>(lights[i].location[axis]) - eye[axis];
            d2 += d * d;
        }
        candidates.push_back({d2, static_cast<std::uint32_t>(i)});
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) { return a.distanceSquared < b.distanceSquared; });
    const std::size_t kept = std::min<std::size_t>(candidates.size(), VOLUME_LIGHT_CAPACITY);
    out.dropped = static_cast<std::uint32_t>(candidates.size() - kept);
    for (std::size_t i = 0; i < kept; ++i) out.indices.push_back(candidates[i].index);
    return out;
}

gpu::Light flashlightOf(const Camera& camera) noexcept {
    gpu::Light light{};
    light.location = camera.location;
    light.flicker = 1;
    light.hue = FLASHLIGHT_HUE;
    light.saturation = FLASHLIGHT_SATURATION;
    light.brightness = FLASHLIGHT_BRIGHTNESS;
    light.radius = FLASHLIGHT_RADIUS;
    light.effect = LE_SPOTLIGHT;
    light.cone = FLASHLIGHT_CONE;
    light.levelBrightness = 1; // UTA-0156 SS 4.5: ours, not the level's, so unscaled
    light.pitch = camera.rotation[0];
    light.yaw = camera.rotation[1];
    light.shadowFace = -1;
    light.shadowFaceCount = 0;
    return light;
}

} // namespace uta::urender
