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

float shadowOf(Light light, vec3 x) {
    if (light.shadowFaceCount == 0u) return 1.0;

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
    ShadowFace shadow = shadowFaces[uint(light.shadowFace) + face];
    vec4 clip = shadow.viewProj * vec4(x, 1.0);
    if (clip.w <= 0.0) return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    // Outside a spotlight's frustum its spot factor is already near zero.
    if (abs(ndc.x) > 1.0 || abs(ndc.y) > 1.0 || ndc.z > 1.0) return 1.0;

    // Kept half a texel inside the tile, so filtering never reads a neighbour's.
    // UTA-0175: the atlas's size is the tier's, so it is read off the atlas.
    vec2 halfTexel = 0.5 / vec2(textureSize(shadowAtlas, 0));
    vec2 uv = shadow.atlasRect.xy + (ndc.xy * 0.5 + 0.5) * shadow.atlasRect.zw;
    uv = clamp(uv, shadow.atlasRect.xy + halfTexel, shadow.atlasRect.xy + shadow.atlasRect.zw - halfTexel);
    // An explicit level: UTA-0015's fog pass reads shadows from a compute
    // shader, which has no derivatives. The atlas has one level either way.
    return textureLod(shadowAtlas, vec3(uv, ndc.z - SHADOW_RECEIVER_BIAS), 0.0);
}

#endif
