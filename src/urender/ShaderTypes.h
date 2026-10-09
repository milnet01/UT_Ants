// The structs a shader reads, laid out byte for byte as std430 lays them out --
// docs/specs/UTA-0014-vulkan-draw-path.md INV-9.
//
// shaders/types.glsl declares the same structs. Every struct's size and every
// field's offset is asserted below, so a field added or widened without its
// padding is a compile error on every leg, MSVC included, rather than a shader
// reading the wrong bytes -- which on a GPU is a device-lost, not a wrong pixel.
//
// THE ASSERTIONS CATCH A STRUCT THAT CHANGES. Nothing catches a struct added to
// the shader interface and never given any (SS 10's partial row), so a new one
// goes here, with its assertions, in the same change.
//
// INTERNAL. No header another subsystem includes may include this one; it is
// plain data and needs no Vulkan type, but it is layout, not API.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace uta::urender::gpu {

/// Column-major, as GLSL reads a mat4.
using Mat4 = std::array<float, 16>;

/// "No texture here": an index into the texture array that is never bound.
inline constexpr std::uint32_t NONE = 0xFFFFFFFFu;

/// UT99's EPolyFlags -- SS 4.5's table, values from Surreal's Engine/Inc/UnObj.h.
inline constexpr std::uint32_t PF_INVISIBLE = 0x00000001u;
inline constexpr std::uint32_t PF_MASKED = 0x00000002u;
inline constexpr std::uint32_t PF_TRANSLUCENT = 0x00000004u;
inline constexpr std::uint32_t PF_NOT_SOLID = 0x00000008u;
inline constexpr std::uint32_t PF_MODULATED = 0x00000040u; ///< UTA-0271
inline constexpr std::uint32_t PF_FAKE_BACKDROP = 0x00000080u;
inline constexpr std::uint32_t PF_TWO_SIDED = 0x00000100u;
inline constexpr std::uint32_t PF_UNLIT = 0x00400000u;
inline constexpr std::uint32_t PF_PORTAL = 0x04000000u;

/// SS 4.6: the cluster grid, the most lights one cluster keeps, and the bit a
/// cluster's count carries when it dropped some. shaders/types.glsl states the
/// capacity and the bit too; the grid reaches the shaders through FrameData.
inline constexpr std::array<std::uint32_t, 3> CLUSTER_GRID{16, 8, 24};
inline constexpr std::uint32_t CLUSTER_COUNT = 16u * 8u * 24u;
inline constexpr std::uint32_t CLUSTER_CAPACITY = 64;
inline constexpr std::uint32_t CLUSTER_OVERFLOW = 0x80000000u;

/// The scene descriptor set's bindings. shaders/types.glsl numbers them the same.
enum Binding : std::uint32_t {
    FRAME = 0,
    OBJECTS = 1,
    MATERIALS = 2,
    LIGHTS = 3,
    CLUSTER_COUNTS = 4,
    CLUSTER_INDICES = 5,
    CLUSTER_BOUNDS = 6,
    PROBE_CELLS = 7,
    PROBES = 8,
    SHADOW_FACES = 9,
    ZONES = 10,        ///< UTA-0156 SS 4.4
    FLAMES = 11,       ///< UTA-0263 SS 4.4: one FlameInstance per FLAM record
    FLAME_RAMPS = 12,  ///< UTA-0263 SS 4.2: eight entries per flame look
    LIQUIDS = 13,      ///< UTA-0105 SS 4.4: one Liquid per liquid look
    MASK_CHARTS = 14,  ///< UTA-0326 SS 4.5: one MaskChart per SMSK chart
    MASK_PAIRS = 15,   ///< UTA-0326 SS 4.5: the charts' pairs; the last storage buffer
    SHADOW_ATLAS = 16,
    FOG_VOLUME = 17, ///< UTA-0015 SS 4.4: the integrated fog image, as a sampler3D
    TEXTURES = 18,   ///< last: it is the variable-count binding
};

