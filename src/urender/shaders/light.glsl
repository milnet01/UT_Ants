// UTA-0112 SS 4.3's light model -- the renderer's ONLY copy of it.
//
// UTA-0014 SS 3 decision 5: the model is written once, in GLSL, and never in
// C++. The shading pass includes this file, and so does the compute shader
// UTA-0014 INV-6 dispatches to hold it to ubake::lightAt on the CPU -- so what
// is graded is the shader that ships, not a transcription of it.
//
// It is the STEADY value. SS 4.9's flicker scalar is applied by the caller, to
// the direct term only.

#ifndef UTA_LIGHT_GLSL
#define UTA_LIGHT_GLSL

#include "types.glsl"

// ELightEffect's values -- the 432 headers' Engine/Inc/EngineClasses.h.
const uint LE_STATIC_SPOT = 8u;
const uint LE_SPOTLIGHT = 12u;
const uint LE_NON_INCIDENCE = 13u;
const uint LE_CYLINDER = 17u;

const float TWO_PI = 6.283185307179586;

// The hue wheel's pure colour moved toward white by saturation; kept in
// integers until the fraction, so the sector is exact.
vec3 lightColour(uint hue, uint saturation) {
    uint scaled = 6u * hue;
    uint sector = scaled / 256u;
    float f = float(scaled - 256u * sector) / 256.0;
    vec3 pure;
    if (sector == 0u) pure = vec3(1.0, f, 0.0);
    else if (sector == 1u) pure = vec3(1.0 - f, 1.0, 0.0);
    else if (sector == 2u) pure = vec3(0.0, 1.0, f);
    else if (sector == 3u) pure = vec3(0.0, 1.0 - f, 1.0);
    else if (sector == 4u) pure = vec3(f, 0.0, 1.0);
    else pure = vec3(1.0, 0.0, 1.0 - f);
    float w = float(saturation) / 255.0;
    return pure * (1.0 - w) + vec3(w);
}

// AActor::WorldLightRadius.
float lightRadius(uint radius) {
    return 25.0 * float(radius + 1u);
}

// UE1's falloff -- UTA-0156: full strength out to half the radius, then down.
float lightFalloff(float distance, float radius) {
    if (distance >= radius) return 0.0;
    if (distance <= 0.0) return 1.0;
    float v = distance / radius;
    return min(1.0, (1.0 + 2.0 * v * v * v - 3.0 * v * v) / v);
}

// UTA-0156: the share of an LE_Cylinder light's reach over which it fades to
// zero, so its sphere bound draws no hard edge -- the widest the block-RMS fit
// against the original game's frames did not worsen.
const float CYLINDER_FADE = 0.1;

// 1 inside the fade, 0 at `radius`, smooth between.
float edgeFade(float distance, float radius) {
    float t = clamp((radius - distance) / (CYLINDER_FADE * radius), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// UTA-0119 SS 4.5's Y * P * R applied to +X; roll does not move it.
vec3 lightDirection(int pitch, int yaw) {
    float p = float(pitch) * TWO_PI / 65536.0;
    float y = float(yaw) * TWO_PI / 65536.0;
    return vec3(cos(p) * cos(y), cos(p) * sin(y), sin(p));
}

// The point `light` is lit from at `x` -- UTA-0162 SS 4.3: its location, or for
// a strip leader the nearest point of the segment from `location` along `span`.
vec3 litFrom(Light light, vec3 x) {
    float ss = dot(light.span, light.span);
    if (ss == 0.0) return light.location;
    return light.location + light.span * clamp(dot(x - light.location, light.span) / ss, 0.0, 1.0);
}

// The light `light` puts on a surface at `x` with unit normal `n`, with no
// shadow test: colour, times brightness / 255, times the falloff, times the
// incidence, times the spot factor, all measured from litFrom. LE_Cylinder and
// LE_NonIncidence replace the falloff and drop the other factors (UTA-0156).
// An absorbed strip light is never uploaded, so it needs no case here.
vec3 lightAt(Light light, vec3 x, vec3 n) {
    vec3 toLight = litFrom(light, x) - x;
    float d = length(toLight);
    float radius = lightRadius(light.radius);
    vec3 colour = lightColour(light.hue, light.saturation) * (float(light.brightness) / 255.0);

    // UTA-0156: UE1's two effects that change the falloff's shape; neither has
    // an incidence term. A cylinder still reaches only inside its sphere, as
    // the map's own per-surface light lists show -- and as clustering assumes.
    if (light.effect == LE_CYLINDER) {
        if (d >= radius) return vec3(0.0);
        return colour * (edgeFade(d, radius) * max(0.0, 1.0 - dot(toLight.xy, toLight.xy) / (radius * radius)));
    }
    if (light.effect == LE_NON_INCIDENCE) return colour * max(0.0, 1.0 - d / radius);

    float f = lightFalloff(d, radius);
    if (f == 0.0) return vec3(0.0);

    // At the light itself there is no direction: both factors are 1.
    float incidence = 1.0;
    float spot = 1.0;
    if (d > 0.0) {
        vec3 l = toLight / d;
        if (light.effect != LE_NON_INCIDENCE) incidence = max(0.0, dot(n, l));
        if (light.effect == LE_SPOTLIGHT || light.effect == LE_STATIC_SPOT) {
            if (light.cone == 0u) return vec3(0.0);
            float c = 1.0 - float(light.cone) / 256.0;
            spot = clamp((dot(lightDirection(light.pitch, light.yaw), -l) - c) / (1.0 - c), 0.0, 1.0);
        }
    }
    return colour * (f * incidence * spot);
}

// UTA-0156 SS 4.4: how strongly a zone's ambient bytes light a surface, set by
// that spec's SS 7 measurement against the original game.
const float AMBIENT_SCALE = 1.0;

// The light a zone's ambient puts on every lit surface in it: a light's colour
// and intensity with no falloff, incidence, spot, shadow or flicker.
vec3 zoneAmbient(Zone zone) {
    return lightColour(zone.hue, zone.saturation) * (float(zone.brightness) / 255.0 * AMBIENT_SCALE);
}

#endif
