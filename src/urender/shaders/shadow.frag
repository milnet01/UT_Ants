#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

// A shadow tile's depth pass. Depth only; a masked surface casts the shape its
// cutout has, at SS 4.5's threshold, so light passes where the forward pass
// shows a hole.

#define UTA_SHADOW_PASS
#include "scene_bindings.glsl"

layout(location = 0) in vec2 uv;

void main() {
    if ((shadowDraw.polyFlags & PF_MASKED) != 0u) {
        Material material = materials[shadowDraw.materialIndex];
        if (texture(textures[nonuniformEXT(material.base)], uv).a < MASK_THRESHOLD) discard;
    }
}
