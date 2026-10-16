// How a swaying vertex moves -- UTA-0055 SS 4.5.
//
// docs/specs/UTA-0055-swaying-cloth-and-water.md SS 4.5.
//
// CHOSEN, NOT FITTED: the original game has no moving geometry to fit
// against. The amplitudes are the spec's starting values; the wavelengths
// and rates are picked to read as cloth in a breeze and as a slow swell.
//
// EVERY RATE IS A WHOLE NUMBER OF CYCLES PER SWAY_WRAP, the light clock's
// wrap (Frame.cpp's FLAME_CLOCK_WRAP), so the clock wrapping moves nothing
// (INV-8).

#ifndef SWAY_GLSL
#define SWAY_GLSL

const uint SWAY_NONE = 0u;
const uint SWAY_CLOTH = 1u;
const uint SWAY_WATER = 2u;

const float SWAY_TWO_PI = 6.28318530718;
const float SWAY_WRAP = 4096.0;

const float CLOTH_AMPLITUDE = 6.0; // units, at weight 1
const float WATER_AMPLITUDE = 3.0; // units, at weight 1
// Gerstner: each crest leans along its travel by this share of its height,
// so the surface never folds over itself.
const float WATER_STEEPNESS = 0.3;

// Cloth: three travelling sines of world position. xyz is the direction, w
// the wavelength (224, 144 and 96 units); CLOTH_CYCLES the cycles per wrap
// (0.6, 1 and 1.7 a second).
const vec4 CLOTH_WAVES[3] = vec4[3](
    vec4(normalize(vec3(0.6, 0.8, -0.5)), 224.0),
    vec4(normalize(vec3(-0.7, 0.5, -0.8)), 144.0),
    vec4(normalize(vec3(0.3, -0.9, -0.6)), 96.0));
const float CLOTH_CYCLES[3] = float[3](2458.0, 4096.0, 6963.0);
const float CLOTH_SHARES[3] = float[3](0.55, 0.3, 0.15);

// Water: four Gerstner waves across the plane. xy is the direction, z the
// wavelength (600, 340, 220 and 150 units), w the cycles per wrap, from deep
// water's rate sqrt(g k) at g = 515 units a second squared.
const vec4 WATER_WAVES[4] = vec4[4](
    vec4(normalize(vec2(1.0, 0.3)), 600.0, 1514.0),
    vec4(normalize(vec2(-0.4, 1.0)), 340.0, 2011.0),
    vec4(normalize(vec2(0.8, -0.6)), 220.0, 2500.0),
    vec4(normalize(vec2(-0.9, -0.5)), 150.0, 3028.0));
const float WATER_SHARES[4] = float[4](0.4, 0.3, 0.2, 0.1);

// UTA-0055 SS 4.5: a wave shorter than three of the cells its surface was cut
// into cannot be drawn without aliasing; it fades out from three cells to two.
float swayKept(float wavelength, float cell) {
    return smoothstep(2.0, 3.0, wavelength / max(cell, 1.0));
}

struct Sway {
    vec3 offset; // added to the vertex's position
    vec3 normal; // the moved surface's normal, unit
};

// The sway of the point `p` with normal `n` at `seconds`, `weight` 0 pinned
// to 1 free, on a surface cut into `cell`-unit cells. A kind of none, or a
// weight of 0, moves nothing.
Sway swayAt(vec3 p, vec3 n, uint kind, float weight, float cell, float seconds) {
    Sway result;
    result.offset = vec3(0.0);
    result.normal = n;
    if (kind == SWAY_CLOTH) {
        float height = 0.0;
        vec3 slope = vec3(0.0);
        for (int i = 0; i < 3; ++i) {
            vec3 k = CLOTH_WAVES[i].xyz * (SWAY_TWO_PI / CLOTH_WAVES[i].w);
            float share = CLOTH_SHARES[i] * swayKept(CLOTH_WAVES[i].w, cell);
            float phase = dot(k, p) - SWAY_TWO_PI * CLOTH_CYCLES[i] / SWAY_WRAP * seconds;
            height += share * sin(phase);
            slope += share * cos(phase) * k;
        }
        float amplitude = CLOTH_AMPLITUDE * weight;
        result.offset = n * (amplitude * height);
        result.normal = normalize(n - amplitude * (slope - n * dot(slope, n)));
    } else if (kind == SWAY_WATER) {
        float height = 0.0;
        vec2 across = vec2(0.0);
        vec2 slope = vec2(0.0);
        for (int i = 0; i < 4; ++i) {
            float k = SWAY_TWO_PI / WATER_WAVES[i].z;
            float share = WATER_SHARES[i] * swayKept(WATER_WAVES[i].z, cell);
            float phase = k * dot(WATER_WAVES[i].xy, p.xy) - SWAY_TWO_PI * WATER_WAVES[i].w / SWAY_WRAP * seconds;
            height += share * sin(phase);
            slope += share * k * cos(phase) * WATER_WAVES[i].xy;
            across += WATER_STEEPNESS * share * cos(phase) * WATER_WAVES[i].xy;
        }
        float amplitude = WATER_AMPLITUDE * weight;
        result.offset = n * (amplitude * height) + vec3(across * amplitude, 0.0);
        result.normal = normalize(n - vec3(slope * (WATER_AMPLITUDE * weight), 0.0));
    }
    return result;
}

#endif
