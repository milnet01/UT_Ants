// Indirect light from a level's probes -- UTA-0014 SS 4.7, evaluating
// UTA-0112 SS 4.9's ambient cube.
//
// The renderer's ONLY copy of this arithmetic (SS 3 decision 5, applied to the
// probes as to the lights). The shading pass includes this file, and so does
// the compute shader UTA-0014 INV-7 dispatches -- which is why the evaluation
// is a device test rather than a C++ one.
//
// PROBES ARE FOUND THROUGH A HASH TABLE, NOT A DENSE GRID. UTA-0112 seeds them
// near geometry at 128-unit spacing, so a grid over their bounding box grows
// with the level's box -- to hundreds of megabytes on a map-sized one -- while
// a table grows with the probes. Probes.cpp builds it; probeHash below is the
// one function both sides must agree on, and INV-7 is what crosses them.
//
// THE INCLUDER DECLARES THE BUFFERS: `probeCells`, the table, and `probes`.
// The scene set declares them in scene_bindings.glsl; INV-7's kernel declares
// its own under the same names.

#ifndef UTA_PROBES_GLSL
#define UTA_PROBES_GLSL

#include "types.glsl"

// What the table is: its spacing, how many probes, the table's size minus one,
// and the longest run a lookup can need.
struct ProbeLattice {
    uint spacing;
    uint count;
    uint tableMask;
    uint longestRun;
};

// Probes.cpp's probeHash, and it must stay identical to it.
uint probeHash(ivec3 cell) {
    return (uint(cell.x) * 73856093u) ^ (uint(cell.y) * 19349663u) ^ (uint(cell.z) * 83492791u);
}

// The probe at `cell`, or -1. Linear probing: an empty slot ends the search.
int probeAt(ProbeLattice lattice, ivec3 cell) {
    uint slot = probeHash(cell) & lattice.tableMask;
    for (uint step = 0u; step <= lattice.longestRun; ++step) {
        ProbeCell entry = probeCells[(slot + step) & lattice.tableMask];
        if (entry.probe < 0) return -1;
        if (entry.cell == cell) return entry.probe;
    }
    return -1;
}

// UTA-0112 SS 4.9: each axis's squared component weights the face that axis's
// sign selects. The faces are +X, -X, +Y, -Y, +Z, -Z.
vec3 ambientCube(Probe probe, vec3 n) {
    vec3 n2 = n * n;
    return n2.x * probe.faces[n.x >= 0.0 ? 0 : 1].rgb
         + n2.y * probe.faces[n.y >= 0.0 ? 2 : 3].rgb
         + n2.z * probe.faces[n.z >= 0.0 ? 4 : 5].rgb;
}

// SS 4.7's two decisions. Trilinear over the eight probes of the lattice cell
// holding `x`, each weighted by its corner's share; a missing corner
// contributes nothing and its weight is shared among the corners present. With
// no corner present the point gets zero -- not the nearest probe's light,
// which outside the lattice is usually light leaking through a wall.
vec3 indirectAt(ProbeLattice lattice, vec3 x, vec3 n) {
    if (lattice.count == 0u || lattice.spacing == 0u) return vec3(0.0);
    vec3 cell = x / float(lattice.spacing);
    vec3 base = floor(cell);
    vec3 f = cell - base;

    vec3 sum = vec3(0.0);
    float weights = 0.0;
    for (int corner = 0; corner < 8; ++corner) {
        ivec3 step = ivec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
        int probe = probeAt(lattice, ivec3(base) + step);
        if (probe < 0) continue;
        vec3 share = mix(1.0 - f, f, vec3(step));
        float weight = share.x * share.y * share.z;
        sum += weight * ambientCube(probes[probe], n);
        weights += weight;
    }
    return weights > 0.0 ? sum / weights : vec3(0.0);
}

#endif
