// UTA-0350 SS 4.4: the painted sky a map only paints on its walls, opened.
//
// paintedSkyGradient and paintedSkyFade are ubundle::paintedSkyLight's
// (ubundle/Sky.cpp), which the probe bake reads; the device tier holds them
// equal (tests/device/shaders/painted_sky.comp). The cloud pattern is
// scene.frag's alone, beside the noise it reads; the bake takes its mean.
//
// NOTHING HERE READS A BINDING, so a test kernel can include it whole.

#ifndef UTA_SKY_GLSL
#define UTA_SKY_GLSL

// ubundle::PAINTED_SKY_CLOUD, over ubundle::PAINTED_SKY_EXPOSURE.
const float PAINTED_SKY_CLOUD = 0.85 / 5.03;

// UTA-0389: below the horizon a calm sea, so the opened box does not end in a
// pale wall: the sky above, mirrored by Schlick's share as water.glsl reflects
// it, over the sea's own colour: the sky's top mixed this far toward its
// horizon, which lands near a map's bright water picture. ubundle's
// PAINTED_SEA_R0 and PAINTED_SEA_MIX.
const float PAINTED_SEA_R0 = 0.02;
const float PAINTED_SEA_MIX = 0.5;

vec3 paintedSkyGradient(vec3 top, vec3 horizon, vec3 d) {
    vec3 sky = mix(horizon, top, sqrt(clamp(abs(d.z), 0.0, 1.0)));
    if (d.z >= 0.0) return sky;
    float c = 1.0 + d.z; // 1 - cos, the sea's normal being straight up
    float mirrored = PAINTED_SEA_R0 + (1.0 - PAINTED_SEA_R0) * (c * c * c * c * c);
    return mix(mix(top, horizon, PAINTED_SEA_MIX), sky, mirrored);
}

// The clouds thin out toward the horizon.
float paintedSkyFade(vec3 d) {
    return smoothstep(0.02, 0.25, d.z);
}

// The light paintedSkyLight gives: the clouds at their mean cover.
vec3 paintedSkyMean(vec3 top, vec3 horizon, float clouds, vec3 d) {
    vec3 gradient = paintedSkyGradient(top, horizon, d);
    return mix(gradient, vec3(PAINTED_SKY_CLOUD), clouds * paintedSkyFade(d));
}

#endif
