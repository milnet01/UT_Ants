// A liquid surface's motion -- docs/specs/UTA-0105-shader-liquids.md SS 4.4.
// scene.frag shifts the picture by `offset` before sampling it, tilts a lit
// surface's normal by `tilt`, and a Wave draws `colour` in place of its picture.
//
// THE SETTINGS ARE UT99's BYTES. The bundle carries each texture's own values
// (ShaderTypes.h's Liquid); every mapping from them to motion is here, so a
// re-fit of SS 4.6's constants needs no re-bake.
//
// THE NOISE IS flame.glsl's value noise (UTA-0263 SS 4.4). No noise texture
// is added.

#ifndef UTA_LIQUID_GLSL
#define UTA_LIQUID_GLSL

#include "flame.glsl"
#include "light.glsl" // TWO_PI

const uint LIQUID_WET = 1u;
const uint LIQUID_ICE = 2u;
const uint LIQUID_WAVE = 3u;

const uint PANNING_LINEAR = 0u;
const uint PANNING_CIRCULAR = 1u;
const uint PANNING_GESTATION = 2u;
const uint PANNING_WAVY_X = 3u;
const uint PANNING_WAVY_Y = 4u;

// SS 4.6's fitted constants. NOT YET FITTED: these are first values, set so
// the motion is visible; the sweep against original frames replaces them and
// records itself here, as flame.glsl's do.
const float LIQUID_RIPPLE_TEXELS = 48.0; // one noise cell's span, texels of the picture
const float LIQUID_DRIFT = 0.25;         // cells a second the field drifts, at FX_Frequency 8
const float LIQUID_WARP_TEXELS = 3.0;    // the picture's largest shift at WaveAmp 128, texels
const float LIQUID_TILT = 0.8;           // the normal's slope at WaveAmp 128, per unit of noise slope
const float ICE_PAN_TEXELS = 32.0;       // texels a second at a pan speed of 255 (127 from still)
const float ICE_CYCLES = 0.02;           // circular and wavy cycles a second per unit of Frequency
const float ICE_SWING_TEXELS = 0.1;      // circular and wavy reach, texels per unit of Amplitude
const float WAVE_BUMP = 0.6;             // the shade's swing from its middle, at BumpMapLight 128
const float WAVE_HIGHLIGHT = 0.5;        // the highlight's peak, added to the shade

struct LiquidSample {
    vec2 offset; // added to the texture coordinate, in repeats
    vec2 tilt;   // tangent-space slope for a lit surface; 0 for Ice
    vec3 colour; // Wave only: its colour, in place of the picture
};


// The field's height at `p` (noise cells) and `seconds`: two octaves drifting
// across each other, so their sum ripples rather than slides.
float liquidHeight(vec2 p, float seconds, float drift, uint seed) {
    float a = flameNoise(p + vec2(0.71, 0.43) * (drift * seconds), seed);
    float b = flameNoise(p * 2.13 + vec2(-0.52, 0.81) * (drift * 1.37 * seconds), seed ^ 0x9E3779B9u);
    return 0.65 * a + 0.35 * b;
}

// The ramp at `shade` in [0, 1], between its eight entries.
vec3 liquidRamp(Liquid liquid, float shade) {
    float at = clamp(shade, 0.0, 1.0) * 7.0;
    int below = min(int(at), 6);
    return mix(liquid.ramp[below].rgb, liquid.ramp[below + 1].rgb, at - float(below));
}

// The Ice panning offset at `uv` and `seconds`, in repeats.
vec2 icePan(Liquid liquid, vec2 uv, float seconds) {
    vec2 size = max(liquid.size, vec2(1.0));
    // Epic's manual: 128 is still. fract keeps a long-running pan precise.
    vec2 linear = fract((liquid.pan - 128.0) / 127.0 * (ICE_PAN_TEXELS * seconds) / size);
    float phase = TWO_PI * fract(ICE_CYCLES * liquid.frequency * seconds);
    vec2 swing = vec2(ICE_SWING_TEXELS * liquid.amplitude) / size;
    vec2 within = fract(uv);
    switch (liquid.panning) {
    case PANNING_CIRCULAR: return swing * vec2(cos(phase), sin(phase));
    case PANNING_GESTATION: return (within - 0.5) * (swing.x * sin(phase));
    case PANNING_WAVY_X: return linear + vec2(swing.x * sin(phase + TWO_PI * within.y), 0.0);
    case PANNING_WAVY_Y: return linear + vec2(0.0, swing.y * sin(phase + TWO_PI * within.x));
    default: return linear; // PANNING_LINEAR, and a value MATS would have refused
    }
}

// SS 4.4: a liquid's offset, tilt and (for a Wave) colour at `uv`.
LiquidSample liquidAt(Liquid liquid, vec2 uv, float seconds, uint seed) {
    LiquidSample result;
    result.offset = vec2(0.0);
    result.tilt = vec2(0.0);
    result.colour = vec3(0.0);
    if (liquid.kind == LIQUID_ICE) {
        result.offset = icePan(liquid, uv, seconds);
        return result;
    }
    vec2 size = max(liquid.size, vec2(1.0));
    vec2 p = uv * size / LIQUID_RIPPLE_TEXELS;
    float drift = LIQUID_DRIFT * liquid.frequency / 8.0;
    // The slope by forward differences, a tenth of a cell apart.
    const float STEP = 0.1;
    float h = liquidHeight(p, seconds, drift, seed);
    vec2 slope = vec2(liquidHeight(p + vec2(STEP, 0.0), seconds, drift, seed) - h,
                      liquidHeight(p + vec2(0.0, STEP), seconds, drift, seed) - h) / STEP;
    float strength = liquid.amplitude / 128.0;
    result.offset = slope * (LIQUID_WARP_TEXELS * strength) / size;
    result.tilt = -slope * (LIQUID_TILT * strength);
    if (liquid.kind == LIQUID_WAVE) {
        // BumpMapAngle turns the light about the surface; BumpMapLight sets how
        // much the slope shows, more the further from the middle; PhongSize
        // widens the highlight.
        float angle = TWO_PI * liquid.bump.y / 256.0;
        vec2 toLight = vec2(cos(angle), sin(angle));
        float bump = WAVE_BUMP * (0.5 + abs(liquid.bump.x - 128.0) / 128.0);
        vec3 n = normalize(vec3(result.tilt, 1.0));
        vec3 halfway = normalize(vec3(toLight, 1.0) + vec3(0.0, 0.0, 1.0));
        float highlight = WAVE_HIGHLIGHT * pow(max(dot(n, halfway), 0.0), 4.0 + 1024.0 / (1.0 + liquid.bump.z));
        result.colour = liquidRamp(liquid, 0.5 + bump * dot(result.tilt, toLight) + highlight);
    }
    return result;
}

#endif
