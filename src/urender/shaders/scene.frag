#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

// The forward pass's shading -- UTA-0014 SS 4.5 to SS 4.10.
//
// One shader for the opaque and the translucent pass. The translucent pass
// binds only the colour attachment, so its velocity output goes nowhere --
// which is SS 4.11's "PF_Translucent batches write none".

#include "scene_bindings.glsl"
#include "light.glsl"
#include "probes.glsl"

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec4 currentClip;
layout(location = 4) in vec4 previousClip;

layout(location = 0) out vec4 outColour;
layout(location = 1) out vec2 outVelocity;

// A byte umat wrote as 127.5 * c + 128 (src/umat/Derive.cpp), read back.
float normalComponent(float stored) {
    return (stored * 255.0 - 128.0) / 127.5;
}

// The surface normal `n` tilted by a normal map's `tangentNormal`, in a tangent
// frame built from screen derivatives -- GEOM carries no tangents.
//
// umat's X is the height's descent along +u, and its Y along -v: its Sobel
// measures down the image, which is +v, and keeps that sign. So the tangent is
// the gradient of u and the bitangent the NEGATIVE gradient of v.
//
// The frame's sign needs no correction here: `n` is turned toward the viewer
// before this is called and the projection's +Y-down is fixed, so the screen
// orientation is the same for every visible fragment. Multiplying it out was
// tried, and a mutation removing it changed no pixel.
vec3 perturbed(vec3 n, vec3 p, vec2 uv, vec3 tangentNormal) {
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, n);
    vec3 dp1perp = cross(n, dp1);
    vec3 gradientU = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 gradientV = dp2perp * duv1.y + dp1perp * duv2.y;
    float scale = inversesqrt(max(max(dot(gradientU, gradientU), dot(gradientV, gradientV)), 1e-20));
    vec3 tangent = gradientU * scale;
    vec3 bitangent = -gradientV * scale;
    return normalize(tangent * tangentNormal.x + bitangent * tangentNormal.y + n * tangentNormal.z);
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
    // An _SRGB block format: the sampler returns linear (SS 4.10).
    vec4 base = texture(textures[nonuniformEXT(material.base)], uv);

    // Bit by bit, never by equality: PF_Masked | PF_TwoSided is still masked.
    if ((draw.polyFlags & PF_MASKED) != 0u && base.a < MASK_THRESHOLD) discard;

    vec3 colour;
    if ((draw.polyFlags & (PF_UNLIT | PF_FAKE_BACKDROP)) != 0u) {
        // No light applied -- but the output stage still applies (SS 4.10).
        colour = base.rgb;
    } else {
        // A two-sided surface seen from behind is lit on the side you see.
        vec3 n = normalize(worldNormal);
        if (!gl_FrontFacing) n = -n;
        vec2 stored = texture(textures[nonuniformEXT(material.normal)], uv).rg;
        vec2 tilt = vec2(normalComponent(stored.x), normalComponent(stored.y));
        n = perturbed(n, worldPosition, uv, vec3(tilt, sqrt(max(0.0, 1.0 - dot(tilt, tilt)))));

        // SS 4.6: only this fragment's own cluster's lights. SS 4.9: the
        // flicker scalar multiplies the direct term and never the indirect.
        uint cluster = clusterOf(gl_FragCoord.xy, (frame.view * vec4(worldPosition, 1.0)).z);
        uint count = clusterCounts[cluster] & ~CLUSTER_OVERFLOW;
        vec3 direct = vec3(0.0);
        for (uint k = 0u; k < count; ++k) {
            Light light = lights[clusterIndices[cluster * CLUSTER_CAPACITY + k]];
            direct += lightAt(light, worldPosition, n) * light.flicker;
        }
        // SS 4.7: the probes, through the same normal the lights use.
        ProbeLattice lattice =
            ProbeLattice(frame.probeSpacing, frame.probeCount, frame.probeTableMask, frame.probeLongestRun);
        vec3 indirect = indirectAt(lattice, worldPosition, n);
        // UTA-0112 SS 4.9: a surface of reflectance rho shows rho * (direct + indirect).
        colour = base.rgb * (direct + indirect);
        if (material.emit != NONE) colour += texture(textures[nonuniformEXT(material.emit)], uv).rgb;
    }

    outColour = vec4(colour, 1.0);
    // Current minus previous, in the target's UV units: +x right, +y down.
    outVelocity = (currentClip.xy / currentClip.w - previousClip.xy / previousClip.w) * 0.5;
}
