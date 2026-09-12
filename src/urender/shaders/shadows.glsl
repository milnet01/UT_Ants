// Reading a light's shadow -- UTA-0014 SS 4.8. The shadow map stands in for
// UTA-0112 SS 4.9's `blocked`.
//
// THE INCLUDER DECLARES `shadowFaces` AND `shadowAtlas`; scene_bindings.glsl does.

#ifndef UTA_SHADOWS_GLSL
#define UTA_SHADOWS_GLSL

#include "types.glsl"

// Shadows.h's SHADOW_ATLAS_SIZE.
const float SHADOW_ATLAS_TEXELS = 4096.0;

// 1 where `light` reaches the surface at `x`, 0 where something nearer the
// light blocks it, filtered between at an edge. A light the atlas could not
// hold (SS 6) is unshadowed.
//
// NO NORMAL OFFSET. A lit surface is kept from shadowing itself by the tile
// pass's slope-scaled depth bias (Pipelines.cpp). An offset along the normal
// was tried, and removing it changed no pixel -- even for a grazing light on
// the smallest tile, the case built to need it.
float shadowOf(Light light, vec3 x) {
    if (light.shadowFaceCount == 0u) return 1.0;

    // A point light's six faces look along +X, -X, +Y, -Y, +Z, -Z: the face is
    // the axis the light-to-surface direction is longest on.
    uint face = 0u;
    if (light.shadowFaceCount == 6u) {
        vec3 d = x - light.location;
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
    vec2 halfTexel = vec2(0.5 / SHADOW_ATLAS_TEXELS);
    vec2 uv = shadow.atlasRect.xy + (ndc.xy * 0.5 + 0.5) * shadow.atlasRect.zw;
    uv = clamp(uv, shadow.atlasRect.xy + halfTexel, shadow.atlasRect.xy + shadow.atlasRect.zw - halfTexel);
    return texture(shadowAtlas, vec3(uv, ndc.z));
}

#endif
