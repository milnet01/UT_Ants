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

// UTA-0165: UT99's FGetHSV hue -- three sectors whose channels sum to 1, the
// last dividing by 84 as the engine does -- moved toward white by saturation.
vec3 lightColour(uint hue, uint saturation) {
    float h = float(hue);
    vec3 pure;
    if (hue < 86u) pure = vec3((85.0 - h) / 85.0, h / 85.0, 0.0);
    else if (hue < 171u) pure = vec3(0.0, (170.0 - h) / 85.0, (h - 85.0) / 85.0);
    else pure = vec3((h - 170.0) / 85.0, 0.0, (255.0 - h) / 84.0);
    float w = float(saturation) / 255.0;
    return pure + w * (vec3(1.0) - pure);
}

// UTA-0165: FGetHSV's brightness over its value at 255 -- close to sqrt(V/255),
// so a dim light is far brighter than a linear V/255 would make it.
float lightIntensity(uint brightness) {
    float b = float(brightness) * 1.4 / 255.0;
    float top = 1.4 * 0.7 / (0.01 + sqrt(1.4));
    return clamp(b * 0.7 / (0.01 + sqrt(b)), 0.0, 1.0) / top;
}

// AActor::WorldLightRadius.
float lightRadius(uint radius) {
    return 25.0 * float(radius + 1u);
}

// UT99's own falloff -- UTA-0187: 1 + 2v^3 - 3v^2, as ubake::falloff says.
float lightFalloff(float distance, float radius) {
    if (distance >= radius) return 0.0;
    if (distance <= 0.0) return 1.0;
    float v = distance / radius;
    return 1.0 + 2.0 * v * v * v - 3.0 * v * v;
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
// shadow test: colour, times its intensity, times the falloff, times the
// incidence, times the spot factor, all measured from litFrom. LE_Cylinder and
// LE_NonIncidence replace the falloff and drop the other factors (UTA-0156).
// An absorbed strip light is never uploaded, so it needs no case here.
vec3 lightAt(Light light, vec3 x, vec3 n) {
    vec3 toLight = litFrom(light, x) - x;
    float d = length(toLight);
    float radius = lightRadius(light.radius);
    // UTA-0156 SS 4.5: the level's brightness scales every effect alike.
    vec3 colour = lightColour(light.hue, light.saturation) * (lightIntensity(light.brightness) * light.levelBrightness);

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

// UTA-0015 SS 4.3: the light `light` puts on the air at `x` -- lightAt with the
// normal pointing at the light, so the incidence factor is 1 and no part of the
// model is written twice. On the light itself lightAt needs no direction.
vec3 lightThrough(Light light, vec3 x) {
    vec3 toLight = litFrom(light, x) - x;
    float d = length(toLight);
    return lightAt(light, x, d > 0.0 ? toLight / d : vec3(0.0, 0.0, 1.0));
}

// UTA-0156 SS 4.4: how strongly a zone's ambient bytes light a surface. First
// set to 2.5 on AS-Frigate with EXPOSURE held. UTA-0165 refitted it with
// EXPOSURE over three maps once brightness went through FGetHSV: pooled block
// RMS 42.2 at 1.5, 42.2 at 2, 42.3 at 2.5 and 42.7 at 3; 1.5 has the lower sum
// over the three maps.
// Refitted again once lights carried LevelInfo.Brightness (UTA-0156 SS 4.5),
// on baker revision 17 with fog zeroed: pooled block RMS 37.0 at 0.25, 36.7 at
// 0.375, 36.6 at 0.5, 36.7 at 0.625 and 36.9 at 0.75
// (ut-ants-uta0156/sweepamb17b.sh).
// UTA-0187 refitted it jointly with DISPLAY_LIGHT_POWER once light met texture
// on display values, on baker revision 21 with the PBR Neutral tone map: at
// DISPLAY_LIGHT_POWER 1.6 the pooled block RMS is 34.9 at 0.75, 34.8 at 1 and
// 35.9 at 1.3 (ut-ants-uta0187/sweep187b.py).
// UTA-0192 took the toe out of that tone map, so the condition above no longer
// holds and this was re-checked -- NOT re-swept: sweep187b.py's linear renders
// are still on disk and the display transform is applied offline, so every
// (c, A) pair it tried was re-scored for free. DISPLAY_LIGHT_POWER 1.6 still
// wins. This constant's own optimum MOVES, to 0.75: pooled block RMS 32.22 at
// 0.75 against 32.49 at 1 and 33.46 at 1.3, and per pixel 32.01 against 32.29
// (ut-ants-uta0192/crosscheck.py, crosscheck2.py). The user settled this value
// on UTA-0187 and moved it here on 2026-09-20, the condition it was fitted
// under having gone.
//
// That re-score validates itself: under the OLD tone map it reproduces
// UTA-0187's shipped answer exactly, r21-c1.6-a1 at pooled 34.85 and k 6.16.
// Only three A values have renders on disk, so 0.75 is the best of the three
// and not a fitted optimum; finding one needs the sweep re-running.
// MIRRORED in tests/device/RenderLightingTest.cpp and RenderOcclusionTest.cpp,
// which hold it as a literal. Change all three together.
const float AMBIENT_SCALE = 0.75;

// UTA-0187: the gain on light before scene.frag raises it to DISPLAY_LIGHT_POWER.
// Held at 1: under a power rule a gain only trades with EXPOSURE, since
// pow(g * light, p) is pow(g, p) times pow(light, p) and the joint fit absorbs
// the constant. Sweeping g 0.5, 2 and 4 confirmed it, none beating 1
// (ut-ants-uta0187/sweep187.py).
const float LIGHT_GAIN = 1.0;

// UTA-0187: UT99 multiplies its lightmap into the texture on display values,
// so a surface in dim light is far darker than light times reflectance gives.
// scene.frag models that as the light, in display units, raised to this power.
// Fitted with AMBIENT_SCALE and EXPOSURE over DM-Deck16][, AS-Frigate and
// DM-Fetid at once, on baker revision 21 with the tone map as it then was --
// PBR Neutral, toe included, which UTA-0192 later removed: pooled
// block RMS 39.0 at 0.5, 37.1 at 0.75, 36.0 at 1 (the linear rule it replaces,
// at AMBIENT_SCALE 0.5), 35.2 at 1.3, 34.8 at 1.6, 35.2 at 1.9 and 36.1 at 2.2.
// At 1.6 the maps score 30.5, 39.6 and 36.5. AS-Frigate's sky -- the defect
// this item was filed for -- falls from 2.36 times the original's brightness to
// 1.29 at pose 0, where the rest of that frame moves from 0.66 to 0.87. The sky
// stood at 3.6 times the rest of the frame's ratio and now stands at 1.5
// (ut-ants-uta0187/sweep187b.py, scored with its sky()).
// The reference frames carry OpenGLDrv's own brightness ramp, measured as
// raw^1.65 at Brightness 1.0; scored offline it is worse at every point under
// both displays, so post.frag applies no ramp and this power carries the rule
// alone (ut-ants-uta0187/ramp.py).
const float DISPLAY_LIGHT_POWER = 1.6;

// The light a zone's ambient puts on every lit surface in it: a light's colour
// and intensity with no falloff, incidence, spot, shadow or flicker.
vec3 zoneAmbient(Zone zone) {
    return lightColour(zone.hue, zone.saturation) * (lightIntensity(zone.brightness) * AMBIENT_SCALE);
}

#endif
