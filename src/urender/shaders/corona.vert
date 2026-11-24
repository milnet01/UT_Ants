#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

// UTA-0321: a light's corona, the glow UT99 draws about a bCorona light. One
// instance a light, six vertices. As the original's does, it keeps one size on
// the screen however far the light is, grows with the light's DrawScale, and is
// hidden by what stands in front of the light: the quad is depth-tested at the
// light, drawn a little toward the eye so the lamp's own fitting does not hide
// it. Close up it fades, as the original's does.
//
// Fitted to UT_MonsterHunt's AS-Frigate captures of Light40 (DrawScale 0.1,
// 1280x720): the glow reaches the wall's own brightness 50 pixels from the
// lamp at 747 and at 560 units, and at 373 rises about two thirds as far
// above it (measured 2026-11-24).

#include "scene_bindings.glsl"
#include "light.glsl"

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
// UT units from the eye where the glow starts to show, and where it is whole:
// whole at 560, about two thirds at 373. Nearer than 373 is not measured.
const float CORONA_FADE_NEAR = 100.0;
const float CORONA_FADE_FULL = 560.0;
// The glow's light at its centre, as scene light: a multiple of the light's
// colour, which the output stage's exposure scales as it does every surface.
// Fitted to Light40 (white, LightBrightness 115) over our own frame without
// the glow: the original's rise above its wall, added to ours.
const float CORONA_GAIN = 0.83;

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

    float fade = smoothstep(CORONA_FADE_NEAR, CORONA_FADE_FULL, distance);
    coronaUv = corner;
    coronaColour = lightColour(coronaHue, coronaSaturation)
                   * (lightIntensity(coronaBrightness) * coronaStrength * fade * CORONA_GAIN);
}
