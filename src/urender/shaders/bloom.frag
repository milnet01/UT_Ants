#version 450

// UTA-0053's emissive bloom: one step of the chain, as LearnOpenGL's physically
// based bloom writes it (learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom),
// after Jimenez's SIGGRAPH 2014 "Next Generation Post Processing in Call of
// Duty: Advanced Warfare". A downsample is its 13-tap filter, the first one
// weighted by a Karis average so one bright texel does not sparkle; an upsample
// is its 3x3 tent, added into the level above by the pipeline's blend.

layout(set = 0, binding = 0) uniform sampler2D source;

layout(push_constant) uniform BloomBlock {
    vec2 sourceTexel;
    vec2 targetTexel;
    vec2 sourceUvMax; // UTA-0051 SS 4.4: past the drawn region nothing is read
    float radius;     // the upsample tent's radius, in UV
    uint mode;
} bloom;

layout(location = 0) out vec4 outColour;

// ShaderTypes.h's BLOOM_* values.
const uint BLOOM_DOWNSAMPLE_FIRST = 0u;
const uint BLOOM_UPSAMPLE = 2u;

vec3 tap(vec2 uv) {
    return texture(source, min(uv, bloom.sourceUvMax)).rgb;
}

float karisWeight(vec3 colour) {
    float luma = dot(colour, vec3(0.2126, 0.7152, 0.0722)) * 0.25;
    return 1.0 / (1.0 + luma);
}

void main() {
    vec2 uv = gl_FragCoord.xy * bloom.targetTexel;

    if (bloom.mode == BLOOM_UPSAMPLE) {
        float x = bloom.radius, y = bloom.radius;
        vec3 a = tap(uv + vec2(-x, y)), b = tap(uv + vec2(0.0, y)), c = tap(uv + vec2(x, y));
        vec3 d = tap(uv + vec2(-x, 0.0)), e = tap(uv), f = tap(uv + vec2(x, 0.0));
        vec3 g = tap(uv + vec2(-x, -y)), h = tap(uv + vec2(0.0, -y)), i = tap(uv + vec2(x, -y));
        vec3 up = e * 4.0 + (b + d + f + h) * 2.0 + (a + c + g + i);
        outColour = vec4(up / 16.0, 1.0);
        return;
    }

    float x = bloom.sourceTexel.x, y = bloom.sourceTexel.y;
    vec3 a = tap(uv + vec2(-2.0 * x, 2.0 * y)), b = tap(uv + vec2(0.0, 2.0 * y)), c = tap(uv + vec2(2.0 * x, 2.0 * y));
    vec3 d = tap(uv + vec2(-2.0 * x, 0.0)), e = tap(uv), f = tap(uv + vec2(2.0 * x, 0.0));
    vec3 g = tap(uv + vec2(-2.0 * x, -2.0 * y)), h = tap(uv + vec2(0.0, -2.0 * y)), i = tap(uv + vec2(2.0 * x, -2.0 * y));
    vec3 j = tap(uv + vec2(-x, y)), k = tap(uv + vec2(x, y));
    vec3 l = tap(uv + vec2(-x, -y)), m = tap(uv + vec2(x, -y));

    vec3 down;
    if (bloom.mode == BLOOM_DOWNSAMPLE_FIRST) {
        vec3 group0 = (a + b + d + e) * (0.125 / 4.0);
        vec3 group1 = (b + c + e + f) * (0.125 / 4.0);
        vec3 group2 = (d + e + g + h) * (0.125 / 4.0);
        vec3 group3 = (e + f + h + i) * (0.125 / 4.0);
        vec3 group4 = (j + k + l + m) * (0.5 / 4.0);
        down = group0 * karisWeight(group0) + group1 * karisWeight(group1) + group2 * karisWeight(group2)
             + group3 * karisWeight(group3) + group4 * karisWeight(group4);
    } else {
        down = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    }
    outColour = vec4(max(down, vec3(0.0)), 1.0);
}
