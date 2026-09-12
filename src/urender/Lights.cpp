// Which lights the direct term draws -- Lights.h.

#include "urender/Lights.h"

#include <cmath>
#include <numbers>

namespace uta::urender {

namespace {

/// A light's cycle: LightPeriod in 32nds of a second, so the default of 32 is
/// one second and no byte is a zero-length cycle.
double cycleSeconds(std::uint8_t period) noexcept { return (period + 1) / 32.0; }

/// Where in its cycle a light is at `seconds`, in [0, 1), LightPhase shifting it.
double cyclePosition(const ubundle::Light& light, double seconds) noexcept {
    const double t = seconds / cycleSeconds(light.period) + light.phase / 256.0;
    return t - std::floor(t);
}

/// A repeatable hash of an integer to [0, 1), for LT_Flicker's jitter.
double noise(std::uint64_t n) noexcept {
    n ^= n >> 33;
    n *= 0xff51afd7ed558ccdULL;
    n ^= n >> 33;
    n *= 0xc4ceb9fe1a85ec53ULL;
    n ^= n >> 33;
    return static_cast<double>(n >> 11) / static_cast<double>(1ULL << 53);
}

} // namespace

float flickerOf(const ubundle::Light& light, double seconds) noexcept {
    const double position = cyclePosition(light, seconds);
    switch (light.type) {
    case LT_PULSE: return static_cast<float>(0.6 + 0.4 * std::sin(2 * std::numbers::pi * position));
    case LT_SUBTLE_PULSE: return static_cast<float>(0.9 + 0.1 * std::sin(2 * std::numbers::pi * position));
    case LT_BLINK: return position < 0.5 ? 1.0f : 0.0f;
    case LT_STROBE: return position < 0.1 ? 1.0f : 0.0f;
    case LT_FLICKER: {
        // Twenty changes a second, each light on its own sequence.
        const auto step = static_cast<std::uint64_t>(std::floor(seconds * 20.0));
        return static_cast<float>(0.5 + 0.5 * noise(step * 1315423911ULL + light.exportIndex));
    }
    default: return 1.0f;
    }
}

std::vector<gpu::Light> drawnLights(const ubundle::Bundle& bundle, double seconds) {
    std::vector<gpu::Light> out;
    if (!bundle.lights) return out;
    out.reserve(bundle.lights->size());
    for (const ubundle::Light& light : *bundle.lights) {
        if (light.type == LT_BACKDROP_LIGHT || light.specialLit) continue;
        gpu::Light record{};
        record.location = light.location;
        record.flicker = flickerOf(light, seconds);
        record.hue = light.hue;
        record.saturation = light.saturation;
        record.brightness = light.brightness;
        record.radius = light.radius;
        record.effect = light.effect;
        record.cone = light.cone;
        record.pitch = light.rotation[0];
        record.yaw = light.rotation[1];
        record.shadowFace = -1;
        record.shadowFaceCount = 0;
        out.push_back(record);
    }
    return out;
}

} // namespace uta::urender
