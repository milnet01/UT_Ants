#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_KHR_shader_subgroup_ballot : require

// The blades' shading -- docs/specs/UTA-0293-grass.md SS 4.5. Each blade is
// coloured by the ground it stands on and lit as that ground is: through its
// clump's shadow-mask chart and texel where it has one, else its cluster's
// lights, plus the probes' indirect light (surface_light.glsl, as scene.frag).
// Lit from both sides: the face turned to the eye is the one shaded, its normal
// bent toward up. Opaque: it writes colour, velocity and an emission of none.
//
// Left out against scene.frag, for a first iteration: the baked occlusion (a
// clump carries no AOCC coordinate), the caustics (no grass grows under water)
// and the outdoor haze (grass ends at GRASS_END, nearer than haze begins on
// most maps).

#define UTA_GRASS_PASS
#include "scene_bindings.glsl"
#include "fog.glsl"
#include "light.glsl"
#include "probes.glsl"
#include "surface_light.glsl"

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 bladeNormal;
layout(location = 2) in float heightShare;
layout(location = 3) in vec4 currentClip;
layout(location = 4) in vec4 previousClip;
layout(location = 5) flat in uint clumpIndex;

layout(location = 0) out vec4 outColour;
layout(location = 1) out vec2 outVelocity;
layout(location = 2) out vec4 outEmission;

// A coarse mip of the ground's picture: its colour around the clump, not a
// texel's. First values, SS 13.
const float COLOUR_LOD = 4.0;
const float ROOT_SHADE = 0.55;
const float TIP_SHADE = 1.15;
const float NORMAL_UP = 0.6; // how far the shading normal bends from the blade's face toward up
const vec3 UP = vec3(0.0, 0.0, 1.0);

void main() {
    GrassClump clump = grassClumps[clumpIndex];
    Material material = materials[clump.material];
    vec3 ground = material.base != NONE
        ? textureLod(textures[nonuniformEXT(material.base)], clump.uv, COLOUR_LOD).rgb
        : vec3(0.5);
    vec3 reflectance = ground * mix(ROOT_SHADE, TIP_SHADE, heightShare);

    vec3 face = normalize(bladeNormal);
    if (dot(face, frame.eye - worldPosition) < 0.0) face = -face;
    vec3 n = normalize(mix(face, UP, NORMAL_UP));

    // The ground's normal is what the shadow map offsets along.
    bool charted = clump.maskChart != MASK_NO_CHART;
    vec3 direct = charted ? chartedLight(n, UP, worldPosition, clump.maskChart, clump.maskTexel)
                          : clusterLight(n, UP, false, worldPosition);
    ProbeLattice lattice = frameProbeLattice();
    vec3 probePoint = worldPosition + UP * (0.5 * float(frame.probeSpacing));
    vec3 indirect = indirectAt(lattice, probePoint, n);
    // UTA-0338 SS 4.5, as scene.frag: off a chart, the sun through the probes.
    if (!charted && frame.sun != NONE) direct += lightAt(lights[frame.sun], worldPosition, n) * sunSeenAt(lattice, probePoint);
    vec3 colour = reflectance * (pow(LIGHT_GAIN * direct, vec3(DISPLAY_LIGHT_POWER))
                                 + indirect * pow(LIGHT_GAIN, DISPLAY_LIGHT_POWER));

    // UTA-0015 SS 4.3: the fog between the eye and the blade, as scene.frag.
    float viewDepth = (frame.view * vec4(worldPosition, 1.0)).z;
    vec3 fogAt = vec3(gl_FragCoord.xy / frame.viewportSize, fogCoordinate(viewDepth) - 0.5 / float(FOG_GRID.z));
    vec4 fogged = textureLod(fogVolume, fogAt, 0.0);
    colour = colour * fogged.a + fogged.rgb;

    outColour = vec4(colour, 1.0);
    outVelocity = (currentClip.xy / currentClip.w - previousClip.xy / previousClip.w) * 0.5;
    outEmission = vec4(0.0, 0.0, 0.0, 1.0);
}
