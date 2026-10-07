// Which lights the direct term draws -- Lights.h.

#include "urender/Lights.h"

#include <cmath>
#include <cstddef>
#include <optional>
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

/// A repeatable hash of an integer to [0, 1), for LT_Flicker's jitter and a
/// flame's flicker.
double noise(std::uint64_t n) noexcept {
    n ^= n >> 33;
    n *= 0xff51afd7ed558ccdULL;
    n ^= n >> 33;
    n *= 0xc4ceb9fe1a85ec53ULL;
    n ^= n >> 33;
    return static_cast<double>(n >> 11) / static_cast<double>(1ULL << 53);
}

/// Smooth value noise in [0, 1] at `t`, one random value per whole step,
/// eased between neighbours. A time past i64's range holds at step 0, as
/// LT_FLICKER's does (UTA-0217).
double smoothNoise(std::uint64_t seed, double t) noexcept {
    const double whole = std::floor(t);
    const double within = t - whole;
    const auto step = std::abs(whole) < 0x1p62 ? static_cast<std::uint64_t>(static_cast<std::int64_t>(whole))
                                                : std::uint64_t{0};
    const double ease = within * within * (3 - 2 * within);
    const double a = noise(step * 2654435761ULL + seed);
    const double b = noise((step + 1) * 2654435761ULL + seed);
    return a + (b - a) * (std::isfinite(ease) ? ease : 0);
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
        // Twenty changes a second, each light on its own sequence. A pinned
        // time may be negative or huge (UTA-0217); both cast through i64, and
        // past its range the sequence holds at step 0.
        const double ticks = std::floor(seconds * 20.0);
        const auto step = std::abs(ticks) < 0x1p63
                              ? static_cast<std::uint64_t>(static_cast<std::int64_t>(ticks))
                              : std::uint64_t{0};
        return static_cast<float>(0.5 + 0.5 * noise(step * 1315423911ULL + light.exportIndex));
    }
    default: return 1.0f;
    }
}

float flameFlickerOf(std::uint32_t seed, double seconds) noexcept {
    // Two octaves, about three and seven changes a second: a flame's light
    // breathes rather than jumps. Chosen, not fitted: SS 4.6's fits are the
    // flame's own, not its light's.
    const double slow = smoothNoise(std::uint64_t{seed} << 1, seconds * 3.0);
    const double fast = smoothNoise((std::uint64_t{seed} << 1) | 1, seconds * 7.0);
    return static_cast<float>(0.8 + 0.2 * (0.65 * slow + 0.35 * fast));
}

namespace {

/// Whether the direct term draws `light` -- directLights' rule.
bool drawsDirectly(const ubundle::Light& light) noexcept {
    // An absorbed strip light is lit by its row's leader (UTA-0162 SS 4.3).
    if (light.type == LT_BACKDROP_LIGHT || light.specialLit || light.strip == ubundle::STRIP_ABSORBED) return false;
    // UTA-0169: brightness 0 never emits -- flicker and TriggerLight only
    // scale the saved value -- but a fog volume still thickens the fog.
    return light.brightness != 0 || light.volumeRadius != 0;
}

} // namespace

std::vector<ubundle::Light> directLights(const ubundle::Bundle& bundle) {
    std::vector<ubundle::Light> out;
    if (!bundle.lights) return out;
    for (const ubundle::Light& light : *bundle.lights)
        if (drawsDirectly(light)) out.push_back(light);
    return out;
}

std::vector<std::uint32_t> drawnIndices(const ubundle::Bundle& bundle) {
    std::vector<std::uint32_t> out;
    if (!bundle.lights) return out;
    std::uint32_t next = 0;
    for (const ubundle::Light& light : *bundle.lights) out.push_back(drawsDirectly(light) ? next++ : gpu::NONE);
    return out;
}

std::vector<gpu::Light> drawnLights(const ubundle::Bundle& bundle, double seconds) {
    std::vector<gpu::Light> out;
    if (!bundle.lights) return out;
    // UTA-0263 SS 4.5: the seed of the first FLAM record naming each light --
    // two flames may name one, and it follows the lower-indexed record.
    std::vector<std::optional<std::uint32_t>> flameSeed(bundle.lights->size());
    if (bundle.flames)
        for (const ubundle::Flame& flame : *bundle.flames)
            if (flame.light >= 0 && static_cast<std::size_t>(flame.light) < flameSeed.size()
                && !flameSeed[static_cast<std::size_t>(flame.light)])
                flameSeed[static_cast<std::size_t>(flame.light)] = flame.seed;
    for (std::size_t index = 0; index < bundle.lights->size(); ++index) {
        const ubundle::Light& light = (*bundle.lights)[index];
        if (!drawsDirectly(light)) continue;
        gpu::Light record{};
        record.location = light.location;
        // UTA-0162 SS 4.3: a leader draws from one end of its segment, along the rest.
        if (light.strip == ubundle::STRIP_LEADER) {
            record.location = light.stripFrom;
            for (std::size_t axis = 0; axis < 3; ++axis) record.span[axis] = light.stripTo[axis] - light.stripFrom[axis];
        }
        // Only a steady light: a map's own effect on any other type stays.
        record.flicker = light.type == LT_STEADY && flameSeed[index] ? flameFlickerOf(*flameSeed[index], seconds)
                                                                      : flickerOf(light, seconds);
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
        record.volumeRadius = light.volumeRadius; // UTA-0015 SS 4.4
        record.volumeBrightness = light.volumeBrightness;
        record.volumeFog = light.volumeFog;
        record.levelBrightness = light.levelBrightness; // UTA-0156 SS 4.5
        out.push_back(record);
    }
    return out;
}

} // namespace uta::urender
