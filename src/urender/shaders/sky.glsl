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

// ubundle::PAINTED_SKY_CLOUD.
const float PAINTED_SKY_CLOUD = 0.85;

vec3 paintedSkyGradient(vec3 top, vec3 horizon, vec3 d) {
    return mix(horizon, top, sqrt(clamp(d.z, 0.0, 1.0)));
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
