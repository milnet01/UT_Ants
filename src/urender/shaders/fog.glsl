// The fog volume's grid and look -- docs/specs/UTA-0015-volumetric-fog.md SS 4.3.
//
// Fog.h states the grid too. The look constants -- FOG_ANISOTROPY,
// HAZE_EXTINCTION, HAZE_SCATTER, VOLUME_GLOW_SCALE and VOLUME_FOG_SCALE --
// live only here, so SS 7's
// sweeps change a shader and nothing else.

#ifndef UTA_FOG_GLSL
#define UTA_FOG_GLSL

const uvec3 FOG_GRID = uvec3(160u, 90u, 64u);
const float FOG_NEAR = 16.0;  // the slices' base depth, where fogCoordinate is 0; slice 0 runs from 0 to z(1)
const float FOG_FAR = 8192.0; // where the last slice ends

// Henyey-Greenstein's asymmetry: a little forward scattering. This spec's call.
const float FOG_ANISOTROPY = 0.2;

// -ln(0.9) / FOG_FAR: a tenth of the light is lost over the fog's whole depth
// (SS 7 step 1).
const float HAZE_EXTINCTION = 1.28614e-5;
// SS 7 step 1: the largest value within 1.0 of no haze, on every map. The
// superseded fits are in docs/fog-constant-fits-history.md.
// Refitted by UTA-0197 at EXPOSURE 5.03 with UTA-0192's toe-less tone map, on
// baker revision 27 bakes, light time pinned. Pixel RMS then block RMS on
// DM-Deck16][, AS-Frigate (blank pose 11 dropped) and DM-Fetid without
// volumetric lighting. No haze 41.4/37.2, 26.2/18.7, 52.1/48.2; 2.5e-5
// 40.8/36.9, 25.0/17.9, 52.1/48.3; 5e-5 40.8/37.0, 25.4/18.9, 52.1/48.4; 7.5e-5
// 41.0/37.3, 26.8/20.8, 52.2/48.5; 1e-4 41.3/37.8, 28.6/23.1, 52.3/48.7. The
// largest within 1.0 of no haze on every map under BOTH scorings is 5e-5;
// 7.5e-5 passes pixel scoring and fails block (ut-ants-uta0197/sweep.sh).
const float HAZE_SCATTER = 5.0e-5;
// SS 7 step 2, on DM-Fetid with volumetric lighting on. The superseded fits
// are in docs/fog-constant-fits-history.md.
// Refitted by UTA-0262, once the glow's colour was decoded from sRGB
// (displayedToLinear below), scored by the fog each depth band adds against
// the original's (RMS over the eight poses) and by pixel RMS; the values before
// it scored 16.8/33.8 (2026-10-02).
// By glow and fog: 5e-4 at 2.5e-2, 5e-2, 1e-1, 2e-1 17.3/35.6, 18.7/36.3,
// 21.0/37.6, 24.7/39.8; 1e-3 8.9/31.9, 10.2/32.3, 12.6/33.1, 16.5/34.9; 1.5e-3
// 11.3/31.9, 11.1/31.8, 11.6/32.1, 13.6/33.0; 2e-3 17.7/33.8, 17.0/33.5,
// 15.9/33.0, 15.4/33.0. Wider, at 0, 6.25e-3, 1.25e-2, 2.5e-2: 7.5e-4 10.4/32.7,
// 10.8/32.8, 11.2/32.9, 11.9/33.2; 1e-3 7.8/31.6, 8.0/31.7, 8.3/31.8, 8.9/31.9;
// 1.25e-3 8.8/31.5, 8.8/31.5, 8.8/31.5, 8.9/31.6. A repeated point scored the
// same. Glow 1e-3 is the band score's minimum inside the grid, its pixel RMS
// within 0.1 of the best. Fog moves the scores little
// below 2.5e-2 and least at 0, which would leave a light's VolumeFog drawing
// nothing, so SS 7 step 1's rule picks it: the largest within 1.0 of the best.
// The fog then adds 16.1 near the eye where the original adds 10.1 (was 25.9),
// and 97.8 past 1024 units where it adds 135.5 (was 74.9); far green over red
// is 1.20 to the original's 1.21 (was 1.09) (ut-ants-uta0262 probe, u262/).
const float VOLUME_GLOW_SCALE = 1.0e-3;
const float VOLUME_FOG_SCALE = 1.25e-2;

const float FOG_PI = 3.141592653589793;

// UTA-0262: UT99 lays a volumetric light's colour over the frame as a colour
// already on screen, so its hue is decoded from sRGB into the linear light the
// fog adds. Undecoded, DM-Fetid's pale green glow displays nearly grey.
vec3 displayedToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045)));
}

// The sRGB encode, displayedToLinear's inverse: linear light `l` as a value on
// screen. Here beside the decode so the pair is written once.
vec3 linearToDisplayed(vec3 l) {
    return mix(l * 12.92, 1.055 * pow(l, vec3(1.0 / 2.4)) - 0.055, greaterThan(l, vec3(0.0031308)));
}

// The view depth slice `k` starts at: 0 for the first, then exponential.
float sliceDepth(float k) {
    return k <= 0.0 ? 0.0 : FOG_NEAR * pow(FOG_FAR / FOG_NEAR, k / float(FOG_GRID.z));
}

// UTA-0215 SS 4.3: under water, a surface in the water keeps all its light
// out to WATER_FOG_START UT units, none past WATER_FOG_END, and a straight
// line between. The original fades nothing with distance: paired frames of
// DM-ArcaneTemple's pool keep the same share from its near wall to its far end
// (post.frag's waterVeil). So the fade starts past the farthest wall those
// frames show, and is this item's call, for the user's "visibility drops"
// in large water; under the output stage's veil a faded-out surface shows as
// the original shows a black one.
const float WATER_FOG_START = 800.0;
const float WATER_FOG_END = 2400.0;

// What survives the water between the eye and a surface `z` deep in view;
// the rest of what it shows is the water's own colour.
float waterKept(float z, uint underwater) {
    return underwater != 0u ? clamp((WATER_FOG_END - z) / (WATER_FOG_END - WATER_FOG_START), 0.0, 1.0) : 1.0;
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
