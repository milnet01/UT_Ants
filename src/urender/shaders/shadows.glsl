// Reading a light's shadow -- UTA-0014 SS 4.8. The shadow map stands in for
// UTA-0112 SS 4.9's `blocked`.
//
// THE INCLUDER DECLARES `shadowFaces` AND `shadowAtlas`; scene_bindings.glsl does.

#ifndef UTA_SHADOWS_GLSL
#define UTA_SHADOWS_GLSL

#include "types.glsl"

// 1 where `light` reaches the surface at `x`, 0 where something nearer the
// light blocks it, filtered between at an edge. A light the atlas could not
// hold (SS 6) is unshadowed.
//
// NO NORMAL OFFSET. A lit surface is kept from shadowing itself by the tile
// pass's slope-scaled depth bias (Pipelines.cpp) and SHADOW_RECEIVER_BIAS. An
// offset along the normal was tried twice: first removing it changed no pixel,
// then (UTA-0182) one large enough to help let light through floors that a
// low light really is blocked from.
//
// UTA-0182: the depth a surface is compared at is pulled this far toward the
// light, in NDC depth. A wall whose corners sit a hair off whole numbers, near
// a far-reaching light that grazes it, read its own stored depth as nearer
// than itself and went black in straight-edged wedges; the slope-scaled bias
// did not cover it. 1e-6 was the smallest value tried that cleared both
// measured cases, and this is three times that. With the near plane at
// reach / 512, it is under a world unit near a light and a few units at the
// far end of the widest reach.
const float SHADOW_RECEIVER_BIAS = 3e-6;

// Where `x` falls in `light`'s atlas: the face, its clip position, the point in
// the tile and the depth it is compared at. False where the light is
// unshadowed there.
bool shadowLookup(Light light, vec3 x, out ShadowFace shadow, out vec4 clip, out vec2 uv, out float depth) {
    if (light.shadowFaceCount == 0u) return false;

    // A point light's six faces look along +X, -X, +Y, -Y, +Z, -Z: the face is
    // the axis the light-to-surface direction is longest on. A strip leader's
    // faces look out from its segment's midpoint (UTA-0162 SS 4.5).
    uint face = 0u;
    if (light.shadowFaceCount == 6u) {
        vec3 d = x - (light.location + 0.5 * light.span);
        vec3 a = abs(d);
        if (a.x >= a.y && a.x >= a.z) face = d.x >= 0.0 ? 0u : 1u;
        else if (a.y >= a.z) face = d.y >= 0.0 ? 2u : 3u;
        else face = d.z >= 0.0 ? 4u : 5u;
    }
    shadow = shadowFaces[uint(light.shadowFace) + face];
    clip = shadow.viewProj * vec4(x, 1.0);
    if (clip.w <= 0.0) return false;
    vec3 ndc = clip.xyz / clip.w;
    // Outside a spotlight's frustum its spot factor is already near zero.
    if (abs(ndc.x) > 1.0 || abs(ndc.y) > 1.0 || ndc.z > 1.0) return false;

    uv = shadow.atlasRect.xy + (ndc.xy * 0.5 + 0.5) * shadow.atlasRect.zw;
    depth = ndc.z - SHADOW_RECEIVER_BIAS;
    return true;
}

// How the tile point (in atlas texels) and the depth move when the surface
// point moves by `d`: the face's projection differentiated at `clip`.
vec3 shadowStep(ShadowFace shadow, vec4 clip, vec3 d, vec2 atlasSize) {
    vec4 dc = shadow.viewProj * vec4(d, 0.0);
    vec3 dndc = (dc.xyz - (clip.xyz / clip.w) * dc.w) / clip.w;
    return vec3(0.5 * dndc.xy * shadow.atlasRect.zw * atlasSize, dndc.z);
}

// One filtered compare at `uv`, kept half a texel inside the tile so filtering
// never reads a neighbour's. UTA-0175: the atlas's size is the tier's, so it
// is read off the atlas. An explicit level: UTA-0015's fog pass reads shadows
// from a compute shader, which has no derivatives. The atlas has one level
// either way.
float shadowTap(vec4 rect, vec2 uv, float depth) {
    vec2 halfTexel = 0.5 / vec2(textureSize(shadowAtlas, 0));
    uv = clamp(uv, rect.xy + halfTexel, rect.xy + rect.zw - halfTexel);
    return textureLod(shadowAtlas, vec3(uv, depth), 0.0);
}

