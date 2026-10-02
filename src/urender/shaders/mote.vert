#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

// UTA-0215: specks drifting in the water around the eye. One instance a speck,
// six vertices and no vertex buffer. Each speck's place is hashed from its
// index, rises and wanders slowly, and wraps in a box about the eye: so a speck
// holds its place in the world as the eye moves, and new ones come in at the
// box's edge, faded in there so none pops. Drawn only with the eye under water.

#include "scene_bindings.glsl"

layout(location = 0) out vec2 moteUv; // -1 to 1 across the speck
layout(location = 1) out float moteFade;

// This item's calls, chosen on DM-ArcaneTemple's pool, not fitted: the
// original draws none.
const float MOTE_BOX = 400.0;  // UT units across the box about the eye
const float MOTE_SIZE = 0.8;   // UT units from a speck's centre to its edge
const float MOTE_RISE = 3.0;   // UT units a second
const float MOTE_WANDER = 6.0; // UT units a speck sways either way

const vec2 CORNERS[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));

// PCG's output permutation: a well-spread 32-bit hash of the index.
uint moteHash(uint v) {
    uint s = v * 747796405u + 2891336453u;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}

void main() {
    uint i = uint(gl_InstanceIndex);
    vec3 h = vec3(moteHash(i), moteHash(i ^ 0x9e3779b9u), moteHash(i ^ 0x85ebca6bu)) / 4294967295.0;
    float t = frame.flameSeconds;
    vec3 sway = MOTE_WANDER * vec3(sin(t * 0.3 + h.x * 6.2832), cos(t * 0.27 + h.y * 6.2832), 0.0);
    vec3 home = h * MOTE_BOX + vec3(0.0, 0.0, MOTE_RISE * t) + sway;
    vec3 offset = mod(home - frame.eye, MOTE_BOX) - 0.5 * MOTE_BOX;

    // The view's right and up, its matrix's first two rows (+X right, +Y up).
    vec3 right = vec3(frame.view[0][0], frame.view[1][0], frame.view[2][0]);
    vec3 up = vec3(frame.view[0][1], frame.view[1][1], frame.view[2][1]);
    vec2 corner = CORNERS[gl_VertexIndex];
    vec3 world = frame.eye + offset + (right * corner.x + up * corner.y) * MOTE_SIZE;

    moteUv = corner;
    moteFade = 1.0 - smoothstep(0.3 * MOTE_BOX, 0.5 * MOTE_BOX, length(offset));
    gl_Position = frame.viewProj * vec4(world, 1.0);
}
