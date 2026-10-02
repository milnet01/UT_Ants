#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

// UTA-0215: a speck in the water -- a soft round dot, pale in the water's own
// colour. Additive, as the flames it is drawn with: nothing is sorted. It adds
// nothing to the emission target, so it does not bloom.

#include "scene_bindings.glsl"

layout(location = 0) in vec2 moteUv;
layout(location = 1) in float moteFade;

layout(location = 0) out vec4 outColour;
layout(location = 1) out vec2 outVelocity; // 0, added: the target keeps what lies behind
layout(location = 2) out vec4 outEmission;

// The speck's light, as scene light: the water's colour this many times over,
// and a little white. This item's call, chosen on the pool.
const float MOTE_WATER_GAIN = 3.0;
const float MOTE_WHITE = 0.006;

void main() {
    float r2 = dot(moteUv, moteUv);
    if (r2 >= 1.0) discard;
    vec3 water = vec3(frame.waterFogR, frame.waterFogG, frame.waterFogB);
    vec3 colour = (water * MOTE_WATER_GAIN + vec3(MOTE_WHITE)) * ((1.0 - r2) * moteFade);
    outColour = vec4(colour, 0.0);
    outVelocity = vec2(0.0);
    outEmission = vec4(0.0);
}
