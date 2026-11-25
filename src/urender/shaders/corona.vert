#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

// UTA-0321: a light's corona, the glow UT99 draws about a bCorona light. One
// instance a light, six vertices. It keeps one size on the screen however far
// the light is, and grows with the light's DrawScale.
//
// UTA-0511: drawn as lens glare, not as the original's. The quad is not
// depth-tested: the whole glow dims by the share of a small disc about the
// light that the scene's depth leaves visible, so a rail across the lamp dims
// it rather than cutting it with a hard line. Its light follows the lamp's
// own brightness. It stays whole up close, fading only as the eye reaches the
// lamp.
//
// Size and gain fitted to UT_MonsterHunt's AS-Frigate captures of Light40
// (DrawScale 0.1, 1280x720): the glow reaches the wall's own brightness 50
// pixels from the lamp at 747 and at 560 units (measured 2026-11-24).

#include "scene_bindings.glsl"
#include "light.glsl"

// The scene's depth, 0 near and 1 far: the refract set's binding, attached
// read-only while the coronas draw.
layout(set = 1, binding = 1) uniform sampler2D sceneDepth;

layout(location = 0) in vec3 coronaLocation;
layout(location = 1) in float coronaScale;
layout(location = 2) in uint coronaHue;
layout(location = 3) in uint coronaSaturation;
layout(location = 4) in uint coronaBrightness;
layout(location = 5) in float coronaStrength;

layout(location = 0) out vec2 coronaUv; // -1 to 1 across the glow
layout(location = 1) flat out vec3 coronaColour;

// The glow's radius, as a share of the view's height, for each unit of
// DrawScale: 50 of 720 pixels at 0.1.
const float CORONA_RADIUS_PER_SCALE = 0.69;
// The largest radius drawn, as a share of the view's height. Not measured: a
// light the original draws larger than this is drawn this size.
const float CORONA_MOST_RADIUS = 0.25;
// UT units the glow is drawn toward the eye from the light, past its fitting.
const float CORONA_PULL = 16.0;
// UT units from the eye over which the glow fades in, so it does not vanish at
// once as the eye passes through the lamp.
const float CORONA_FADE_NEAR = 0.0;
const float CORONA_FADE_FULL = 32.0;
// The disc tested for how much of the lamp shows: its radius as a share of
// the glow's, and never under this many pixels.
const float OCCLUSION_RADIUS_SHARE = 0.25;
const float OCCLUSION_LEAST_PIXELS = 1.5;
const int OCCLUSION_SAMPLES = 16;
// The glow's light at its centre, as scene light: a multiple of the light's
// colour, which the output stage's exposure scales as it does every surface.
// Fitted to Light40 (white, LightBrightness 115) over our own frame without
// the glow: the original's rise above its wall, added to ours.
const float CORONA_GAIN = 0.83;

// The share of OCCLUSION_SAMPLES points over a disc of `radiusPixels` about
// `pixel` where nothing nearer than `depth` stands. A point off the view
// counts as hidden, so a lamp leaving the view fades out.
float visibleShare(vec2 pixel, float radiusPixels, float depth) {
    ivec2 size = textureSize(sceneDepth, 0);
    vec2 drawn = frame.viewportSize;
    int open = 0;
    for (int i = 0; i < OCCLUSION_SAMPLES; ++i) {
        // A Vogel spiral: even cover of the disc from few points.
        float r = sqrt((float(i) + 0.5) / float(OCCLUSION_SAMPLES)) * radiusPixels;
        float a = float(i) * 2.39996323;
        vec2 at = pixel + r * vec2(cos(a), sin(a));
        if (any(lessThan(at, vec2(0.0))) || any(greaterThanEqual(at, drawn))) continue;
        ivec2 texel = min(ivec2(at), size - 1);
        if (texelFetch(sceneDepth, texel, 0).r >= depth) ++open;
    }
    return float(open) / float(OCCLUSION_SAMPLES);
}

const vec2 CORNERS[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));

void main() {
    vec3 toEye = frame.eye - coronaLocation;
    float distance = length(toEye);
    vec3 at = coronaLocation + toEye * (min(CORONA_PULL, 0.5 * distance) / max(distance, 1e-3));
    vec4 centre = frame.viewProj * vec4(at, 1.0);
    if (centre.w <= 0.0) { // behind the eye: every corner on one point outside the view
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        coronaUv = vec2(0.0);
        coronaColour = vec3(0.0);
        return;
    }

    float radius = min(coronaScale * CORONA_RADIUS_PER_SCALE, CORONA_MOST_RADIUS) * 2.0; // NDC spans 2
    vec2 corner = CORNERS[gl_VertexIndex];
    vec2 offset = corner * vec2(radius * frame.viewportSize.y / frame.viewportSize.x, radius);
    gl_Position = centre + vec4(offset * centre.w, 0.0, 0.0);

    vec2 pixel = (centre.xy / centre.w * 0.5 + 0.5) * frame.viewportSize;
    float radiusPixels = 0.5 * radius * frame.viewportSize.y; // radius spans NDC's 2
    float shown = visibleShare(pixel, max(radiusPixels * OCCLUSION_RADIUS_SHARE, OCCLUSION_LEAST_PIXELS),
                               centre.z / centre.w);
    float fade = smoothstep(CORONA_FADE_NEAR, CORONA_FADE_FULL, distance) * shown;
    coronaUv = corner;
    coronaColour = lightColour(coronaHue, coronaSaturation)
                   * (lightIntensity(coronaBrightness) * coronaStrength * fade * CORONA_GAIN);
}
