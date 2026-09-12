#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

// The forward pass's vertex stage -- UTA-0014 SS 4.5 and SS 4.11.

#include "scene_bindings.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 uv;
layout(location = 3) out vec4 currentClip;
layout(location = 4) out vec4 previousClip;

void main() {
    Object object = objects[draw.objectIndex];
    vec4 world = object.model * vec4(inPosition, 1.0);
    worldPosition = world.xyz;
    worldNormal = mat3(object.normalMatrix) * inNormal;
    uv = inUv;

    // SS 4.11 provision 2: motion vectors from the current and previous clip
    // positions with the jitter excluded.
    currentClip = frame.viewProjUnjittered * world;
    previousClip = frame.previousViewProjUnjittered * (object.previousModel * vec4(inPosition, 1.0));

    gl_Position = frame.viewProj * world;
    // PF_FakeBackdrop is the level's sky: depth written at the far plane.
    if ((draw.polyFlags & PF_FAKE_BACKDROP) != 0u) gl_Position.z = gl_Position.w;
}