/// One frame's camera and settings.
struct FrameData {
    Mat4 viewProj;                   ///< jittered -- what gl_Position uses
    Mat4 viewProjUnjittered;         ///< SS 4.11: motion vectors carry no jitter
    Mat4 previousViewProjUnjittered; ///< the last frame's, cached by the renderer
    Mat4 view;                       ///< world to view, for the cluster depth
    std::array<float, 3> eye;
    float exposure;
    std::array<std::uint32_t, 3> clusterGrid;
    std::uint32_t lightCount;
    float clusterDepthScale;
    float clusterDepthBias;
    float nearPlane;
    float farPlane;
    std::array<float, 2> viewportSize;
    std::uint32_t probeSpacing;
    std::uint32_t probeCount;
    std::uint32_t probeTableMask;  ///< the probe table's size minus one
    std::uint32_t probeLongestRun; ///< the most slots past its hash any probe sits
    std::uint32_t skyTexture;      ///< UTA-0163: an index into the texture array, or NONE
    std::uint32_t shadowFaceCount;
    std::uint32_t skyFirstFace;    ///< UTA-0163: the sky's six faces, from here in the face table
    std::uint32_t occlusionTexture; ///< UTA-0164: an index into the texture array, or NONE
    float flameSeconds;          ///< UTA-0263 SS 4.4: the light clock, wrapped
    std::uint32_t cameraZone;    ///< UTA-0089 SS 4.1: the zone the camera is in
    std::uint32_t cameraUnderwater; ///< UTA-0215 SS 4.3: nonzero where the camera's zone is water
    /// UTA-0215 SS 4.8: the camera's zone's ViewFog as the light that displays
    /// as it, which the water's specks are drawn in. Three floats, not a vec3,
    /// which std430 would move to the next 16.
    std::array<float, 3> waterFog;
    std::uint32_t skyCapture; ///< UTA-0281: nonzero while UTA-0163 captures the sky's faces
    std::uint32_t shadowMaskTexture; ///< UTA-0326 SS 4.5: an index into the texture array, or NONE
    /// UTA-0326 SS 4.5: the flashlight's index into LIGHTS, or NONE. A level
    /// fragment lit from its chart's pairs adds it, since no pair names it.
    std::uint32_t flashlight;
    /// UTA-0338 SS 4.5: the sun's index into LIGHTS, or NONE. It is in no
    /// cluster; the shader lights other fragments from it through the probes'
    /// view of it, and draws its disc in the sky.
    std::uint32_t sun;
};
static_assert(sizeof(FrameData) == 384);
static_assert(offsetof(FrameData, viewProj) == 0);
static_assert(offsetof(FrameData, viewProjUnjittered) == 64);
static_assert(offsetof(FrameData, previousViewProjUnjittered) == 128);
static_assert(offsetof(FrameData, view) == 192);
static_assert(offsetof(FrameData, eye) == 256);
static_assert(offsetof(FrameData, exposure) == 268);
static_assert(offsetof(FrameData, clusterGrid) == 272);
static_assert(offsetof(FrameData, lightCount) == 284);
static_assert(offsetof(FrameData, clusterDepthScale) == 288);
static_assert(offsetof(FrameData, clusterDepthBias) == 292);
static_assert(offsetof(FrameData, nearPlane) == 296);
static_assert(offsetof(FrameData, farPlane) == 300);
static_assert(offsetof(FrameData, viewportSize) == 304);
static_assert(offsetof(FrameData, probeSpacing) == 312);
static_assert(offsetof(FrameData, probeCount) == 316);
static_assert(offsetof(FrameData, probeTableMask) == 320);
static_assert(offsetof(FrameData, probeLongestRun) == 324);
static_assert(offsetof(FrameData, skyTexture) == 328);
static_assert(offsetof(FrameData, shadowFaceCount) == 332);
static_assert(offsetof(FrameData, skyFirstFace) == 336);
static_assert(offsetof(FrameData, occlusionTexture) == 340); // UTA-0164 SS 4.5
static_assert(offsetof(FrameData, flameSeconds) == 344);
static_assert(offsetof(FrameData, cameraZone) == 348);
static_assert(offsetof(FrameData, cameraUnderwater) == 352);
static_assert(offsetof(FrameData, skyCapture) == 368);
static_assert(offsetof(FrameData, shadowMaskTexture) == 372); // UTA-0326 SS 4.5
static_assert(offsetof(FrameData, flashlight) == 376);
static_assert(offsetof(FrameData, sun) == 380); // UTA-0338 SS 4.5

