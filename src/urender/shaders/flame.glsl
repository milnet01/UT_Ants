// A flame's heat and colour -- docs/specs/UTA-0263-shader-flames.md SS 4.4.
// One function for both kinds of flame: the camera-facing ones flame.frag
// draws, and the surfaces scene.frag draws with a flame look.
//
// THE NOISE IS COMPUTED HERE. No noise texture is added (SS 4.4).

#ifndef UTA_FLAME_GLSL
#define UTA_FLAME_GLSL

// SS 4.6's four fitted constants. Each records its sweep beside it.
// UNFITTED: the values below are a starting point, not yet swept against
// original frames.
const float FLAME_SPEED_SLOW = 0.9; // the warping octave's climb, flame heights a second
const float FLAME_SPEED_FAST = 1.8; // the warped octave's climb
const float FLAME_EROSION = 1.1;    // heat lost from base to tip
const float FLAME_BRIGHTNESS = 1.0; // the ramp's scale, as emission

// A repeatable hash of a lattice point and a seed to [0, 1).
float flameHash(ivec2 cell, uint seed) {
    uint h = uint(cell.x) * 1597334677u ^ uint(cell.y) * 3812015801u ^ seed * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;
    return float(h >> 8) * (1.0 / 16777216.0);
}

// Value noise in [0, 1): a random value per lattice point, eased between.
float flameNoise(vec2 p, uint seed) {
    vec2 whole = floor(p);
    vec2 within = p - whole;
    vec2 ease = within * within * (3.0 - 2.0 * within);
    ivec2 cell = ivec2(whole);
    float a = flameHash(cell, seed);
    float b = flameHash(cell + ivec2(1, 0), seed);
    float c = flameHash(cell + ivec2(0, 1), seed);
    float d = flameHash(cell + ivec2(1, 1), seed);
    return mix(mix(a, b, ease.x), mix(c, d, ease.x), ease.y);
}

// SS 4.4's heat at (u across, v up), both 0 to 1, at `seconds`. `shaped` is a
// camera-facing flame, whose teardrop fades to 0 at the sides and the base.
float flameHeat(vec2 uv, float seconds, uint seed, bool shaped) {
    // 1. Two octaves, scrolled up at different speeds; the slow one warps the
    // fast one's coordinates, more towards the top.
    vec2 p = uv * vec2(2.0, 3.0);
    float warp = flameNoise(p * 0.8 - vec2(0.0, seconds * FLAME_SPEED_SLOW * 2.4), seed) - 0.5;
    vec2 q = p + vec2(warp * (0.3 + 1.5 * uv.y), 0.0);
    float n = 0.65 * flameNoise(q - vec2(0.0, seconds * FLAME_SPEED_FAST * 3.0), seed + 1u)
              + 0.35 * flameNoise(q * 2.3 - vec2(0.0, seconds * FLAME_SPEED_FAST * 6.9), seed + 2u);
    // 2. Tongues thin and break off as they rise.
    float heat = n + 0.45 - uv.y * FLAME_EROSION;
    // 3. The shape.
    if (shaped) {
        float across = abs(uv.x - 0.5) * 2.0;
        float halfWidth = 1.0 - pow(uv.y, 1.4); // widest at the foot, a point at the tip
        float sides = 1.0 - smoothstep(halfWidth * 0.55, halfWidth, across);
        float foot = smoothstep(0.0, 0.08, uv.y);
        heat *= sides * foot;
    }
    return clamp(heat, 0.0, 1.0);
}

// The ramp's colour at `heat`, linear and scaled to emission. `first` is the
// ramp's first of its eight entries in flameRamps.
vec3 flameColour(uint first, float heat) {
    float at = heat * 7.0;
    uint below = min(uint(at), 6u);
    vec3 colour = mix(flameRamps[first + below].rgb, flameRamps[first + below + 1u].rgb, at - float(below));
    return colour * FLAME_BRIGHTNESS;
}

#endif
