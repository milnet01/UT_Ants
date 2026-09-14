#version 450

// FSR 1's upscale -- UTA-0154, amending UTA-0051 SS 4.4. EASU enlarges the top-left
// region of `source` to the whole target, from colour alone. `source` is
// post.frag's upscale input: tone mapped, gamma 2.0, and edge-clamped past the
// region, so a tap beyond it reads the region's own edge.

layout(set = 0, binding = 0) uniform sampler2D source;

layout(push_constant) uniform PostBlock {
    float exposure;
    uint linearOutput;
    uvec2 regionSize; // the part of `source` this frame drew
    uint upscaleInput;
} post;

layout(location = 0) out vec4 outColour;

#define A_GPU 1
#define A_GLSL 1
#include "ffx_a.h"

#define FSR_EASU_F 1
vec4 FsrEasuRF(vec2 p) { return textureGather(source, p, 0); }
vec4 FsrEasuGF(vec2 p) { return textureGather(source, p, 1); }
vec4 FsrEasuBF(vec2 p) { return textureGather(source, p, 2); }
#include "ffx_fsr1.h"

void main() {
    vec2 size = vec2(textureSize(source, 0));
    uvec4 con0, con1, con2, con3;
    FsrEasuCon(con0, con1, con2, con3, float(post.regionSize.x), float(post.regionSize.y), size.x, size.y, size.x,
               size.y);
    vec3 colour;
    FsrEasuF(colour, uvec2(gl_FragCoord.xy), con0, con1, con2, con3);
    outColour = vec4(colour, 1.0);
}
