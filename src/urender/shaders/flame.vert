#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

// The camera-facing flames -- docs/specs/UTA-0263-shader-flames.md SS 4.4.
// One instance per FLAM record, six vertices each and no vertex buffer: a
// quad standing on the flame's foot, turned about the world's vertical axis
// to face the eye, so it stays upright when the camera looks down on it.

#include "scene_bindings.glsl"

layout(location = 0) out vec2 flameUv; // u across, v up, both 0 to 1
layout(location = 1) out vec3 worldPosition;
layout(location = 2) flat out uint ramp;
layout(location = 3) flat out uint seed;

const vec2 CORNERS[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1));

void main() {
    FlameInstance flame = flameInstances[gl_InstanceIndex];
    vec2 corner = CORNERS[gl_VertexIndex];
    // UT99's Z is up. An eye straight above the foot faces it along X.
    vec2 toEye = frame.eye.xy - flame.base.xy;
    float reach = length(toEye);
    vec2 facing = reach > 1e-3 ? toEye / reach : vec2(1.0, 0.0);
    vec3 across = vec3(-facing.y, facing.x, 0.0);
    vec3 world = flame.base + across * ((corner.x - 0.5) * flame.width) + vec3(0.0, 0.0, corner.y * flame.height);

    flameUv = corner;
    worldPosition = world;
    ramp = flame.ramp;
    seed = flame.seed;
    gl_Position = frame.viewProj * vec4(world, 1.0);
}
