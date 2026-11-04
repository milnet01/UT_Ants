#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

// The blades of grass -- docs/specs/UTA-0293-grass.md SS 4.4 and SS 4.5.
// One instance per VEGE clump, no vertex buffer: gl_InstanceIndex names the
// clump (the draw's first instance is its cell's first clump), and
// gl_VertexIndex the blade and the corner. Each blade is a tapered strip of 7
// vertices, drawn as its 5 triangles' 15 corners; urender/Grass.h states the
// same count. Every choice a blade makes is a hash of the clump's seed.

#define UTA_GRASS_PASS
#include "scene_bindings.glsl"

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 bladeNormal;
layout(location = 2) out float heightShare; // 0 at the root, 1 at the tip
layout(location = 3) out vec4 currentClip;
layout(location = 4) out vec4 previousClip;
layout(location = 5) flat out uint clumpIndex;

const uint VERTICES_PER_BLADE = 15u;
// The blades stand within CLUMP_RADIUS of the clump's foot, BLADE_LOW to
// BLADE_HIGH tall, and lean and bend at most LEAN + GUST_BEND + FLUTTER_BEND
// of their height at the tip: 6 + 0.66 x 20 = 19.2 units at the most, inside
// ubake's BLADE_REACH of 20, which a cell's bounds allow every way.
const float CLUMP_RADIUS = 6.0;
const float BLADE_LOW = 8.0;
const float BLADE_HIGH = 20.0;
const float BLADE_WIDTH = 1.8;
const float LEAN = 0.35;
// SS 4.5's wind: one direction for every map, a travelling gust, and each
// blade's own flutter. First values, SS 13.
const vec2 WIND_DIRECTION = vec2(0.8, 0.6);
const float GUST_WAVE = 0.004;  // radians a unit along the wind: gusts some 1500 units apart
const float GUST_SPEED = 1.3;   // radians a second
const float GUST_BEND = 0.25;   // of the height, at the tip
const float FLUTTER_SPEED = 3.1;
const float FLUTTER_BEND = 0.06;

// lowbias32: a well-mixed 32-bit hash.
uint hashOf(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float unitOf(uint h) {
    return float(h >> 8) * (1.0 / 16777216.0);
}

// A blade's corner `strip` (0 to 6) at light time `time`, shrunk by `shrink`.
// Even corners on the left, odd on the right, rising in pairs to the tip.
vec3 bladePoint(GrassClump clump, uint blade, uint strip, float time, float shrink, out vec3 normal,
                out float share) {
    uint h = hashOf(clump.seed ^ (blade * 0x9e3779b9u));
    float angle = unitOf(h) * 6.2831853;
    float radius = sqrt(unitOf(hashOf(h + 1u))) * CLUMP_RADIUS;
    float height = mix(BLADE_LOW, BLADE_HIGH, unitOf(hashOf(h + 2u))) * shrink;
    float facing = unitOf(hashOf(h + 3u)) * 6.2831853;
    float leanAngle = unitOf(hashOf(h + 4u)) * 6.2831853;
    float phase = unitOf(hashOf(h + 5u)) * 6.2831853;

    vec3 root = clump.base + vec3(cos(angle), sin(angle), 0.0) * radius;
    vec2 wind = normalize(WIND_DIRECTION);
    float gust = sin(dot(clump.base.xy, wind) * GUST_WAVE - time * GUST_SPEED);
    float flutter = sin(time * FLUTTER_SPEED + phase);
    // The tip's sideways reach, as a share of the height.
    vec2 reach = vec2(cos(leanAngle), sin(leanAngle)) * LEAN + wind * (GUST_BEND * gust) + wind * (FLUTTER_BEND * flutter);

    uint level = strip / 2u;
    share = float(level) / 3.0;
    float side = strip == 6u ? 0.0 : float(strip % 2u) - 0.5;
    vec3 across = vec3(cos(facing), sin(facing), 0.0);
    // The bend grows with the square of the height up the blade.
    vec3 centre = root + vec3(reach * (share * share), share) * height;
    vec3 tangent = vec3(reach * (2.0 * share), 1.0);
    normal = normalize(cross(across, tangent));
    return centre + across * (side * BLADE_WIDTH * shrink * (1.0 - share));
}

void main() {
    clumpIndex = uint(gl_InstanceIndex);
    GrassClump clump = grassClumps[clumpIndex];
    uint blade = uint(gl_VertexIndex) / VERTICES_PER_BLADE;
    uint corner = uint(gl_VertexIndex) % VERTICES_PER_BLADE;
    // Triangle t of the strip is its corners t, t + 1 and t + 2.
    uint strip = corner / 3u + corner % 3u;

    // SS 4.4: from GRASS_FADE to GRASS_END the blades shrink to nothing.
    float shrink = 1.0 - smoothstep(grassDraw.fadeStart, grassDraw.end, distance(frame.eye, clump.base));

    vec3 normal, unusedNormal;
    float share, unusedShare;
    vec3 world = bladePoint(clump, blade, strip, frame.flameSeconds, shrink, normal, share);
    vec3 previous = bladePoint(clump, blade, strip, frame.previousFlameSeconds, shrink, unusedNormal, unusedShare);

    worldPosition = world;
    bladeNormal = normal;
    heightShare = share;
    // UTA-0014 SS 4.11 provision 2: motion vectors from the current and
    // previous clip positions, the previous one at the previous light time.
    currentClip = frame.viewProjUnjittered * vec4(world, 1.0);
    previousClip = frame.previousViewProjUnjittered * vec4(previous, 1.0);
    gl_Position = frame.viewProj * vec4(world, 1.0);
}
