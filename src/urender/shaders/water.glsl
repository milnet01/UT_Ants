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

// SS 4.4's constants. NOT FITTED, 2026-10-02: SS 4.6's repeat measure finds no
// repeat to reduce. Both candidate pools, DM-ArcaneTemple's swater4a and
// AS-OceanFloor's pond1, are translucent over a floor that repeats at its own
// scale. Arcane's central pool (256 units a repeat), straight down at 512, 2048
// and 3840 units: the water's correlation one repeat apart is no higher than
// half a repeat apart, ripple on or off, before this item or after; with what
// varies slower than an eighth of a repeat taken out it is under 0.07 at every
// shift. Seen across the pool at UTA-0105's pose, likewise. So these are first
// values, and a fit waits on an opaque Wet pool.
// What the tiling itself changes, against the same build with it off
// (TILE_VARIATION_REPEATS 1e6, fade off), on Arcane: up-close mean luma 85.6
// against 85.5 and contrast 11.7 against 12.2; UTA-0105's motion 5.80 against
// 5.88. Variation scales 1 to 16 move none of these by more than the noise.
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

// UTA-0215: caustics -- the net of bright lines light makes on what lies
// under a rippling surface. Two layers of value noise, each drifting its own
// way and bent by a slow wave, are bright where they cross their middle value,
// which draws thin curved lines as focused light does. This item's call, not
// fitted: the original draws none.
const float CAUSTIC_CELL = 64.0;     // world units across one noise cell
const float CAUSTIC_SPEED = 0.25;    // cells a second each layer drifts
// The focused light's mean, as a share of the surface's reflectance; its
// lines reach about nine times it. Added, not multiplied: caustics are light
// arriving from the surface, so they show in a pool the map leaves dim.
const float CAUSTIC_LIGHT = 0.03;

float causticLines(vec2 q, float t, uint seed, vec2 drift) {
    vec2 w = q + drift * (t * CAUSTIC_SPEED);
    w += 0.35 * vec2(sin(w.y * 3.1 + t * 0.9), cos(w.x * 2.7 - t * 0.7));
    return pow(1.0 - abs(2.0 * flameNoise(w, seed) - 1.0), 8.0);
}

// The light the caustics add, per unit reflectance, at world point `p`,
// facing `normal`, at light time `t`. pow(1 - |x|, 8) averages 1/9 over an
// even x, so its mean is CAUSTIC_LIGHT. The pattern lies on the axis plane the
// surface faces most, so a slope never smears it; floors take it whole and
// walls half.
float causticLight(vec3 p, vec3 normal, float t) {
    vec3 a = abs(normal);
    vec2 plane = a.z >= a.x && a.z >= a.y ? p.xy : (a.x >= a.y ? p.yz : p.xz);
    vec2 q = plane / CAUSTIC_CELL;
    float lines = 0.5 * (causticLines(q, t, 31u, vec2(1.0, 0.4)) + causticLines(q * 1.37, t, 32u, vec2(-0.5, 0.9)));
    float facing = mix(0.5, 1.0, max(normal.z, 0.0));
    return CAUSTIC_LIGHT * facing * 9.0 * lines;
}

#endif
