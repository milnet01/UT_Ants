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
#include "variation.glsl" // the two-copy blend, TILE_VARIATION_REPEATS

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
// TILE_VARIATION_REPEATS and TILE_OFFSETS live in variation.glsl (UTA-0277).
const float TILE_FADE_START = 3.0;        // the mip level where the fade toward the mean begins
const float TILE_FADE_END = 6.0;          // the mip level past which the picture is its mean

// True for the liquids SS 3 says reflect: Wet and Wave, never Ice.
bool waterReflects(uint liquid) {
    return liquid != NONE && liquids[liquid].kind != LIQUID_ICE;
}

// UTA-0272: a glass pane's reflectance looking straight down,
// ((1 - 1.5) / (1 + 1.5))^2. Not fitted: it is window glass's own value. A
// pane reflects as water does, by the sky and probe helpers below.
const float GLASS_R0 = 0.04;

// SS 4.2: Schlick's share of light reflected, at the angle between normal `n`
// and the direction to the eye `v`, for reflectance `r0` looking straight down.
float fresnelShare(vec3 n, vec3 v, float r0) {
    float c = 1.0 - max(dot(n, v), 0.0);
    float c2 = c * c;
    return r0 + (1.0 - r0) * (c2 * c2 * c);
}

float waterShare(vec3 n, vec3 v) {
    return fresnelShare(n, v, WATER_R0);
}

// SS 4.2: whether reflected direction `r` meets the sky -- the level has one,
// `r` points up, and the water's zone or the viewer's has a sky window.
bool waterSeesSky(vec3 r, uint zone) {
    return (frame.skyTexture != NONE || frame.paintedSky != 0u) && r.z > 0.0 && (zones[zone].sky != 0u || zones[frame.cameraZone].sky != 0u);
}

// SS 4.2: what the probes reflect along `r` at `x`, scaled as scene.frag
// scales the indirect light a surface of albedo 1 would show. Black where
// there are none (SS 6).
vec3 waterProbeReflection(vec3 x, vec3 r) {
    return indirectAt(frameProbeLattice(), x, r) * pow(LIGHT_GAIN, DISPLAY_LIGHT_POWER);
}

// SS 4.4, Quilez's third technique (variation.glsl): the picture `base` sampled
// at `at` plus two offsets the noise at `uv` picks, and blended by it, then
// faded toward its mean colour as it is drawn small. `duv1` and `duv2` are the
// undisplaced derivatives. Call in uniform control flow: textureQueryLod needs it.
vec4 waterPicture(uint base, vec2 uv, vec2 at, vec2 duv1, vec2 duv2, uint seed) {
    TileBlend blend = tileBlendAt(uv, seed);
    vec4 a = textureGrad(textures[nonuniformEXT(base)], at + blend.offsetA, duv1, duv2);
    vec4 b = textureGrad(textures[nonuniformEXT(base)], at + blend.offsetB, duv1, duv2);
    vec4 picture = mix(a, b, tileWeight(blend, a.rgb, b.rgb));

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

#ifdef UTA_REFRACT
// UTA-0273 SS 4.4: see-through water. Only scene_refract.frag, the refracting
// pipelines' shader, defines UTA_REFRACT, so only it declares set 1.
layout(set = 1, binding = 0) uniform sampler2D sceneCopy;  // hdr before the water drew
layout(set = 1, binding = 1) uniform sampler2D sceneDepth; // 0 near, 1 far

// First values, chosen by eye, not fitted: the original draws none of this.
// UTA-0273 SS 13 records how they were chosen.
// How far a full unit of the ripple's tilt moves what is behind, in pixels of
// a 1080-line view, for water REFRACTION_REFERENCE_UNITS from the eye. Nearer
// water bends it further on screen, as the same bend spans more pixels.
const float REFRACTION_PIXELS = 64.0;
const float REFRACTION_REFERENCE_UNITS = 256.0;
// The share of light kept per unit of water crossed is exp(-ABSORPTION):
// red goes first, so deep water turns blue-green. About half the red is gone
// after 150 units, half the blue after 770.
const vec3 ABSORPTION = vec3(0.0046, 0.0018, 0.0009);
// The water's own picture fades in over this much depth from the shore.
const float SHORE_UNITS = 24.0;

// Depth `d`, 0 near and 1 far, as view-space distance along the view axis:
// Placement.cpp's projectionOf, inverted.
float viewDistanceOf(float d) {
    return frame.nearPlane * frame.farPlane / (frame.farPlane - d * (frame.farPlane - frame.nearPlane));
}

// SS 4.4: what the water at this fragment shows, given `surface` -- the colour
// the old blend added, its reflection included -- and `ripple`, how far the
// ripple tilts its normal, in world space. Where the water is shallow its
// own picture fades, so at the shore only what is behind it shows.
vec3 seeThrough(vec3 surface, vec3 ripple) {
    vec2 pixel = gl_FragCoord.xy;
    float waterAt = viewDistanceOf(gl_FragCoord.z);
    float thickness = max(viewDistanceOf(texelFetch(sceneDepth, ivec2(pixel), 0).r) - waterAt, 0.0);

    // The tilt as seen on screen: view x is right, view y up, screen y down.
    vec3 tilt = mat3(frame.view) * ripple;
    vec2 offset = vec2(tilt.x, -tilt.y) * REFRACTION_PIXELS * (REFRACTION_REFERENCE_UNITS / waterAt)
                  * (frame.viewportSize.y / 1080.0);
    // SS 6: clamped to the drawn region, so the edge repeats.
    vec2 bent = clamp(pixel + offset, vec2(0.5), frame.viewportSize - vec2(0.5));
    // SS 4.4: what lies nearer than the water is not behind it, so the
    // fragment reads straight behind itself instead. The copy is read at the
    // very pixel whose depth was tested: a filtered read there blended in its
    // neighbours, and next to a pillar one of them was the pillar (INV-5).
    ivec2 at = ivec2(bent);
    if (viewDistanceOf(texelFetch(sceneDepth, at, 0).r) < waterAt) at = ivec2(pixel);
    vec3 behind = texelFetch(sceneCopy, at, 0).rgb;

    vec3 tint = exp(-thickness * ABSORPTION);
    vec3 picture = surface * smoothstep(0.0, SHORE_UNITS, thickness);
    // The old blend, ONE and ONE_MINUS_SRC_COLOR, done here so the background
    // can be bent and tinted first.
    return picture + behind * tint * (vec3(1.0) - picture);
}
#endif

#endif
