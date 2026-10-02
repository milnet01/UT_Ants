// The water look -- docs/specs/UTA-0089-water-reflections.md SS 4.2 and SS 4.4.
// A Wet or Wave liquid reflects the sky or the probes by Fresnel's law, and a
// Wet liquid's picture is varied so its repeats do not line up. scene.frag
// decides where the reflection goes (SS 4.3) and samples the sky itself,
// since skyAt is its own.
//
// THE NOISE IS flame.glsl's value noise, as liquid.glsl's is. No noise
// texture is added.

#ifndef UTA_WATER_GLSL
#define UTA_WATER_GLSL

#include "flame.glsl"
#include "liquid.glsl"
#include "light.glsl" // LIGHT_GAIN, DISPLAY_LIGHT_POWER
#include "probes.glsl"
#include "types.glsl"

// SS 4.2: water's reflectance looking straight down, ((1 - 1.333) / (1 + 1.333))^2.
// Not fitted: it is water's own value.
const float WATER_R0 = 0.02;

// SS 4.4's constants, fitted by SS 4.6's measures -- see the sweep beside each.
// NOT YET FITTED: first values, before SS 4.6's sweep.
const float TILE_VARIATION_REPEATS = 4.0; // repeats of the picture across one noise cell
const float TILE_FADE_START = 3.0;        // the mip level where the fade toward the mean begins
const float TILE_FADE_END = 6.0;          // the mip level past which the picture is its mean
// Quilez's own value, not fitted: how many offsets the noise picks between.
const float TILE_OFFSETS = 8.0;

// True for the liquids SS 3 says reflect: Wet and Wave, never Ice.
bool waterReflects(uint liquid) {
    return liquid != NONE && liquids[liquid].kind != LIQUID_ICE;
}

// SS 4.2: Schlick's share of light reflected, at the angle between normal `n`
// and the direction to the eye `v`.
float waterShare(vec3 n, vec3 v) {
    float c = 1.0 - max(dot(n, v), 0.0);
    float c2 = c * c;
    return WATER_R0 + (1.0 - WATER_R0) * (c2 * c2 * c);
}

// SS 4.2: whether reflected direction `r` meets the sky -- the level has one,
// `r` points up, and the water's zone or the viewer's has a sky window.
bool waterSeesSky(vec3 r, uint zone) {
    return frame.skyTexture != NONE && r.z > 0.0 && (zones[zone].sky != 0u || zones[frame.cameraZone].sky != 0u);
}

// SS 4.2: what the probes reflect along `r` at `x`, scaled as scene.frag
// scales the indirect light a surface of albedo 1 would show. Black where
// there are none (SS 6).
vec3 waterProbeReflection(vec3 x, vec3 r) {
    ProbeLattice lattice =
        ProbeLattice(frame.probeSpacing, frame.probeCount, frame.probeTableMask, frame.probeLongestRun);
    return indirectAt(lattice, x, r) * pow(LIGHT_GAIN, DISPLAY_LIGHT_POWER);
}

// SS 4.4, Quilez's third technique: the picture `base` sampled at `at` plus two
// offsets the noise at `uv` picks, and blended by it, then faded toward its
// mean colour as it is drawn small. `duv1` and `duv2` are the undisplaced
// derivatives. Call in uniform control flow: textureQueryLod needs it.
vec4 waterPicture(uint base, vec2 uv, vec2 at, vec2 duv1, vec2 duv2, uint seed) {
    float k = flameNoise(uv / TILE_VARIATION_REPEATS, seed) * TILE_OFFSETS;
    float ia = floor(k);
    float f = k - ia;
    vec2 offsetA = sin(vec2(3.0, 7.0) * ia);
    vec2 offsetB = sin(vec2(3.0, 7.0) * (ia + 1.0));
    vec4 a = textureGrad(textures[nonuniformEXT(base)], at + offsetA, duv1, duv2);
    vec4 b = textureGrad(textures[nonuniformEXT(base)], at + offsetB, duv1, duv2);
    vec3 difference = a.rgb - b.rgb;
    vec4 picture = mix(a, b, smoothstep(0.2, 0.8, f - 0.1 * (difference.r + difference.g + difference.b)));

    float lod = textureQueryLod(textures[nonuniformEXT(base)], uv).x;
    float smallest = float(textureQueryLevels(textures[nonuniformEXT(base)]) - 1);
    vec3 mean = textureLod(textures[nonuniformEXT(base)], vec2(0.5), smallest).rgb;
    float fade = clamp((lod - TILE_FADE_START) / (TILE_FADE_END - TILE_FADE_START), 0.0, 1.0);
    // The alpha stays the blend's, so a masked liquid's holes move with the picture.
    return vec4(mix(picture.rgb, mean, fade), picture.a);
}

#endif
