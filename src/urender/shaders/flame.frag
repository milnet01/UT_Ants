#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

// The camera-facing flames' shading -- docs/specs/UTA-0263-shader-flames.md
// SS 4.4. Additive: draw order does not matter and nothing is sorted. It
// writes colour and the emission target, so a flame feeds the bloom; the
// translucent pass binds colour alone and could not (INV-7).

#include "scene_bindings.glsl"
#include "fog.glsl"
#include "flame.glsl"

layout(location = 0) in vec2 flameUv;
layout(location = 1) in vec3 worldPosition;
layout(location = 2) flat in uint ramp;
layout(location = 3) flat in uint seed;

layout(location = 0) out vec4 outColour;
layout(location = 1) out vec2 outVelocity; // 0, added: the target keeps what lies behind
layout(location = 2) out vec4 outEmission;

void main() {
    float heat = flameHeat(flameUv, frame.flameSeconds, seed, true);
    if (heat <= 0.0) discard;
    vec3 colour = flameColour(ramp, heat);
    // SS 6: scene.frag fogs each surface it shades, so no later pass would
    // veil a flame. The transmittance to this depth dims it as it dims the
    // wall behind; the in-scattering is already on that wall.
    float viewDepth = (frame.view * vec4(worldPosition, 1.0)).z;
    vec3 fogAt = vec3(gl_FragCoord.xy / frame.viewportSize, fogCoordinate(viewDepth) - 0.5 / float(FOG_GRID.z));
    colour *= textureLod(fogVolume, fogAt, 0.0).a;
    colour *= waterAbsorption(viewDepth, frame.cameraUnderwater); // UTA-0215 SS 4.3
    outColour = vec4(colour, 0.0);
    outVelocity = vec2(0.0);
    outEmission = vec4(colour, 0.0);
}
