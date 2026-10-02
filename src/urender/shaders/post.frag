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
    float reserved;      // keeps flashFog on its 16 bytes
    float wobbleSeconds; // UTA-0215 SS 4.4: the light clock, wrapped
    vec4 flashFog;       // UTA-0215: xyz ViewFog; w nonzero under water
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

// UTA-0215 SS 4.4: the wobble -- this spec's call, chosen to be faint.
const float WOBBLE_AMPLITUDE = 0.003; // of the frame
const float WOBBLE_FREQUENCY = 24.0;
const float WOBBLE_SPEED = 1.5;

// UTA-0215 SS 4.2: what the original lays over the view under water, in a
// channel whose ViewFog part is `f`, the largest part being `most`:
// out = veil + (1 - veil) x (1 - most^2) x displayed. Measured 2026-10-02 in
// game by the UT_MonsterHunt session, which set the head zone's ViewFog per
// shot (work/uta0269/fogsweep/, mixsweep/, lavaswap/). One part alone: a black
// surface shows 31, 56, 100, 170 and 218 for f 0.05 to 0.6, whatever ViewFlash
// is, which 1 - (1 - f)^(2.1 + 0.02 / f) meets within 1. Beside a larger part
// a channel shows less -- 0.2 gives 100 alone, 90 beside 0.4, 80 beside 0.6 --
// which the exponent's (1 - 0.6 (most - f)) meets within 3, lava's (215, 79,
// 34) included. A part of 0 keeps 0.661 of the scene beside a 0.6, and water's
// keep about 0.95 of 1 - veil: 1 - most^2.
float waterVeil(float f, float most) {
    return f <= 0.0 ? 0.0 : 1.0 - pow(1.0 - min(f, 1.0), (2.1 + 0.02 / f) * (1.0 - 0.6 * (most - f)));
}

float encodeSrgb(float l) { return l <= 0.0031308 ? l * 12.92 : 1.055 * pow(l, 1.0 / 2.4) - 0.055; }
float decodeSrgb(float c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }

void main() {
    // Texel for texel, exactly as UTA-0014 reads it. Past a smaller region the
    // region's own edge repeats, so FSR 1's taps beyond it read the edge.
    ivec2 at = min(ivec2(gl_FragCoord.xy), ivec2(post.regionSize) - 1);
    bool underwater = post.flashFog.w != 0.0;
    if (underwater) {
        vec2 uv = gl_FragCoord.xy / vec2(post.regionSize);
        float phase = post.wobbleSeconds * WOBBLE_SPEED;
        vec2 shift = WOBBLE_AMPLITUDE * vec2(sin(uv.y * WOBBLE_FREQUENCY + phase), cos(uv.x * WOBBLE_FREQUENCY + phase));
        at = clamp(at + ivec2(round(shift * vec2(post.regionSize))), ivec2(0), ivec2(post.regionSize) - 1);
    }
    vec3 colour = texelFetch(hdr, at, 0).rgb;
    // UTA-0053: emission's glow, added before exposure at this texel's own place
    // in the half-size chain. Added rather than mixed: only emission feeds it.
    if (post.bloomStrength > 0.0)
        colour += texture(bloom, (vec2(at) + 0.5) / vec2(textureSize(hdr, 0))).rgb * post.bloomStrength;
    if (post.linearOutput == 0u) colour = toneMap(colour * post.exposure);
    // UTA-0215 SS 4.2: the original's view under water, on the display value.
    // It is a view effect, not light, so it applies under linearOutput too.
    if (underwater) {
        float most = max(post.flashFog.r, max(post.flashFog.g, post.flashFog.b));
        for (int c = 0; c < 3; ++c) {
            float veil = waterVeil(post.flashFog[c], most);
            float shown = (1.0 - most * most) * encodeSrgb(max(colour[c], 0.0));
            colour[c] = decodeSrgb(clamp(veil + (1.0 - veil) * shown, 0.0, 1.0));
        }
    }
    // UTA-0154: FSR 1 takes display-referred colour in [0, 1], and its header
    // allows gamma 2.0 for it.
    if (post.upscaleInput != 0u) colour = sqrt(clamp(colour, 0.0, 1.0));
    outColour = vec4(colour, 1.0);
}
