#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

// The forward pass's shading -- UTA-0014 SS 4.5 to SS 4.10.
//
// One shader for the opaque and the translucent pass. The translucent pass
// binds only the colour attachment, so its velocity output goes nowhere --
// which is SS 4.11's "PF_Translucent batches write none".

#include "scene_bindings.glsl"

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec4 currentClip;
layout(location = 4) in vec4 previousClip;

layout(location = 0) out vec4 outColour;
layout(location = 1) out vec2 outVelocity;

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
        // Direct and indirect light arrive with their own passes; until then
        // a lit surface receives none.
        vec3 light = vec3(0.0);
        colour = base.rgb * light;
        if (material.emit != NONE) colour += texture(textures[nonuniformEXT(material.emit)], uv).rgb;
    }

    outColour = vec4(colour, 1.0);
    // Current minus previous, in the target's UV units: +x right, +y down.
    outVelocity = (currentClip.xy / currentClip.w - previousClip.xy / previousClip.w) * 0.5;
}
