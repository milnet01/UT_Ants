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
// SS 7 step 1, on DM-Deck16][ at EXPOSURE 3.2, block RMS against the original:
// no haze 46.2; 2.5e-4 46.3; 5e-4 46.3; 1e-3 46.4; 2e-3 46.5; 4e-3 46.9;
// 8e-3 47.8. The largest within 1.0 of no haze.
const float HAZE_SCATTER = 4.0e-3;
// SS 7 step 2, on DM-Fetid with volumetric lighting on, the same measure. By
// glow: 2.5e-3 38.9 with no fog; 3e-3 39.5, 39.3 and 38.9 at fog 0, 8e-3 and
// 3.2e-2; 3.5e-3 40.9 to 40.0; 4e-3 42.9 to 41.7; 5e-3 and 6e-3 higher still.
// 2.5e-3 with no fog ties 3e-3 at 3.2e-2, whose mean brightness is nearer the
// original's; fog helps a little at every glow, and 3.2e-2 is the most swept.
const float VOLUME_GLOW_SCALE = 3.0e-3;
const float VOLUME_FOG_SCALE = 3.2e-2;

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