/// Where one drawn thing is: the level (identity) or a mover.
struct Object {
    Mat4 model;
    Mat4 previousModel;
    Mat4 normalMatrix; ///< the inverse transpose of model's upper 3x3, in a mat4
};
static_assert(sizeof(Object) == 192);
static_assert(offsetof(Object, model) == 0);
static_assert(offsetof(Object, previousModel) == 64);
static_assert(offsetof(Object, normalMatrix) == 128);

/// One material: indices into the texture array, NONE where a map is absent.
struct Material {
    std::uint32_t base;
    std::uint32_t normal;
    std::uint32_t rough;
    std::uint32_t height;
    std::uint32_t emit;
    std::uint32_t metallic;
    std::uint32_t parallaxDepth; ///< UTA-0040 SS 4.6: texels of the base level; 0 for none
    std::uint32_t flame;         ///< UTA-0263 SS 4.4: its ramp's first entry in FLAME_RAMPS, or NONE
    std::uint32_t liquid;        ///< UTA-0105 SS 4.4: its look's index in LIQUIDS, or NONE
    std::uint32_t glass;         ///< UTA-0270: an Ice look's glass picture, or NONE
    std::uint32_t detail;        ///< UTA-0275: its DetailTexture picture, or NONE
    std::array<float, 2> detailRepeats; ///< UTA-0275: detail repeats per repeat of the base, u and v
    std::uint32_t tileKind;      ///< UTA-0277 SS 4.1: ubundle::TileKind's value; only 1 moves its repeats
};
static_assert(sizeof(Material) == 56);
static_assert(offsetof(Material, base) == 0);
static_assert(offsetof(Material, normal) == 4);
static_assert(offsetof(Material, rough) == 8);
static_assert(offsetof(Material, height) == 12);
static_assert(offsetof(Material, emit) == 16);
static_assert(offsetof(Material, metallic) == 20);
static_assert(offsetof(Material, parallaxDepth) == 24);
static_assert(offsetof(Material, flame) == 28);
static_assert(offsetof(Material, liquid) == 32);
static_assert(offsetof(Material, glass) == 36);
static_assert(offsetof(Material, detail) == 40);
static_assert(offsetof(Material, detailRepeats) == 44);
static_assert(offsetof(Material, tileKind) == 52);

/// One light, as UT99's own numbers -- the shader turns them into light
/// (SS 3 decision 5), so no part of UTA-0112 SS 4.3's model is computed here.
struct Light {
    std::array<float, 3> location;
    float flicker; ///< SS 4.9's time-varying scalar; 1 for a steady light
    std::uint32_t hue;
    std::uint32_t saturation;
    std::uint32_t brightness;
    std::uint32_t radius;
    std::uint32_t effect;
    std::uint32_t cone;
    std::int32_t pitch;
    std::int32_t yaw;
    std::int32_t shadowFace;       ///< first entry in SHADOW_FACES, or -1
    std::uint32_t shadowFaceCount; ///< 0 for a light drawn without shadows
    std::uint32_t volumeRadius;     ///< UTA-0015 SS 4.4: UT99's VolumeRadius; 0 for none
    std::uint32_t volumeBrightness; ///< UTA-0015: UT99's VolumeBrightness
    std::array<float, 3> span; ///< UTA-0162: a strip leader's segment, from `location`; zero otherwise
    std::uint32_t volumeFog;   ///< UTA-0015: UT99's VolumeFog
    float levelBrightness;     ///< UTA-0156 SS 4.5: LevelInfo.Brightness
    std::array<std::uint32_t, 3> pad{}; ///< std430 rounds the struct to its vec3's 16
};
static_assert(sizeof(Light) == 96);
static_assert(offsetof(Light, location) == 0);
static_assert(offsetof(Light, flicker) == 12);
static_assert(offsetof(Light, hue) == 16);
static_assert(offsetof(Light, saturation) == 20);
static_assert(offsetof(Light, brightness) == 24);
static_assert(offsetof(Light, radius) == 28);
static_assert(offsetof(Light, effect) == 32);
static_assert(offsetof(Light, cone) == 36);
static_assert(offsetof(Light, pitch) == 40);
static_assert(offsetof(Light, yaw) == 44);
static_assert(offsetof(Light, shadowFace) == 48);
static_assert(offsetof(Light, shadowFaceCount) == 52);
static_assert(offsetof(Light, volumeRadius) == 56);
static_assert(offsetof(Light, volumeBrightness) == 60);
static_assert(offsetof(Light, span) == 64);
static_assert(offsetof(Light, volumeFog) == 76);
static_assert(offsetof(Light, levelBrightness) == 80);

