#version 450

// FSR 1's sharpen -- UTA-0154. RCAS sharpens EASU's output, then the colour goes
// back from gamma 2.0 to linear light for the _SRGB target, whose store encodes.

layout(set = 0, binding = 0) uniform sampler2D source;

layout(location = 0) out vec4 outColour;

#define A_GPU 1
#define A_GLSL 1
#include "ffx_a.h"

#define FSR_RCAS_F 1
// RCAS reads one pixel either side, so the target's own edge is clamped to.
vec4 FsrRcasLoadF(ivec2 p) { return texelFetch(source, clamp(p, ivec2(0), textureSize(source, 0) - 1), 0); }
void FsrRcasInputF(inout float r, inout float g, inout float b) {}
#include "ffx_fsr1.h"

/// Stops of reduction from RCAS's maximum sharpness: AMD's own sample's default
/// (FidelityFX-FSR v1.0.2, sample/src/VK/SampleRenderer.h, rcasAttenuation).
const float SHARPNESS_STOPS = 0.25;

void main() {
    uvec4 con;
    FsrRcasCon(con, SHARPNESS_STOPS);
    float r, g, b;
    FsrRcasF(r, g, b, uvec2(gl_FragCoord.xy), con);
    vec3 colour = vec3(r, g, b);
    outColour = vec4(colour * colour, 1.0);
}
