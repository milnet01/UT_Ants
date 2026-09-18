// The fog volume's grid and look -- docs/specs/UTA-0015-volumetric-fog.md SS 4.3.
//
// Fog.h states the grid too. The four look constants live only here, so SS 7's
// sweeps change a shader and nothing else.

#ifndef UTA_FOG_GLSL
#define UTA_FOG_GLSL

const uvec3 FOG_GRID = uvec3(160u, 90u, 64u);
const float FOG_NEAR = 16.0;  // where slice 1 starts
const float FOG_FAR = 8192.0; // where the last slice ends

// Henyey-Greenstein's asymmetry: a little forward scattering. This spec's call.
const float FOG_ANISOTROPY = 0.2;

// -ln(0.9) / FOG_FAR: a tenth of the light is lost over the fog's whole depth
// (SS 7 step 1).
const float HAZE_EXTINCTION = 1.28614e-5;
// SS 7 step 1, on DM-Deck16][, block RMS against the original at EXPOSURE 2.2.
// Refitted by UTA-0166, which gave every light a shadow tile: about fourteen
// times as many lights scatter into the haze as did when 6.0e-3 was chosen, so
// that value now scores 105.4 and is far too bright. No haze 45.7; 1e-4 43.1;
// 2e-4 41.2; 2.5e-4 40.7; 3e-4 40.5; 3.5e-4 40.4; 4e-4 40.5; 5e-4 41.1;
// 1e-3 48.4; 2e-3 67.3; 6e-3 105.4. Repeating the no-haze control moved it 0.1,
// so 3e-4 to 4e-4 are one value; the largest is taken, its mean displayed luma
// of 59.8 being the nearest to the original's 68.2. Haze now IMPROVES the
// match -- before UTA-0166 every value made it worse than no haze at all.
// Refitted by UTA-0165 at EXPOSURE 5.5, once lights carried LevelInfo.Brightness
// and the exposure rose to match: haze made up for the low exposure, and now
// every value costs again. No haze 37.5; 1e-4 38.0; 1.5e-4 39.3; 2e-4 41.0;
// 4e-4 49.3; 8e-4 66.3; 1.6e-3 89.1. SS 7 step 1's first rule holds again:
// the largest value within 1.0 of no haze.
// Refitted by UTA-0178, with the rule applied on each of the three maps, since
// an outdoor map's large lights fill its whole view with haze where an indoor
// map's do not. At EXPOSURE 5.4, haze scale 0, 0.25, 0.5 and 1 of 1e-4:
// AS-Frigate 36.5, 37.3, 39.4, 44.7; DM-Deck16][ 37.1, 36.8, 36.9, 37.4;
// DM-Fetid with volumetric lighting on 34.7, 34.5, 34.3 at the first three.
// The largest within 1.0 of no haze on every map is 0.25. At 2.5e-5 itself,
// 36.8, 36.9 and 34.5 (ut-ants-uta0178/haze3.sh).
const float HAZE_SCATTER = 2.5e-5;
// SS 7 step 2, on DM-Fetid with volumetric lighting on, the same measure and
// exposure, refitted by UTA-0166 on top of the haze above. By glow at fog
// 6.4e-2: none 68.2; 2e-3 46.7; 2.5e-3 43.6; 3e-3 41.4; 4e-3 39.2; 5e-3 39.1;
// 8e-3 45.8; 1.6e-2 70.8; 3.2e-2 98.9. Then by fog at glow 5e-3: none 40.5;
// 3.2e-2 39.7; 6.4e-2 39.1; 1.28e-1 38.4; 2.56e-1 38.7; 5.12e-1 41.7.
// Both are measured minima. The earlier fit stopped fog at 6.4e-2, the edge of
// its sweep, where it was still gaining; swept wider it turns at 1.28e-1.
// Refitted by UTA-0165 at EXPOSURE 5.5 with the haze above; no fog 57.8. By
// glow at fog 5e-2: 1e-3 37.2; 1.5e-3 35.2; 2e-3 36.7; 2.5e-3 40.1; 4e-3 53.5;
// 8e-3 81.9. By fog at glow 2e-3: 2.5e-2 37.5; 5e-2 36.7; 1e-1 35.4; 2e-1 34.2.
// Then both past that edge: at fog 2e-1, glow 1.5e-3 35.4, 2.5e-3 35.4, 3e-3
// 38.1; at fog 4e-1, glow 2e-3 34.9, 3e-3 34.3, 4e-3 38.7. The lowest measured
// is glow 2e-3 at fog 2e-1, whose mean displayed luma is 76.0 to the original's
// 76.8 (ut-ants-uta0156/fogglow17.sh, fogglow17b.sh).
// Rechecked by UTA-0168 on baker revision 18 at EXPOSURE 5.4; no fog 58.0. At
// fog 2e-1, glow 1.5e-3 35.5, 2e-3 34.1, 2.5e-3 35.1; at fog 4e-1, glow 2e-3
// 35.0, 3e-3 34.1. The two 34.1s tie, and glow 2e-3 at fog 2e-1 stays: its mean
// displayed luma is 75.1 to the original's 76.8, the other's 80.5
// (ut-ants-uta0156/fogglow18.sh). HAZE_SCATTER was not re-swept at 5.4: it was
// fitted on DM-Deck16][, whose lightmap agreement UTA-0168 moved only from
// 99.2% to 99.6%.
const float VOLUME_GLOW_SCALE = 2.0e-3;
const float VOLUME_FOG_SCALE = 2.0e-1;

const float FOG_PI = 3.141592653589793;

// The view depth slice `k` starts at: 0 for the first, then exponential.
float sliceDepth(float k) {
    return k <= 0.0 ? 0.0 : FOG_NEAR * pow(FOG_FAR / FOG_NEAR, k / float(FOG_GRID.z));
}

// Where view depth `z` sits along the slices, 0 at FOG_NEAR and 1 at FOG_FAR.
float fogCoordinate(float z) {
    return log(max(z, FOG_NEAR) / FOG_NEAR) / log(FOG_FAR / FOG_NEAR);
}

// Henyey-Greenstein, with `cosTheta` 1 when looking straight into the light.
float phaseOf(float cosTheta) {
    float g = FOG_ANISOTROPY;
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * FOG_PI * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5));
}

#endif
