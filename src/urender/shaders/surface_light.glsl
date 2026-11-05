// The direct light at a fragment, from its shadow-mask chart or its cluster --
// UTA-0326 SS 4.5 and UTA-0014 SS 4.6. scene.frag lights the level with these,
// and UTA-0293's grass.frag lights each blade with them from the ground's chart,
// so both take light the same way. A shader including this must first enable
// GL_EXT_nonuniform_qualifier and GL_KHR_shader_subgroup_ballot.

#ifndef UTA_SURFACE_LIGHT_GLSL
#define UTA_SURFACE_LIGHT_GLSL

#include "scene_bindings.glsl"
#include "clusters.glsl"
#include "light.glsl"
#include "shadows.glsl"

// UTA-0298: the highlight each light below adds, gathered beside its diffuse
// light when the caller turns it on. scene.frag sets these before it calls
// chartedLight or clusterLight; grass.frag leaves specularOn false, so its
// blades take no highlight and pay nothing for it.
bool specularOn = false;
vec3 specularView = vec3(0.0, 0.0, 1.0); // unit, toward the eye
float specularRoughness = 1.0;           // perceptual, 0 mirror to 1 matte
vec3 specularF0 = vec3(0.04);            // reflectance face-on
vec3 specularSum = vec3(0.0);

const float SPECULAR_PI = 3.14159265;

// GGX's lobe with Smith's shadowing (k = alpha / 2) and Schlick's Fresnel,
// times pi so it sits beside this renderer's diffuse, which takes `lit` --
// colour, falloff and incidence -- as it is. `lit` already carries N.L.
vec3 specularOf(Light light, vec3 position, vec3 n, vec3 lit) {
    vec3 l = light.effect == SUN_EFFECT ? -lightDirection(light.pitch, light.yaw)
                                         : litFrom(light, position) - position;
    float d = length(l);
    if (d == 0.0) return vec3(0.0);
    l /= d;
    float nl = dot(n, l);
    float nv = dot(n, specularView);
    if (nl <= 0.0 || nv <= 0.0) return vec3(0.0);
    vec3 h = normalize(l + specularView);
    float alpha = max(specularRoughness * specularRoughness, 0.0064);
    float a2 = alpha * alpha;
    float nh = max(dot(n, h), 0.0);
    float denominator = nh * nh * (a2 - 1.0) + 1.0;
    float distribution = a2 / (SPECULAR_PI * denominator * denominator);
    float k = alpha * 0.5;
    float visibility = 1.0 / (4.0 * (nl * (1.0 - k) + k) * (nv * (1.0 - k) + k));
    vec3 fresnel = specularF0 + (1.0 - specularF0) * pow(1.0 - max(dot(specularView, h), 0.0), 5.0);
    return lit * (SPECULAR_PI * distribution * visibility) * fresnel;
}

// UTA-0326 SS 4.5: the direct light at a level fragment, from its chart's pairs
// in place of its cluster's lights, each seen through the baked mask -- or the
// shadow map, where a mover can come between. `n` is the shading normal.
// UTA-0293: `position`, the chart and its texel are the caller's, so grass
// lights itself from the ground's chart.
vec3 chartedLight(vec3 n, vec3 surface, vec3 position, uint maskChart, vec2 maskTexel) {
    vec3 direct = vec3(0.0);
    vec2 atlasSize = vec2(textureSize(textures[nonuniformEXT(frame.shadowMaskTexture)], 0));
    // UTA-0325: one chart at a time across the subgroup, so its pairs and their
    // lights load once for the lanes that share it rather than once a lane.
    // Each lane still lights only from its own chart. A waterfall loop: each
    // pass, the lanes whose chart the first active lane names light from it and
    // break out; the rest go round with the next chart. It ends because every
    // pass retires at least that first lane. Work for every lane goes after the
    // loop, never between the light gathering and the break.
    for (;;) {
        uint chartIndex = subgroupBroadcastFirst(maskChart);
        if (chartIndex != maskChart) continue;
        MaskChart chart = maskCharts[chartIndex];
        vec2 chartSize = vec2(chart.width, chart.height);
        for (uint k = 0u; k < chart.pairCount; ++k) {
            MaskPair pair = maskPairs[chart.firstPair + k];
            Light light = lights[pair.light];
            vec3 lit = lightAt(light, position, n);
            if (all(equal(lit, vec3(0.0)))) continue;
            float seen = 1.0;
            if (pair.moverReach != 0u) {
                seen = softShadowOf(light, position, surface);
            } else if (pair.x != MASK_ALL_LIT) {
                // The border texel keeps a filtered read inside the rectangle.
                vec2 at = vec2(pair.x, pair.y) + clamp(maskTexel, vec2(0.5), chartSize - 0.5);
                seen = textureLod(textures[nonuniformEXT(frame.shadowMaskTexture)], at / atlasSize, 0.0).r;
            }
            direct += lit * (light.flicker * seen);
            if (specularOn) specularSum += specularOf(light, position, n, lit) * (light.flicker * seen);
        }
        break;
    }
    // No pair names the flashlight, which moves with the camera.
    if (frame.flashlight != NONE) {
        Light light = lights[frame.flashlight];
        vec3 lit = lightAt(light, position, n);
        float seen = light.flicker * softShadowOf(light, position, surface);
        direct += lit * seen;
        if (specularOn) specularSum += specularOf(light, position, n, lit) * seen;
    }
    return direct;
}

// SS 4.6: the direct light at a fragment no chart lights, from its own
// cluster's lights only. UTA-0345: the sky room (`skyFlat`) takes each light by
// distance alone, with no incidence or shadow.
vec3 clusterLight(vec3 n, vec3 surface, bool skyFlat, vec3 position) {
    vec3 direct = vec3(0.0);
    uint cluster = clusterOf(gl_FragCoord.xy, (frame.view * vec4(position, 1.0)).z);
    uint count = clusterCounts[cluster] & ~CLUSTER_OVERFLOW;
    for (uint k = 0u; k < count; ++k) {
        Light light = lights[clusterIndices[cluster * CLUSTER_CAPACITY + k]];
        // UTA-0323: a cluster lists every light whose sphere meets its box,
        // so many give this point nothing. Their nine shadow reads were a
        // fifth of DM-Bishop's frame; skipping them changes no pixel.
        vec3 lit = skyFlat ? lightThrough(light, position) : lightAt(light, position, n);
        if (all(equal(lit, vec3(0.0)))) continue;
        // SS 4.8: the shadow map stands in for UTA-0112's `blocked`.
        float seen = light.flicker * (skyFlat ? 1.0 : softShadowOf(light, position, surface));
        direct += lit * seen;
        if (specularOn && !skyFlat) specularSum += specularOf(light, position, n, lit) * seen;
    }
    return direct;
}

#endif
