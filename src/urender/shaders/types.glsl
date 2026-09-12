// The structs the renderer's shaders read -- ShaderTypes.h declares the same
// ones and asserts every offset (UTA-0014 INV-9). A change here is a change
// there, in the same commit.

#ifndef UTA_TYPES_GLSL
#define UTA_TYPES_GLSL

const uint NONE = 0xFFFFFFFFu;

// UT99's EPolyFlags -- UTA-0014 SS 4.5.
const uint PF_MASKED = 0x00000002u;
const uint PF_TRANSLUCENT = 0x00000004u;
const uint PF_FAKE_BACKDROP = 0x00000080u;
const uint PF_TWO_SIDED = 0x00000100u;
const uint PF_UNLIT = 0x00400000u;

// UTA-0014 SS 4.5: a masked texel's alpha is binary at the source, so any
// threshold strictly inside the range classifies every authored texel alike;
// 0.5 splits the filtered edge evenly.
const float MASK_THRESHOLD = 0.5;

struct FrameData {
    mat4 viewProj;
    mat4 viewProjUnjittered;
    mat4 previousViewProjUnjittered;
    mat4 view;
    vec3 eye;
    float exposure;
    uvec3 clusterGrid;
    uint lightCount;
    float clusterDepthScale;
    float clusterDepthBias;
    float nearPlane;
    float farPlane;
    vec2 viewportSize;
    uint probeSpacing;
    uint probeCount;
    ivec3 probeOrigin;
    uint shadowFaceCount;
    ivec3 probeDims;
    uint reserved;
};

struct Object {
    mat4 model;
    mat4 previousModel;
    mat4 normalMatrix;
};

struct Material {
    uint base;
    uint normal;
    uint rough;
    uint height;
    uint emit;
    uint metallic;
};

struct Light {
    vec3 location;
    float flicker;
    uint hue;
    uint saturation;
    uint brightness;
    uint radius;
    uint effect;
    uint cone;
    int pitch;
    int yaw;
    int shadowFace;
    uint shadowFaceCount;
    uint reserved0;
    uint reserved1;
};

struct ClusterBounds {
    vec4 minimum;
    vec4 maximum;
};

struct Probe {
    vec4 faces[6];
};

struct ShadowFace {
    mat4 viewProj;
    vec4 atlasRect;
};

#endif
