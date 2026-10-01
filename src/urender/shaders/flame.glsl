// A flame's heat and colour -- docs/specs/UTA-0263-shader-flames.md SS 4.4.
// One function for both kinds of flame: the camera-facing ones flame.frag
// draws, and the surfaces scene.frag draws with a flame look.
//
// THE NOISE IS COMPUTED HERE. No noise texture is added (SS 4.4).

#ifndef UTA_FLAME_GLSL
#define UTA_FLAME_GLSL

// SS 4.6's four fitted constants, and the shape beside them. Fitted
// 2026-10-01 against AS-Frigate's torches (NaliFX.TORCHES2) in the original:
// 11 views 300 units from a torch, 1280x720, 90 degrees; a flame pixel is
// one whose displayed luma rises over 10 above the same view with the fire
// blanked; ultra tier, light time pinned. TORCHES2's MaxFrameRate is 0, so
// the original steps its fire once a drawn frame; consecutive frames at
// 30 fps are its 1/30 s. Original vs these values:
//   mean luma over flame pixels 136.3 vs 140.4 (+3%)
//   total luma rise over the torch's box 1461k vs 1439k
//   mean change over flame pixels in 1/30 s 7.13 vs 7.52 (+5%)
//   flame pixels a view 21428 vs 18765 (-12%), colour 184,130,47 vs 195,134,73
// Sweeps (from the old 0.9 / 1.8 / 1.1 / 1.0 and offset 0.45, whose heat
// sat at 1 over most of the flame):
//   speed: fast 1.8 -> change 12.2, 1.0 -> 6.9, 0.6 -> 3.9; slow is kept at
//     half of fast and moves the change little (0.9/0.9 -> 6.2).
//   brightness: 0.3 -> rise 1441k, 0.45 -> 1675k, 0.7 -> 1939k, 1.0 -> 2165k.
//     The mean luma over flame pixels barely moves (141 to 149): a dimmer
//     flame loses its dim pixels from the mask. So the rise decides it.
//   erosion: with offset 0, 0.45 -> top at 0.95 of the quad, 0.6 -> 0.75.
const float FLAME_SPEED_SLOW = 0.49; // the warping octave's climb, flame heights a second
const float FLAME_SPEED_FAST = 0.98; // the warped octave's climb
const float FLAME_EROSION = 0.5;     // heat lost from base to tip
const float FLAME_BRIGHTNESS = 0.3;  // the ramp's scale, as emission

// The camera-facing flame's shape: not among SS 4.6's four, but set against
// the original's flame coverage. The original's torch is a column about 0.3
// of the quad wide that reaches its top; the old teardrop filled the quad.
//   width 1.0 taper 1.4 -> 38786 pixels; 0.35 / 3.0 -> 16759; 0.4 / 3.0 -> 18765.
const float FLAME_WIDTH = 0.4;        // the teardrop's half-width at its foot, a share of the quad's
const float FLAME_TAPER = 3.0;        // how fast it narrows to its tip; higher keeps it wide longer
const float FLAME_HEAT_OFFSET = 0.0;  // heat added to the noise before erosion; 0 is SS 4.4's plain form

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
    float heat = n + FLAME_HEAT_OFFSET - uv.y * FLAME_EROSION;
    // 3. The shape.
    if (shaped) {
        float across = abs(uv.x - 0.5) * 2.0;
        float halfWidth = FLAME_WIDTH * (1.0 - pow(uv.y, FLAME_TAPER)); // widest at the foot, a point at the tip
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
