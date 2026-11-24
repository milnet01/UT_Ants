#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

// UTA-0321: a corona -- a soft round glow, brightest at the light. Additive,
// as the flames it is drawn with: nothing is sorted. It adds nothing to the
// emission target: it is itself the glow, and bloom would spread it twice.

layout(location = 0) in vec2 coronaUv;
layout(location = 1) flat in vec3 coronaColour;

layout(location = 0) out vec4 outColour;
layout(location = 1) out vec2 outVelocity; // 0, added: the target keeps what lies behind
layout(location = 2) out vec4 outEmission;

// How fast the glow falls from its centre: exp(-CORONA_DECAY r), r 0 at the
// centre and 1 at the edge, eased to 0 at the edge. Fitted to the original's
// Light40 row through the lamp, r from 0.3 to 0.9: within 6% in log.
const float CORONA_DECAY = 3.75;

void main() {
    float r = length(coronaUv);
    if (r >= 1.0) discard;
    outColour = vec4(coronaColour * (exp(-CORONA_DECAY * r) * sqrt(1.0 - r)), 0.0);
    outVelocity = vec2(0.0);
    outEmission = vec4(0.0);
}
