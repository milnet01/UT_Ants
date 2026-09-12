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
layout(set = 0, binding = 7) readonly buffer ProbeGridBlock { int probeGrid[]; };
layout(set = 0, binding = 8) readonly buffer ProbeBlock { Probe probes[]; };
layout(set = 0, binding = 9) readonly buffer ShadowFaceBlock { ShadowFace shadowFaces[]; };
layout(set = 0, binding = 10) uniform sampler2DShadow shadowAtlas;
layout(set = 0, binding = 11) uniform sampler2D textures[];

layout(push_constant) uniform DrawBlock {
    uint objectIndex;
    uint materialIndex;
    uint polyFlags;
} draw;

#endif
