// UTA-0180: large-scale variation -- a slow change in a lit surface's
// brightness across the world, so a texture repeated over a wall or a floor
// stops reading as one patch in a grid. It moves no picture: panels, trims and
// signs drawn to fit their surface stay where they are. The world, not the
// texture coordinate, carries it, so two surfaces meeting at a corner agree.
//
// UTA-0277 SS 4.4: and the half that does move the picture, for a material the
// bake judged natural -- UTA-0089 SS 4.4's two-copy blend, which water draws
// its Wet picture with too.

#ifndef UTA_VARIATION_GLSL
#define UTA_VARIATION_GLSL

#include "flame.glsl"

// World units across one noise cell: four repeats of a 256-texel texture at
// DrawScale 1, the same span water.glsl varies its picture over. This spec's
// call, not fitted.
const float VARIATION_CELL = 1024.0;
// The most a lit surface's reflectance moves either way. Swept on the three
// reference maps against the original at baker revision 34, pixel/block RMS:
// 0 -> DM-Deck16][ 39.63/36.20, AS-Frigate 24.40/17.08, DM-Fetid 31.56/26.80;
// 0.1 -> 39.64/36.20, 24.29/16.87, 31.59/26.83; 0.2 -> 39.65/36.21,
// 24.21/16.71, 31.63/26.87; 0.3 -> 39.66/36.22, 24.16/16.58, 31.67/26.91.
// UTA-0015 SS 7 step 1's rule, the largest within 1.0 of none, binds nowhere
// in that range, so the value is the largest swept and looked at: at 0.3 a
// floor's brightness drifts gently and nothing reads as blotches, the mean of
// three noises keeping the spread well inside it (ut-ants u180/deck-var.png).
const float VARIATION_AMPLITUDE = 0.3;

// The reflectance factor at world point `p`, 1 - AMPLITUDE to 1 + AMPLITUDE:
// three value noises, one on each axis plane, so it is continuous in 3D.
float variationAt(vec3 p) {
    vec3 q = p / VARIATION_CELL;
    float n = (flameNoise(q.xy, 11u) + flameNoise(q.yz, 12u) + flameNoise(q.zx, 13u)) / 3.0;
    return 1.0 + VARIATION_AMPLITUDE * (2.0 * n - 1.0);
}

// UTA-0089 SS 4.4's constants, moved here by UTA-0277 when the blend became
// shared. Water's own fitting record stays in water.glsl.
const float TILE_VARIATION_REPEATS = 4.0; // repeats of the picture across one noise cell
// Quilez's own value, not fitted: how many offsets the noise picks between.
const float TILE_OFFSETS = 8.0;

// Quilez's third technique: two offsets the noise at `uv` picks between, and
// how far along from the first to the second it is, 0 to 1.
struct TileBlend {
    vec2 offsetA;
    vec2 offsetB;
    float along;
};

TileBlend tileBlendAt(vec2 uv, uint seed) {
    float k = flameNoise(uv / TILE_VARIATION_REPEATS, seed) * TILE_OFFSETS;
    float ia = floor(k);
    return TileBlend(sin(vec2(3.0, 7.0) * ia), sin(vec2(3.0, 7.0) * (ia + 1.0)), k - ia);
}

// The second copy's weight, given what the two copies of the picture show
// there: where they differ the blend leans to one, so it keeps its contrast
// rather than averaging both to grey.
float tileWeight(TileBlend blend, vec3 a, vec3 b) {
    vec3 difference = a - b;
    return smoothstep(0.2, 0.8, blend.along - 0.1 * (difference.r + difference.g + difference.b));
}

#endif
