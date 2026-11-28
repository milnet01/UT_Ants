#version 450

// UTA-0053's anti-aliasing: FXAA, after Timothy Lottes' FXAA 3.11 quality
// path (NVIDIA, 2011), written here from its method rather than its source.
// A pixel whose neighbours differ enough lies on an edge; the edge's run is
// searched both ways, and the pixel is blended across the edge by how near it
// lies to the run's nearer end. The blend never reaches past the one pixel
// across the edge.
//
// `source` is post.frag's upscale input: tone mapped, gamma 2.0. FXAA's
// thresholds are set for display-referred colour, which that is. Writing
// FSR 1's input, the colour stays gamma 2.0 for EASU; writing the output, it
// goes back to linear light for the _SRGB target, as fsr_rcas.frag's does.

layout(set = 0, binding = 0) uniform sampler2D source;

// The head of post.frag's PostBlock, which ShaderTypes.h's PostConstants
// lays out: change all four together (fsr_easu.frag, fxaa.frag).
layout(push_constant) uniform PostBlock {
    float exposure;
    uint linearOutput;
    uvec2 regionSize; // the part of `source` this frame drew
    uint upscaleInput; // nonzero: write FSR 1's input, not the output
} post;

layout(location = 0) out vec4 outColour;

// FXAA 3.11's defaults for its quality path: the smallest contrast, as a
// share of the brightest neighbour and absolutely, that counts as an edge,
// and how much of a lone bright or dark pixel is blended away.
const float EDGE_THRESHOLD = 0.166;
const float EDGE_THRESHOLD_MIN = 0.0833;
const float SUBPIXEL = 0.75;

// How far each step of the edge search moves along the edge, in pixels: 25
// in total each way. Whole pixels, so each step reads two texels exactly.
const int SEARCH_STEPS = 8;
const int SEARCH_STEP[SEARCH_STEPS] = int[](1, 1, 1, 2, 2, 2, 4, 12);

ivec2 regionMax() { return ivec2(post.regionSize) - 1; }

vec3 colourAt(ivec2 p) { return texelFetch(source, clamp(p, ivec2(0), regionMax()), 0).rgb; }

float lumaOf(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

float lumaAt(ivec2 p) { return lumaOf(colourAt(p)); }

vec3 antialiased(ivec2 p) {
    vec3 centre = colourAt(p);
    float m = lumaOf(centre);
    float n = lumaAt(p + ivec2(0, -1)), s = lumaAt(p + ivec2(0, 1));
    float e = lumaAt(p + ivec2(1, 0)), w = lumaAt(p + ivec2(-1, 0));
    float most = max(m, max(max(n, s), max(e, w)));
    float least = min(m, min(min(n, s), min(e, w)));
    float range = most - least;
    if (range < max(EDGE_THRESHOLD_MIN, most * EDGE_THRESHOLD)) return centre;

    float nw = lumaAt(p + ivec2(-1, -1)), ne = lumaAt(p + ivec2(1, -1));
    float sw = lumaAt(p + ivec2(-1, 1)), se = lumaAt(p + ivec2(1, 1));

    // A horizontal edge changes most from row to row.
    float acrossRows = 2.0 * abs(n + s - 2.0 * m) + abs(ne + se - 2.0 * e) + abs(nw + sw - 2.0 * w);
    float acrossColumns = 2.0 * abs(e + w - 2.0 * m) + abs(ne + nw - 2.0 * n) + abs(se + sw - 2.0 * s);
    bool horizontal = acrossRows >= acrossColumns;
    ivec2 along = horizontal ? ivec2(1, 0) : ivec2(0, 1);

    // The edge lies toward the steeper of the two neighbours across it.
    float before = horizontal ? n : w, after = horizontal ? s : e;
    bool toBefore = abs(before - m) >= abs(after - m);
    ivec2 across = (horizontal ? ivec2(0, 1) : ivec2(1, 0)) * (toBefore ? -1 : 1);
    float side = toBefore ? before : after;
    float gradient = 0.25 * max(abs(before - m), abs(after - m));
    float edgeLuma = 0.5 * (m + side);

    // Walk the edge both ways, reading the pair of texels either side of it,
    // until the pair's mean leaves the edge's.
    int toNeg = 0, toPos = 0;
    float endNeg = 0.0, endPos = 0.0;
    bool doneNeg = false, donePos = false;
    for (int i = 0; i < SEARCH_STEPS && !(doneNeg && donePos); ++i) {
        if (!doneNeg) {
            toNeg += SEARCH_STEP[i];
            ivec2 q = p - along * toNeg;
            endNeg = 0.5 * (lumaAt(q) + lumaAt(q + across)) - edgeLuma;
            doneNeg = abs(endNeg) >= gradient;
        }
        if (!donePos) {
            toPos += SEARCH_STEP[i];
            ivec2 q = p + along * toPos;
            endPos = 0.5 * (lumaAt(q) + lumaAt(q + across)) - edgeLuma;
            donePos = abs(endPos) >= gradient;
        }
    }

    // Blend only where the nearer end turns the way this pixel does: past it
    // the edge belongs to the other side.
    bool negNearer = toNeg < toPos;
    float end = negNearer ? endNeg : endPos;
    bool centreDarker = m < edgeLuma;
    float edgeBlend = ((end < 0.0) != centreDarker) ? 0.5 - float(min(toNeg, toPos)) / float(toNeg + toPos) : 0.0;

    // A lone pixel unlike all eight neighbours is blended as well.
    float mean = (2.0 * (n + s + e + w) + nw + ne + sw + se) / 12.0;
    float lone = smoothstep(0.0, 1.0, clamp(abs(mean - m) / range, 0.0, 1.0));
    float subpixelBlend = lone * lone * SUBPIXEL;

    return mix(centre, colourAt(p + across), max(edgeBlend, subpixelBlend));
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    // Past the drawn region the input repeats the region's edge, for EASU's
    // taps; that copy is kept as it is.
    vec3 colour = all(lessThan(p, ivec2(post.regionSize))) ? antialiased(p) : colourAt(p);
    outColour = vec4(post.upscaleInput != 0u ? colour : colour * colour, 1.0);
}