/// One cluster's box, in view space.
struct ClusterBounds {
    std::array<float, 4> minimum;
    std::array<float, 4> maximum;
};
static_assert(sizeof(ClusterBounds) == 32);
static_assert(offsetof(ClusterBounds, minimum) == 0);
static_assert(offsetof(ClusterBounds, maximum) == 16);

/// One probe's ambient cube: six linear-RGB faces, +X -X +Y -Y +Z -Z, each
/// padded to a vec4 as a std430 array of vec3 would not be.
struct Probe {
    std::array<float, 24> faces;
};
static_assert(sizeof(Probe) == 96);
static_assert(offsetof(Probe, faces) == 0);

/// One slot of the probe table (Probes.h): a lattice cell and its probe's
/// index, or -1 where the slot is empty.
struct ProbeCell {
    std::array<std::int32_t, 3> cell;
    std::int32_t probe;
};
static_assert(sizeof(ProbeCell) == 16);
static_assert(offsetof(ProbeCell, cell) == 0);
static_assert(offsetof(ProbeCell, probe) == 12);

/// One shadow-map face: its light's projection, and its tile in the atlas as
/// (u offset, v offset, u size, v size).
struct ShadowFace {
    Mat4 viewProj;
    std::array<float, 4> atlasRect;
};
static_assert(sizeof(ShadowFace) == 80);
static_assert(offsetof(ShadowFace, viewProj) == 0);
static_assert(offsetof(ShadowFace, atlasRect) == 64);

/// UTA-0326 SS 4.5: one SMSK chart -- its run of MASK_PAIRS, left out pairs
/// already dropped, and its rectangle's size, border included.
struct MaskChart {
    std::uint32_t firstPair;
    std::uint32_t pairCount;
    std::uint32_t width;
    std::uint32_t height;
};
static_assert(sizeof(MaskChart) == 16);
static_assert(offsetof(MaskChart, firstPair) == 0);
static_assert(offsetof(MaskChart, pairCount) == 4);
static_assert(offsetof(MaskChart, width) == 8);
static_assert(offsetof(MaskChart, height) == 12);

/// UTA-0326 SS 4.5: one SMSK pair, its light an index into LIGHTS. `x` is
/// ubundle::MASK_ALL_LIT for a pair that stores no texels.
struct MaskPair {
    std::uint32_t light;
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t moverReach; ///< 1 where the pair reads the shadow map instead (SS 4.4)
};
static_assert(sizeof(MaskPair) == 16);
static_assert(offsetof(MaskPair, light) == 0);
static_assert(offsetof(MaskPair, x) == 4);
static_assert(offsetof(MaskPair, y) == 8);
static_assert(offsetof(MaskPair, moverReach) == 12);

