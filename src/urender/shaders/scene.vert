#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

// The forward pass's vertex stage -- UTA-0014 SS 4.5 and SS 4.11.

#include "scene_bindings.glsl"
#include "sway.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in uint inZone; // UTA-0156 SS 4.4
layout(location = 4) in vec2 inOcclusionUv; // UTA-0164 SS 4.5
layout(location = 5) in uint inMaskChart;   // UTA-0326 SS 4.5
layout(location = 6) in vec2 inMaskTexel;
layout(location = 7) in uvec4 inSway; // UTA-0055 SS 4.5: y the kind, z the weight 0 to 255, w the cell

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
layout(location = 7) flat out uint maskChart; // UTA-0326 SS 4.5
layout(location = 8) out vec2 maskTexel;

void main() {
    Object object = objects[draw.objectIndex];
    // UTA-0055 SS 4.5: a swaying vertex moves before its object's matrix, and
    // its last frame's place uses the last frame's clock, so its motion
    // vector carries the sway. The depth pass runs this too, so both agree.
    vec3 position = inPosition;
    vec3 normal = inNormal;
    vec3 previousPosition = inPosition;
    if (inSway.y != SWAY_NONE) {
        float weight = float(inSway.z) / 255.0;
        float cell = 16.0 * float(inSway.w);
        Sway now = swayAt(inPosition, inNormal, inSway.y, weight, cell, frame.flameSeconds);
        position += now.offset;
        normal = now.normal;
        previousPosition += swayAt(inPosition, inNormal, inSway.y, weight, cell, frame.previousFlameSeconds).offset;
    }
    vec4 world = object.model * vec4(position, 1.0);
    worldPosition = world.xyz;
    worldNormal = mat3(object.normalMatrix) * normal;
    uv = inUv + draw.panOffset; // UTA-0269
    zone = inZone;
    occlusionUv = inOcclusionUv;
    maskChart = inMaskChart;
    maskTexel = inMaskTexel;

    // SS 4.11 provision 2: motion vectors from the current and previous clip
    // positions with the jitter excluded.
    currentClip = frame.viewProjUnjittered * world;
    previousClip = frame.previousViewProjUnjittered * (object.previousModel * vec4(previousPosition, 1.0));

    // UTA-0252: PF_FakeBackdrop, the level's sky, keeps its own depth like any
    // surface. Written at the far plane, it let the sky zone's room draw over it.
    gl_Position = frame.viewProj * world;
}
