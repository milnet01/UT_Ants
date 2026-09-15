#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

// The forward pass's shading -- UTA-0014 SS 4.5 to SS 4.10, with UTA-0040's
// parallax occlusion.
//
// One shader for the opaque and the translucent pass. The translucent pass
// binds only the colour attachment, so its velocity output goes nowhere --
// which is SS 4.11's "PF_Translucent batches write none".

#include "scene_bindings.glsl"
#include "light.glsl"
#include "probes.glsl"
#include "shadows.glsl"

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec4 currentClip;
layout(location = 4) in vec4 previousClip;
layout(location = 5) flat in uint zone; // UTA-0156 SS 4.4

layout(location = 0) out vec4 outColour;
layout(location = 1) out vec2 outVelocity;
// UTA-0053: this surface's emission alone, which feeds the bloom chain. The
// translucent pass binds colour only, so its emission does not bloom.
layout(location = 2) out vec4 outEmission;

// UTA-0040 SS 4.4: the tier's step counts, set by Pipelines.cpp. A tier with
// no steps compiles the march out.
layout(constant_id = 0) const uint PARALLAX_MIN_STEPS = 0u;
layout(constant_id = 1) const uint PARALLAX_MAX_STEPS = 0u;
// UTA-0040 SS 4.5 step 4: parallax fades out over the mip level above this.
const float PARALLAX_FADE_MIP = 4.0;

// A byte umat wrote as 127.5 * c + 128 (src/umat/Derive.cpp), read back.
float normalComponent(float stored) {
    return (stored * 255.0 - 128.0) / 127.5;
}

// The world directions in which u and v grow, built from screen derivatives --
// GEOM carries no tangents. Scaled together, so their ratio is kept.
struct TextureAxes {
    vec3 u;
    vec3 v;
};

TextureAxes textureAxes(vec3 n, vec3 p, vec2 duv1, vec2 duv2) {
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec3 dp2perp = cross(dp2, n);
    vec3 dp1perp = cross(n, dp1);
    vec3 gradientU = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 gradientV = dp2perp * duv1.y + dp1perp * duv2.y;
    float scale = inversesqrt(max(max(dot(gradientU, gradientU), dot(gradientV, gradientV)), 1e-20));
    return TextureAxes(gradientU * scale, gradientV * scale);
}

// The surface normal `n` tilted by a normal map's `tangentNormal`.
//
// umat's X is the height's descent along +u, and its Y along -v: its Sobel
// measures down the image, which is +v, and keeps that sign. So the tangent is
// the direction u grows and the bitangent the direction v SHRINKS.
//
// The frame's sign needs no correction here: `n` is turned toward the viewer
// before this is called and the projection's +Y-down is fixed, so the screen
// orientation is the same for every visible fragment. Multiplying it out was
// tried, and a mutation removing it changed no pixel.
vec3 perturbed(vec3 n, TextureAxes axes, vec3 tangentNormal) {
    return normalize(axes.u * tangentNormal.x - axes.v * tangentNormal.y + n * tangentNormal.z);
}

// UTA-0040 SS 4.5: the coordinate the view meets the height field at. White is
// high and depth is pushed inward. The height map is sampled at one mip level
// fixed before the loop: implicit derivatives inside a loop with an early exit
// are undefined.
vec2 parallaxUv(Material material, TextureAxes axes, vec3 n) {
    vec3 toEye = normalize(frame.eye - worldPosition);
    float facing = dot(n, toEye);
    if (facing <= 0.0) return uv;

    float lod = textureQueryLod(textures[nonuniformEXT(material.height)], uv).x;
    float fade = 1.0 - clamp(lod - PARALLAX_FADE_MIP, 0.0, 1.0);
    if (fade <= 0.0) return uv;

    // The UV distance the view crosses per unit of depth, in each axis, and the
    // whole depth as a fraction of one repeat of the base picture.
    vec2 texels = vec2(textureSize(textures[nonuniformEXT(material.base)], 0));
    vec2 across = vec2(dot(toEye, axes.u), dot(toEye, axes.v)) / max(facing, 0.05);
    vec2 reach = across * (float(material.parallaxDepth) / texels) * fade;

    uint steps = max(1u, uint(mix(float(PARALLAX_MAX_STEPS), float(PARALLAX_MIN_STEPS), facing)));
    float layer = 1.0 / float(steps);
    vec2 stepUv = reach * layer;

    vec2 at = uv;
    float layerDepth = 0.0;
    float depthHere = 1.0 - textureLod(textures[nonuniformEXT(material.height)], at, lod).r;
    for (uint i = 0u; i < PARALLAX_MAX_STEPS; ++i) {
        if (i >= steps || layerDepth >= depthHere) break;
        at -= stepUv;
        layerDepth += layer;
        depthHere = 1.0 - textureLod(textures[nonuniformEXT(material.height)], at, lod).r;
    }

    // One linear interpolation between the last two samples, as Godot does.
    vec2 before = at + stepUv;
    float after = depthHere - layerDepth;
    float previous = (1.0 - textureLod(textures[nonuniformEXT(material.height)], before, lod).r) - (layerDepth - layer);
    float weight = after / min(after - previous, -1e-6);
    return mix(at, before, clamp(weight, 0.0, 1.0));
}