/// One zone's ambient light, as UT99's bytes -- UTA-0156 SS 4.4. The shader
/// turns them into light, as it does a Light's.
struct Zone {
    std::uint32_t brightness;
    std::uint32_t hue;
    std::uint32_t saturation;
    std::uint32_t sky; ///< UTA-0089 SS 4.1: 1 where the zone holds a sky window
    std::uint32_t water; ///< UTA-0215: 1 where the zone is water, for its caustics
};
static_assert(sizeof(Zone) == 20);
static_assert(offsetof(Zone, brightness) == 0);
static_assert(offsetof(Zone, hue) == 4);
static_assert(offsetof(Zone, saturation) == 8);
static_assert(offsetof(Zone, sky) == 12);

/// One liquid look as the shader reads it -- UTA-0105 SS 4.2 and SS 4.4. The
/// settings stay UT99's bytes, as floats; liquid.glsl maps them.
struct Liquid {
    std::uint32_t kind;    ///< ubundle::LiquidKind: 1 Wet, 2 Ice, 3 Wave
    std::uint32_t panning; ///< Ice: PanningStyle
    float amplitude;
    float frequency;
    std::array<float, 2> pan;  ///< Ice: HorizPanSpeed, VertPanSpeed
    std::array<float, 2> size; ///< texels
    std::array<float, 4> bump; ///< Wave: BumpMapLight, BumpMapAngle, PhongSize; Ice: w MoveIce (UTA-0270)
    std::array<std::array<float, 4>, 8> ramp; ///< Wave: linear RGB, darkest first
};
static_assert(sizeof(Liquid) == 176);
static_assert(offsetof(Liquid, pan) == 16);
static_assert(offsetof(Liquid, size) == 24);
static_assert(offsetof(Liquid, bump) == 32);
static_assert(offsetof(Liquid, ramp) == 48);

/// One entry of a flame's ramp, linear RGB and an unused fourth -- UTA-0263
/// SS 4.2. A look is eight in a row, coldest first.
using FlameRampEntry = std::array<float, 4>;

/// One camera-facing flame, a FLAM record as the shader draws it -- UTA-0263
/// SS 4.3 and SS 4.4.
struct FlameInstance {
    std::array<float, 3> base; ///< the foot, world units
    float width;
    float height;
    std::uint32_t seed;
    std::uint32_t ramp; ///< its look's first entry in FLAME_RAMPS
    std::uint32_t reserved;
};
static_assert(sizeof(FlameInstance) == 32);
static_assert(offsetof(FlameInstance, base) == 0);
static_assert(offsetof(FlameInstance, width) == 12);
static_assert(offsetof(FlameInstance, height) == 16);
static_assert(offsetof(FlameInstance, seed) == 20);
static_assert(offsetof(FlameInstance, ramp) == 24);

/// The scene pipelines' push constants: which object, which material, and the
/// batch's own flags, which the shaders test bit by bit. UTA-0269: how far its
/// texture has panned, in repeats and under 1, worked out on the CPU in double
/// so a long-running clock does not lose the fraction.
struct DrawConstants {
    std::uint32_t objectIndex;
    std::uint32_t materialIndex;
    std::uint32_t polyFlags;
    std::uint32_t reserved;            ///< so panOffset sits on the vec2's 8 bytes
    std::array<float, 2> panOffset;
};
static_assert(sizeof(DrawConstants) == 24);
static_assert(offsetof(DrawConstants, objectIndex) == 0);
static_assert(offsetof(DrawConstants, materialIndex) == 4);
static_assert(offsetof(DrawConstants, polyFlags) == 8);
static_assert(offsetof(DrawConstants, panOffset) == 16);

/// A shadow tile's push constants: the face it is drawn from, and the batch.
struct ShadowConstants {
    Mat4 viewProj;
    std::uint32_t objectIndex;
    std::uint32_t materialIndex;
    std::uint32_t polyFlags;
    std::uint32_t reserved;
};
static_assert(sizeof(ShadowConstants) == 80);
static_assert(offsetof(ShadowConstants, viewProj) == 0);
static_assert(offsetof(ShadowConstants, objectIndex) == 64);
static_assert(offsetof(ShadowConstants, materialIndex) == 68);
static_assert(offsetof(ShadowConstants, polyFlags) == 72);
static_assert(offsetof(ShadowConstants, reserved) == 76);

