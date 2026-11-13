#version 450

// UTA-0019 SS 4.8's stand-in shapes. One instance per shape, its record an
// instance-rate vertex (ShaderTypes.h's ShapeInstance), and no vertex buffer
// beside it: each kind's mesh is made here from the vertex index. Every
// triangle winds counter-clockwise about its outward normal, so one front
// face culls the far side of every kind.
//
// Vertex counts are ShaderTypes.h's SHAPE_VERTICES, which the draw uses.

layout(location = 0) in vec3 inA;
layout(location = 1) in uint inKind;
layout(location = 2) in vec3 inB;
layout(location = 3) in float inRadius;
layout(location = 4) in vec4 inColour;
layout(location = 5) in float inHalfHeight;

layout(push_constant) uniform ShapeBlock {
    mat4 viewProj;
    vec3 eye;
    float pixelAngle;        // a pixel's world size one unit from the eye
    float minimumHalfPixels; // a line's least half-thickness, in pixels
} shape;

layout(location = 0) flat out vec4 colour;

const uint SPHERE = 0u;
const uint LINE = 1u;
const float PI = 3.14159265358979;

const uint SLICES = 16u; // around; the sphere's and the cylinder's
const uint RINGS = 8u;   // the sphere's, pole to pole

// A quad's six corners, (along, up): two triangles, each counter-clockwise
// seen from where along x up points.
const uvec2 QUAD[6] = uvec2[](uvec2(0, 0), uvec2(1, 0), uvec2(1, 1), uvec2(0, 0), uvec2(1, 1), uvec2(0, 1));

vec2 around(uint step) {
    float angle = 2.0 * PI * float(step % SLICES) / float(SLICES);
    return vec2(cos(angle), sin(angle));
}

vec3 sphereVertex(uint index) {
    uint cell = index / 6u;
    uvec2 corner = QUAD[index % 6u];
    vec2 ring = around(cell % SLICES + corner.x);
    float latitude = PI * float(cell / SLICES + corner.y) / float(RINGS); // 0 at the bottom
    float across = sin(latitude);
    return inA + inRadius * vec3(across * ring, -cos(latitude));
}

vec3 cylinderVertex(uint index) {
    uint side = index / 12u;
    uint corner = index % 12u;
    if (corner < 6u) { // the wall
        uvec2 at = QUAD[corner];
        return inA + vec3(inRadius * around(side + at.x), (2.0 * float(at.y) - 1.0) * inHalfHeight);
    }
    // The caps: the top one about +Z, the bottom one about -Z.
    corner -= 6u;
    bool top = corner < 3u;
    uint point = corner % 3u;
    if (point == 0u) return inA + vec3(0.0, 0.0, top ? inHalfHeight : -inHalfHeight);
    uint step = top == (point == 1u) ? side : side + 1u;
    return inA + vec3(inRadius * around(step), top ? inHalfHeight : -inHalfHeight);
}

// A unit cube's faces, each its axis and two tangents whose cross is that axis.
const vec3 FACE_AXIS[6] = vec3[](vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, -1, 0), vec3(0, 0, 1),
                                 vec3(0, 0, -1));
const vec3 FACE_U[6] = vec3[](vec3(0, 1, 0), vec3(0, 0, 1), vec3(0, 0, 1), vec3(1, 0, 0), vec3(1, 0, 0),
                              vec3(0, 1, 0));
const vec3 FACE_V[6] = vec3[](vec3(0, 0, 1), vec3(0, 1, 0), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0),
                              vec3(1, 0, 0));

// A box from A to B, `radius` either side of the line -- but never thinner on
// screen than minimumHalfPixels, so a far beam stays seen.
vec3 lineVertex(uint index) {
    uint face = index / 6u;
    uvec2 corner = QUAD[index % 6u];
    vec3 cube = FACE_AXIS[face] + FACE_U[face] * (2.0 * float(corner.x) - 1.0)
                + FACE_V[face] * (2.0 * float(corner.y) - 1.0);
    vec3 along = inB - inA;
    float length_ = length(along);
    vec3 forward = length_ > 1e-4 ? along / length_ : vec3(1.0, 0.0, 0.0);
    vec3 helper = abs(forward.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 side = normalize(cross(forward, helper));
    vec3 up = cross(forward, side); // forward, side, up: right-handed, as the cube's x, y, z
    float t = cube.x * 0.5 + 0.5;
    vec3 end = mix(inA, inB, t);
    float half_ = max(inRadius, shape.minimumHalfPixels * shape.pixelAngle * distance(shape.eye, end));
    // The ends reach `half_` past A and B, so a line seen end on is a square.
    return end + forward * (cube.x * half_) + (side * cube.y + up * cube.z) * half_;
}

void main() {
    vec3 world = inKind == SPHERE ? sphereVertex(uint(gl_VertexIndex))
                 : inKind == LINE ? lineVertex(uint(gl_VertexIndex))
                                  : cylinderVertex(uint(gl_VertexIndex));
    colour = inColour;
    gl_Position = shape.viewProj * vec4(world, 1.0);
}
