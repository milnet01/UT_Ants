#version 450

// The output stage -- UTA-0014 SS 4.10. Exposure, then the Khronos PBR Neutral
// tone map, into an _SRGB target whose store encodes. It applies to every
// colour the frame wrote, unlit surfaces and sky included; Config::linearOutput
// skips it and nothing else.

layout(set = 0, binding = 0) uniform sampler2D hdr;

layout(push_constant) uniform PostBlock {
    float exposure;
    uint linearOutput;
    uvec2 regionSize; // UTA-0051 SS 4.4: the top-left part of `hdr` this frame drew
    uint upscaleInput; // UTA-0154: write FSR 1's input rather than the output
} post;

layout(location = 0) out vec4 outColour;

// Khronos PBR Neutral, as the reference implementation writes it:
// https://github.com/KhronosGroup/ToneMapping/blob/main/PBR_Neutral/pbrNeutral.glsl
vec3 pbrNeutral(vec3 colour) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    float x = min(colour.r, min(colour.g, colour.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    colour -= offset;

    float peak = max(colour.r, max(colour.g, colour.b));
    if (peak < startCompression) return colour;

    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    colour *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(colour, newPeak * vec3(1.0), g);
}

void main() {
    // Texel for texel, exactly as UTA-0014 reads it. Past a smaller region the
    // region's own edge repeats, so FSR 1's taps beyond it read the edge.
    ivec2 at = min(ivec2(gl_FragCoord.xy), ivec2(post.regionSize) - 1);
    vec3 colour = texelFetch(hdr, at, 0).rgb;
    if (post.linearOutput == 0u) colour = pbrNeutral(colour * post.exposure);
    // UTA-0154: FSR 1 takes display-referred colour in [0, 1], and its header
    // allows gamma 2.0 for it.
    if (post.upscaleInput != 0u) colour = sqrt(clamp(colour, 0.0, 1.0));
    outColour = vec4(colour, 1.0);
}
