#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

// The forward pass's vertex stage -- UTA-0014 SS 4.5 and SS 4.11.

#include "scene_bindings.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in uint inZone; // UTA-0156 SS 4.4
layout(location = 4) in vec2 inOcclusionUv; // UTA-0164 SS 4.5

// UTA-0260: the depth pass and the forward pass both run this stage, and a
// surface must land on exactly the depth it was first drawn at.
invariant gl_Position;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 uv;
layout(location = 3) out vec4 currentClip;
layout(location = 4) out vec4 previousClip;
layout(location = 5) flat out uint zone;
layout(location = 6) out vec2 occlusionUv;

void main() {
    Object object = objects[draw.objectIndex];
    vec4 world = object.model * vec4(inPosition, 1.0);
    worldPosition = world.xyz;
    worldNormal = mat3(object.normalMatrix) * inNormal;
    uv = inUv + draw.panOffset; // UTA-0269
    zone = inZone;
    occlusionUv = inOcclusionUv;

    // SS 4.11 provision 2: motion vectors from the current and previous clip
    // positions with the jitter excluded.
    currentClip = frame.viewProjUnjittered * world;
    previousClip = frame.previousViewProjUnjittered * (object.previousModel * vec4(inPosition, 1.0));

    // UTA-0252: PF_FakeBackdrop, the level's sky, keeps its own depth like any
    // surface. Written at the far plane, it let the sky zone's room draw over it.
    gl_Position = frame.viewProj * world;
}
