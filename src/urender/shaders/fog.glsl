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
// SS 7 step 1, on DM-Deck16][, block RMS against the original. Refitted by
// UTA-0165 at EXPOSURE 2.2: no haze 45.8; 1e-3 45.9; 2e-3 46.0; 4e-3 46.3;
// 6e-3 46.7; 8e-3 47.1; 1.2e-2 47.8. The largest within 1.0 of no haze.
const float HAZE_SCATTER = 6.0e-3;
// SS 7 step 2, on DM-Fetid with volumetric lighting on, the same measure, also
// refitted at EXPOSURE 2.2. By glow, at fog 0, 1.6e-2, 3.2e-2 and 6.4e-2: none
// 65.6; 3e-3 42.8, 42.8, 42.9, 43.2; 4e-3 42.7, 42.5, 42.3, 42.0; 5e-3 44.3 to
// 42.8; 6e-3 46.8 to 44.7; 8e-3 53.3 to 50.4. The pair below is the lowest, and
// its mean displayed luma of 73.1 is the nearest to the original's 76.8. Fog
// still gains at the widest swept, by 0.7, 0.5 then 0.3 a doubling.
const float VOLUME_GLOW_SCALE = 4.0e-3;
const float VOLUME_FOG_SCALE = 6.4e-2;

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
