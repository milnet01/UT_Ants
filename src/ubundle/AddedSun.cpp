// The sun as a light, and folding it into what the renderer draws --
// docs/specs/UTA-0338-baked-sun.md SS 4.2.

#include "ubundle/Bundle.h"

namespace uta::ubundle {
namespace {

/// UT's LT_Steady.
constexpr std::uint8_t LT_STEADY = 1;

} // namespace

Light lightOfSun(const Sun& sun) noexcept {
    Light light;
    light.type = LT_STEADY;
    light.effect = SUN_EFFECT;
    // Its light travels down from the sun: pitched below the horizon by the
    // sun's height, and turned half a turn from where the sun stands.
    light.rotation = {-sun.pitch, sun.yaw + 32768, 0};
    light.brightness = sun.brightness;
    light.hue = sun.hue;
    light.saturation = sun.saturation;
    light.levelBrightness = sun.levelBrightness;
    return light;
}

void applySun(Bundle& bundle) {
    if (!bundle.sun) return;
    if (!bundle.lights) bundle.lights.emplace();
    bundle.lights->push_back(lightOfSun(*bundle.sun));
    bundle.sun.reset();
}

} // namespace uta::ubundle
