#version 450

// The output stage -- UTA-0014 SS 4.10. Exposure, then the tone map, into an
// _SRGB target whose store encodes. It applies to every colour the frame
// wrote, unlit surfaces and sky included; Config::linearOutput skips it and
// nothing else.

layout(set = 0, binding = 0) uniform sampler2D hdr;
// UTA-0053: the bloom chain's top level, half the target's size.
layout(set = 0, binding = 1) uniform sampler2D bloom;

layout(push_constant) uniform PostBlock {
    float exposure;
    uint linearOutput;
    uvec2 regionSize; // UTA-0051 SS 4.4: the top-left part of `hdr` this frame drew
    uint upscaleInput; // UTA-0154: write FSR 1's input rather than the output
    float bloomStrength; // UTA-0053: 0 where the tier draws no bloom
} post;

layout(location = 0) out vec4 outColour;

// UTA-0192: Khronos PBR Neutral's shoulder, WITHOUT its toe.
// https://github.com/KhronosGroup/ToneMapping/blob/main/PBR_Neutral/pbrNeutral.glsl
//
// The reference operator opens by subtracting an offset of up to 0.04 from
// every channel. UT99 has no such step: its 2x lightmap blend clips at white
// and leaves dim surfaces alone, so ours darkened every one of them.
//
// Measured against the original on DM-Deck16][, AS-Frigate and DM-Fetid, the
// toe is the whole of the mismatch and the shoulder costs nothing. Pooled block
// RMS: 34.85 with the toe, 32.49 without, where UT99's own clip scores 32.58
// and that clip UNDER the toe scores 35.06 -- worse than shipping. Tone mapped
// per pixel rather than per block, which is how the original's frames were
// formed: 34.37, 32.34 for the clip (ut-ants-uta0192/decompose.py).
//
// So the shoulder stays: it is free by this measure, and it is what keeps
// bright lamps, fog glow and UTA-0053's bloom from flat-topping. Every shoulder
// shape tried scored 32.49 to 32.55 and every variant carrying the toe 34.85 to
// 35.06, so this constant is not delicate -- the toe's absence is what matters.
//
// The score is LUMA, so it cannot see hue. The toe exists upstream to protect
// dark-tone saturation, and dropping it may move dark hues in a way nothing
// here measured.
vec3 toneMap(vec3 colour) {
    const float startCompression = 0.76;
    const float desaturation = 0.15;

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
    // UTA-0053: emission's glow, added before exposure at this texel's own place
    // in the half-size chain. Added rather than mixed: only emission feeds it.
    if (post.bloomStrength > 0.0)
        colour += texture(bloom, (vec2(at) + 0.5) / vec2(textureSize(hdr, 0))).rgb * post.bloomStrength;
    if (post.linearOutput == 0u) colour = toneMap(colour * post.exposure);
    // UTA-0154: FSR 1 takes display-referred colour in [0, 1], and its header
    // allows gamma 2.0 for it.
    if (post.upscaleInput != 0u) colour = sqrt(clamp(colour, 0.0, 1.0));
    outColour = vec4(colour, 1.0);
}
