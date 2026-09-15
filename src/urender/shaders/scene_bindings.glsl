// The scene descriptor set -- ShaderTypes.h's Binding enum numbers it the same.
// A shader including this must first enable GL_EXT_nonuniform_qualifier.

#ifndef UTA_SCENE_BINDINGS_GLSL
#define UTA_SCENE_BINDINGS_GLSL

#include "types.glsl"

layout(set = 0, binding = 0) readonly buffer FrameBlock { FrameData frame; };
layout(set = 0, binding = 1) readonly buffer ObjectBlock { Object objects[]; };
layout(set = 0, binding = 2) readonly buffer MaterialBlock { Material materials[]; };
layout(set = 0, binding = 3) readonly buffer LightBlock { Light lights[]; };
// The cluster pass writes these two and declares them itself.
#ifndef UTA_CLUSTER_WRITER
layout(set = 0, binding = 4) readonly buffer ClusterCountBlock { uint clusterCounts[]; };
layout(set = 0, binding = 5) readonly buffer ClusterIndexBlock { uint clusterIndices[]; };
#endif
layout(set = 0, binding = 6) readonly buffer ClusterBoundsBlock { ClusterBounds clusterBounds[]; };
layout(set = 0, binding = 7) readonly buffer ProbeCellBlock { ProbeCell probeCells[]; };
layout(set = 0, binding = 8) readonly buffer ProbeBlock { Probe probes[]; };
layout(set = 0, binding = 9) readonly buffer ShadowFaceBlock { ShadowFace shadowFaces[]; };
layout(set = 0, binding = 10) readonly buffer ZoneBlock { Zone zones[]; }; // UTA-0156 SS 4.4
layout(set = 0, binding = 11) uniform sampler2DShadow shadowAtlas;
layout(set = 0, binding = 12) uniform sampler3D fogVolume; // UTA-0015 SS 4.3
layout(set = 0, binding = 13) uniform sampler2D textures[];

#if defined(UTA_SHADOW_PASS)
// ShaderTypes.h's ShadowConstants: a shadow tile's face, then the batch.
layout(push_constant) uniform ShadowBlock {
    mat4 viewProj;
    uint objectIndex;
    uint materialIndex;
    uint polyFlags;
    uint reserved;
} shadowDraw;
#elif defined(UTA_FOG_PASS)
// ShaderTypes.h's FogConstants -- UTA-0015 SS 4.4.
layout(push_constant) uniform FogBlock {
    mat4 viewToWorld;
    vec2 tanHalfFov;
    uint volumeLightCount;
    uint flashlight;
    float hazeScale;
    float reserved[3];
} fog;
#else
layout(push_constant) uniform DrawBlock {
    uint objectIndex;
    uint materialIndex;
    uint polyFlags;
} draw;
#endif

#endif
