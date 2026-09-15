// Which cluster a point is in -- UTA-0014 SS 4.6. Moved out of scene.frag by
// UTA-0015, whose fog pass finds a froxel's lights the same way.
//
// THE INCLUDER DECLARES `frame`; scene_bindings.glsl does.

#ifndef UTA_CLUSTERS_GLSL
#define UTA_CLUSTERS_GLSL

// The same slicing Clusters.cpp builds the boxes with: 16 tiles across, 8 down,
// and 24 depth slices exponential in z. `pixel` is in the drawn region.
uint clusterOf(vec2 pixel, float viewZ) {
    uvec3 grid = frame.clusterGrid;
    uint x = min(uint(pixel.x * float(grid.x) / frame.viewportSize.x), grid.x - 1u);
    uint y = min(uint(pixel.y * float(grid.y) / frame.viewportSize.y), grid.y - 1u);
    float slice = floor(log(max(viewZ, frame.nearPlane)) * frame.clusterDepthScale - frame.clusterDepthBias);
    uint z = uint(clamp(slice, 0.0, float(grid.z - 1u)));
    return x + y * grid.x + z * grid.x * grid.y;
}

#endif
