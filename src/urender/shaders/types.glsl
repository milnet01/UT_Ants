// The structs the renderer's shaders read -- ShaderTypes.h declares the same
// ones and asserts every offset (UTA-0014 INV-9). A change here is a change
// there, in the same commit.

#ifndef UTA_TYPES_GLSL
#define UTA_TYPES_GLSL

const uint NONE = 0xFFFFFFFFu;

// UT99's EPolyFlags -- UTA-0014 SS 4.5.
const uint PF_MASKED = 0x00000002u;
const uint PF_TRANSLUCENT = 0x00000004u;
const uint PF_MODULATED = 0x00000040u; // UTA-0271
const uint PF_FAKE_BACKDROP = 0x00000080u;
const uint PF_TWO_SIDED = 0x00000100u;
const uint PF_UNLIT = 0x00400000u;

// UTA-0014 SS 4.5: a masked texel's alpha is binary at the source, so any
// threshold strictly inside the range classifies every authored texel alike;
// 0.5 splits the filtered edge evenly.
const float MASK_THRESHOLD = 0.5;

// UTA-0014 SS 4.6: at most this many lights a cluster, and the bit a cluster's
// count carries when it had to drop some. ShaderTypes.h states the same two.
const uint CLUSTER_CAPACITY = 64u;
const uint CLUSTER_OVERFLOW = 0x80000000u;

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
    uint probeTableMask;
    uint probeLongestRun;
    uint skyTexture;   // UTA-0163: NONE when the level has no sky, or it is being drawn
    uint shadowFaceCount;
    uint skyFirstFace; // UTA-0163
    uint occlusionTexture; // UTA-0164: NONE when the bundle has no AOCC or the tier draws none
    float flameSeconds; // UTA-0263 SS 4.4: the light clock, wrapped
    uint cameraZone;    // UTA-0089 SS 4.1
    uint cameraUnderwater; // UTA-0215 SS 4.3: nonzero where the camera's zone is water
    float waterFogR;       // UTA-0215 SS 4.3: what far things fade to under water, as light
    float waterFogG;
    float waterFogB;
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
    uint parallaxDepth; // UTA-0040: texels of the base level; 0 for none
    uint flame;         // UTA-0263: its ramp's first entry in flameRamps, or NONE
    uint liquid;        // UTA-0105: its look's index in liquids, or NONE
};

// UTA-0105 SS 4.4: ShaderTypes.h's Liquid. UT99's settings, as floats.
struct Liquid {
    uint kind;     // 1 Wet, 2 Ice, 3 Wave
    uint panning;  // Ice: PanningStyle
    float amplitude;
    float frequency;
    vec2 pan;      // Ice: HorizPanSpeed, VertPanSpeed
    vec2 size;     // texels
    vec4 bump;     // Wave: BumpMapLight, BumpMapAngle, PhongSize
    vec4 ramp[8];  // Wave: linear RGB, darkest first
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
    uint volumeRadius;     // UTA-0015: UT99's VolumeRadius; 0 for none
    uint volumeBrightness; // UTA-0015: UT99's VolumeBrightness
    vec3 span; // UTA-0162: a strip leader's segment, from `location`; zero otherwise
    uint volumeFog;        // UTA-0015: UT99's VolumeFog
    float levelBrightness; // UTA-0156 SS 4.5: LevelInfo.Brightness
    uint pad0;             // std430 rounds the struct to its vec3's 16
    uint pad1;
    uint pad2;
};

struct ClusterBounds {
    vec4 minimum;
    vec4 maximum;
};

struct Probe {
    vec4 faces[6];
};

struct ProbeCell {
    ivec3 cell;
    int probe;
};

struct ShadowFace {
    mat4 viewProj;
    vec4 atlasRect;
};

// UTA-0156 SS 4.4: one zone's ambient light, as UT99's bytes.
struct Zone {
    uint brightness;
    uint hue;
    uint saturation;
    uint sky; // UTA-0089 SS 4.1: 1 where the zone holds a sky window
    uint water; // UTA-0215: 1 where the zone is water, for its caustics
};

// UTA-0263 SS 4.4: one camera-facing flame.
struct FlameInstance {
    vec3 base;
    float width;
    float height;
    uint seed;
    uint ramp;
    uint reserved;
};

#endif