// Which cluster a fragment is in -- the same slicing Clusters.cpp builds the
// boxes with: 16 tiles across, 8 down, and 24 depth slices exponential in z.
uint clusterOf(vec2 pixel, float viewZ) {
    uvec3 grid = frame.clusterGrid;
    uint x = min(uint(pixel.x * float(grid.x) / frame.viewportSize.x), grid.x - 1u);
    uint y = min(uint(pixel.y * float(grid.y) / frame.viewportSize.y), grid.y - 1u);
    float slice = floor(log(max(viewZ, frame.nearPlane)) * frame.clusterDepthScale - frame.clusterDepthBias);
    uint z = uint(clamp(slice, 0.0, float(grid.z - 1u)));
    return x + y * grid.x + z * grid.x * grid.y;
}

void main() {
    Material material = materials[draw.materialIndex];

    // Derivatives first, in uniform control flow: every material map is sampled
    // with these, at whatever coordinate parallax gives (UTA-0040 SS 4.5 step 1).
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    // A two-sided surface seen from behind is lit, and displaced, on the side you see.
    vec3 surface = normalize(worldNormal);
    if (!gl_FrontFacing) surface = -surface;
    TextureAxes axes = textureAxes(surface, worldPosition, duv1, duv2);

    vec2 shadingUv = uv;
    if (PARALLAX_MAX_STEPS > 0u && material.parallaxDepth != 0u
        && (draw.polyFlags & (PF_MASKED | PF_FAKE_BACKDROP)) == 0u)
        shadingUv = parallaxUv(material, axes, surface);

    // An _SRGB block format: the sampler returns linear (SS 4.10).
    vec4 base = textureGrad(textures[nonuniformEXT(material.base)], shadingUv, duv1, duv2);

    // Bit by bit, never by equality: PF_Masked | PF_TwoSided is still masked.
    if ((draw.polyFlags & PF_MASKED) != 0u && base.a < MASK_THRESHOLD) discard;

    vec3 colour;
    if ((draw.polyFlags & (PF_UNLIT | PF_FAKE_BACKDROP)) != 0u) {
        // No light applied -- but the output stage still applies (SS 4.10).
        colour = base.rgb;
    } else {
        vec2 stored = textureGrad(textures[nonuniformEXT(material.normal)], shadingUv, duv1, duv2).rg;
        vec2 tilt = vec2(normalComponent(stored.x), normalComponent(stored.y));
        vec3 n = perturbed(surface, axes, vec3(tilt, sqrt(max(0.0, 1.0 - dot(tilt, tilt)))));

        // SS 4.6: only this fragment's own cluster's lights. SS 4.9: the
        // flicker scalar multiplies the direct term and never the indirect.
        uint cluster = clusterOf(gl_FragCoord.xy, (frame.view * vec4(worldPosition, 1.0)).z);
        uint count = clusterCounts[cluster] & ~CLUSTER_OVERFLOW;
        vec3 direct = vec3(0.0);
        for (uint k = 0u; k < count; ++k) {
            Light light = lights[clusterIndices[cluster * CLUSTER_CAPACITY + k]];
            // SS 4.8: the shadow map stands in for UTA-0112's `blocked`.
            direct += lightAt(light, worldPosition, n) * (light.flicker * shadowOf(light, worldPosition));
        }
        // SS 4.7: the probes, through the same normal the lights use.
        ProbeLattice lattice =
            ProbeLattice(frame.probeSpacing, frame.probeCount, frame.probeTableMask, frame.probeLongestRun);
        vec3 indirect = indirectAt(lattice, worldPosition, n);
        // UTA-0156 SS 4.4: the zone's ambient, on every lit surface in it.
        vec3 ambient = zoneAmbient(zones[zone]);
        // UTA-0112 SS 4.9: a surface of reflectance rho shows rho * (direct + indirect).
        colour = base.rgb * (direct + indirect + ambient);
    }
    // Emission is added to a lit surface only, as before UTA-0040, and at the
    // displaced coordinate like every other map.
    vec3 emitted = vec3(0.0);
    if ((draw.polyFlags & (PF_UNLIT | PF_FAKE_BACKDROP)) == 0u && material.emit != NONE)
        emitted = textureGrad(textures[nonuniformEXT(material.emit)], shadingUv, duv1, duv2).rgb;
    colour += emitted;
    outEmission = vec4(emitted, 1.0);

    outColour = vec4(colour, 1.0);
    // Current minus previous, in the target's UV units: +x right, +y down.
    outVelocity = (currentClip.xy / currentClip.w - previousClip.xy / previousClip.w) * 0.5;
}