/// The output stage's push constants -- SS 4.10.
struct PostConstants {
    float exposure;
    std::uint32_t linearOutput; ///< Config::linearOutput: skip exposure and the tone map
    /// UTA-0051 SS 4.4: the top-left part of the HDR target this frame drew.
    std::array<std::uint32_t, 2> regionSize;
    /// UTA-0154: nonzero writes FSR 1's input -- gamma 2.0, edge-clamped past
    /// the region -- rather than the output.
    std::uint32_t upscaleInput;
    /// UTA-0053: how much of the bloom chain the frame adds; 0 where the tier
    /// draws no bloom.
    float bloomStrength;
    float reserved; ///< keeps flashFog on its 16 bytes
    /// UTA-0215 SS 4.4: the light clock, wrapped as flames' is, for the wobble.
    float wobbleSeconds;
    /// UTA-0215 SS 4.2: xyz the zone's ViewFog, which sets the view's veil; w nonzero under water.
    std::array<float, 4> flashFog;
};
static_assert(sizeof(PostConstants) == 48);
static_assert(offsetof(PostConstants, exposure) == 0);
static_assert(offsetof(PostConstants, linearOutput) == 4);
static_assert(offsetof(PostConstants, regionSize) == 8);
static_assert(offsetof(PostConstants, upscaleInput) == 16);
static_assert(offsetof(PostConstants, bloomStrength) == 20);
static_assert(offsetof(PostConstants, reserved) == 24);
static_assert(offsetof(PostConstants, wobbleSeconds) == 28);
static_assert(offsetof(PostConstants, flashFog) == 32);

/// UTA-0053: which step of the bloom chain bloom.frag draws.
inline constexpr std::uint32_t BLOOM_DOWNSAMPLE_FIRST = 0; ///< from the emission target, Karis-weighted
inline constexpr std::uint32_t BLOOM_DOWNSAMPLE = 1;
inline constexpr std::uint32_t BLOOM_UPSAMPLE = 2; ///< added into the level above

/// UTA-0053: one bloom step's push constants.
struct BloomConstants {
    std::array<float, 2> sourceTexel;
    std::array<float, 2> targetTexel;
    std::array<float, 2> sourceUvMax; ///< past the drawn region nothing is read
    float radius;                     ///< the upsample tent's, in UV
    std::uint32_t mode;
};
static_assert(sizeof(BloomConstants) == 32);
static_assert(offsetof(BloomConstants, sourceTexel) == 0);
static_assert(offsetof(BloomConstants, targetTexel) == 8);
static_assert(offsetof(BloomConstants, sourceUvMax) == 16);
static_assert(offsetof(BloomConstants, radius) == 24);
static_assert(offsetof(BloomConstants, mode) == 28);

/// UTA-0015 SS 4.4: both fog stages' push constants.
struct FogConstants {
    Mat4 viewToWorld;
    std::array<float, 2> tanHalfFov; ///< across, then down, for the drawn region
    std::uint32_t volumeLightCount;  ///< entries of VOLUME_LIGHTS in use
    std::uint32_t flashlight;        ///< an index into LIGHTS, or NONE
    float hazeScale;                 ///< Config::hazeScale
    std::array<float, 3> reserved;
};
static_assert(sizeof(FogConstants) == 96);
static_assert(offsetof(FogConstants, viewToWorld) == 0);
static_assert(offsetof(FogConstants, tanHalfFov) == 64);
static_assert(offsetof(FogConstants, volumeLightCount) == 72);
static_assert(offsetof(FogConstants, flashlight) == 76);
static_assert(offsetof(FogConstants, hazeScale) == 80);
static_assert(offsetof(FogConstants, reserved) == 84);

} // namespace uta::urender::gpu
