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

// The scene's colour at this output pixel. A frame drawn at full size is read
// texel for texel, exactly as UTA-0014 reads it. A smaller region is stretched
// over the whole output with bilinear filtering that never reads outside it.
vec3 sceneColour() {
    ivec2 size = textureSize(hdr, 0);
    if (post.regionSize == uvec2(size)) return texelFetch(hdr, ivec2(gl_FragCoord.xy), 0).rgb;
    vec2 at = gl_FragCoord.xy * vec2(post.regionSize) / vec2(size) - 0.5;
    vec2 below = floor(at);
    vec2 weight = at - below;
    ivec2 last = ivec2(post.regionSize) - 1;
    ivec2 lo = clamp(ivec2(below), ivec2(0), last);
    ivec2 hi = clamp(ivec2(below) + 1, ivec2(0), last);
    vec3 top = mix(texelFetch(hdr, ivec2(lo.x, lo.y), 0).rgb, texelFetch(hdr, ivec2(hi.x, lo.y), 0).rgb, weight.x);
    vec3 bottom = mix(texelFetch(hdr, ivec2(lo.x, hi.y), 0).rgb, texelFetch(hdr, ivec2(hi.x, hi.y), 0).rgb, weight.x);
    return mix(top, bottom, weight.y);
}

void main() {
    vec3 colour = sceneColour();
    if (post.linearOutput == 0u) colour = pbrNeutral(colour * post.exposure);
    outColour = vec4(colour, 1.0);
}