// One read. The fog pass's: haze has no edge to see.
float shadowOf(Light light, vec3 x) {
    ShadowFace shadow;
    vec4 clip;
    vec2 uv;
    float depth;
    if (!shadowLookup(light, x, shadow, clip, uv, depth)) return 1.0;
    return shadowTap(shadow.atlasRect, uv, depth);
}

// UTA-0307: nine reads, for a surface, whose edges are seen up close. Each is
// the sampler's own 2x2 compare, placed and weighted so the nine together weigh
// a 5x5 block of texels in a smooth hill: the optimised PCF of Ignacio
// Castano's "Shadow Mapping Summary" (The Witness), as in Matt Pettineo's
// shadows sample. An edge slanting across the texels then blends over several
// of them instead of following them in a stair one texel deep. On the
// slanting-edge test at the coarsest tile, the edge's worst distance from a
// straight line: one read 7.3 rows, an even 3x3 square of reads (the same
// cost) 2.4, these 1.8.
//
// EACH READ IS COMPARED AT THE SURFACE'S OWN DEPTH THERE, not at the centre's.
// A read moved sideways over a surface the light grazes compares against the
// surface itself one texel on, which is nearer the light, and the surface
// shadows itself in stripes. The surface's depth gradient across the tile
// carries the depth to each read: any two directions in its plane, through
// the face's projection, give it. A plane stays a plane under the projection,
// so this is exact for a flat surface. Where the plane is edge-on to the face,
// one read.
//
// The directions come from `normal`, the surface's own, not from screen
// derivatives of the point, which need uniform control flow and gave the same
// edge.
float softShadowOf(Light light, vec3 x, vec3 normal) {
    ShadowFace shadow;
    vec4 clip;
    vec2 uv;
    float depth;
    if (!shadowLookup(light, x, shadow, clip, uv, depth)) return 1.0;
    vec2 atlasSize = vec2(textureSize(shadowAtlas, 0));
    vec3 along = cross(normal, abs(normal.x) < 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0));
    vec3 sx = shadowStep(shadow, clip, along, atlasSize);
    vec3 sy = shadowStep(shadow, clip, cross(normal, along), atlasSize);
    // Edge-on: the two directions land on the tile nearly parallel.
    float det = sx.x * sy.y - sx.y * sy.x;
    if (abs(det) < 1e-3 * length(sx.xy) * length(sy.xy)) return shadowTap(shadow.atlasRect, uv, depth);
    // Depth per atlas texel along u and v: the solution of
    // dot(slope, sx.xy) = sx.z and dot(slope, sy.xy) = sy.z.
    vec2 slope = vec2(sy.y * sx.z - sx.y * sy.z, sx.x * sy.z - sy.x * sx.z) / det;

    // Where the reads sit and what each weighs, from where `uv` falls within
    // its texel: Castano's 5x5 weights, which sum to 12 along each axis.
    vec2 texel = uv * atlasSize;
    vec2 base = floor(texel + 0.5);
    vec2 s = texel + 0.5 - base;
    base -= 0.5;
    vec3 wu = vec3(4.0 - 3.0 * s.x, 7.0, 1.0 + 3.0 * s.x);
    vec3 wv = vec3(4.0 - 3.0 * s.y, 7.0, 1.0 + 3.0 * s.y);
    vec3 ou = vec3((3.0 - 2.0 * s.x) / wu.x - 2.0, (3.0 + s.x) / wu.y, s.x / wu.z + 2.0);
    vec3 ov = vec3((3.0 - 2.0 * s.y) / wv.x - 2.0, (3.0 + s.y) / wv.y, s.y / wv.z + 2.0);
    float lit = 0.0;
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i) {
            vec2 o = base + vec2(ou[i], ov[j]) - texel;
            // Capped at the far plane, which is what an empty texel holds: no
            // surface lies past it. Where a wall the light grazes ends in the
            // tile, its slope carried on into the empty texels beyond, came out
            // farther than far, and the wall shadowed itself.
            float d = min(depth + dot(slope, o), 1.0);
            lit += wu[i] * wv[j] * shadowTap(shadow.atlasRect, uv + o / atlasSize, d);
        }
    return lit / 144.0;
}

#endif
