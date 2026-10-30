// The Renderer: its targets, and the frame it draws --
// docs/specs/UTA-0014-vulkan-draw-path.md SS 4.3, SS 4.10 and SS 4.11.
//
// THE FRAME, IN ORDER -- recordFrame's order, one function a stage:
//   fires: UTA-0286's replayed pictures the frame advanced, uploaded;
//   shadow tiles: those this frame's plan says to draw (SS 4.8), each then
//      copied into UTA-0333's quarter-size copy for the haze;
//   0. clustered light culling, a compute pass (SS 4.6);
//   0.5. UTA-0015's fog volume, two compute passes over a froxel grid, from
//      the tier that draws it; the forward pass reads it;
//   1. the forward pass: UTA-0260's depth-only pre-pass of the opaque
//      surfaces, then every opaque, masked and sky batch, writing colour,
//      velocity, emission and depth;
//   2. the translucent pass: colour only, depth-tested and not written;
//   2.5. UTA-0263's camera-facing flames, and UTA-0215's water specks;
//   3. UTA-0053's emissive bloom: the emission down a chain of half-size
//      levels and back up it;
//   4. the output stage: the bloom added, then exposure and the tone map into
//      the _SRGB target;
//   5. the UI composite seam, after the output stage (SS 4.11 provision 3).
//
// THE SURFACELESS PATH SUBMITS AND WAITS. draw returns when the frame is
// finished, so readback needs no synchronisation of its own. The presenting
// path does the same and then presents -- and it acquires before the frame is
// planned, so a frame skipped for an out-of-date surface leaves no shadow tile
// believed drawn (Swapchain.h).

#include "urender/Renderer.h"

#include "urender/Clusters.h"
#include "urender/Device.h"
#include "urender/Fog.h"
#include "urender/Geometry.h"
#include "urender/Lights.h"
#include "urender/Materials.h"
#include "urender/Pipelines.h"
#include "urender/Placement.h"
#include "urender/Probes.h"
#include "urender/Shadows.h"
#include "urender/Sky.h"
#include "urender/Resources.h"
#include "urender/ShaderTypes.h"
#include "urender/Swapchain.h"
#include "urender/Tiers.h"
#include "urender/Zones.h"

#include "ubundle/Sky.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <span>

namespace uta::urender {

namespace {

constexpr VkFormat HDR_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat VELOCITY_FORMAT = VK_FORMAT_R16G16_SFLOAT;
constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
/// UTA-0333: atlas texels per texel of the haze's copy, on each axis --
/// shadow_reduce.comp's HAZE_SHADOW_STEP, which every tile side is a multiple of.
constexpr std::uint32_t HAZE_SHADOW_STEP = 4;
/// SS 4.10 names it, because a read-back pixel is compared against a literal
/// and B8G8R8A8 would swap two channels of it without failing anything else.
constexpr VkFormat OUTPUT_FORMAT = VK_FORMAT_R8G8B8A8_SRGB;

/// Where each texture sits in the scene set's TEXTURES array, and how many there
/// are. The one place the order is decided: the materials' textures, then
/// UTA-0163's sky faces, UTA-0164's occlusion atlas, UTA-0326's shadow mask, and
/// UTA-0337's sky-layer lights last. An absent texture takes no slot and reads
/// gpu::NONE. upload sizes the array from it, writeDescriptors fills the array in
/// this order, and drawView hands the shaders these indices.
struct TextureSlots {
    std::uint32_t sky = gpu::NONE;
    std::uint32_t occlusion = gpu::NONE;
    std::uint32_t shadowMask = gpu::NONE;
    std::uint32_t firstSkyLayerLight = 0; ///< the lights take [firstSkyLayerLight, count)
    std::uint32_t count = 0;
};

TextureSlots textureSlotsOf(std::size_t materialTextures, bool sky, bool occlusion, bool shadowMask,
                            std::size_t skyLayerLights) {
    TextureSlots slots;
    auto next = static_cast<std::uint32_t>(materialTextures);
    if (sky) slots.sky = next++;
    if (occlusion) slots.occlusion = next++;
    if (shadowMask) slots.shadowMask = next++;
    slots.firstSkyLayerLight = next;
    slots.count = next + static_cast<std::uint32_t>(skyLayerLights);
    return slots;
}

/// SS 4.10: fixed, with no adaptation. Measured, not chosen: the exposure whose
/// displayed frames best match the original game's over DM-Deck16]['s
/// PlayerStart views, drawn from the same cameras. UTA-0156 fitted 2.41 and set
/// 2.4; UTA-0162's strip lights moved the fit to 3.05, and 3.0 has the lower
/// block error of the two nearest. UTA-0156's cylinder fade moved it to 3.18,
/// and 3.2 has the lowest block error of 3.0, 3.1 and 3.2. The method and
/// scripts are on UTA-0156.
/// UTA-0165's FGetHSV brightness lifts every light, so it was refitted over
/// DM-Deck16][, AS-Frigate and DM-Fetid at once, fog zeroed and with
/// AMBIENT_SCALE fitted alongside: pooled block RMS 42.2 at ambient 1.5 and
/// exposure 2.20, per map 45.8, 40.3 and 40.5, where the linear model at 3.2
/// had scored 46.2, 40.3 and 42.8.
/// UTA-0165 then found every light scaled by LevelInfo.Brightness (UTA-0156
/// SS 4.5) and UTA-0166 gave every light a shadow; refitted with AMBIENT_SCALE
/// on baker revision 17, fog zeroed, the joint exposure is 5.53 at ambient 0.5,
/// pooled block RMS 36.6, per map 37.5, 34.3 and 40.8. Each map's own best
/// exposure is 5.26, 5.68 and 5.76, where before it they were 4.22, 3.24 and
/// 8.10 (ut-ants-uta0156/sweepamb17b.sh).
/// UTA-0168 let light through PF_NotSolid, which AS-Frigate's lanterns had
/// blocked; refitted on baker revision 18 the same way. By ambient: 0.25
/// exposure 5.85, RMS 36.1; 0.5 exposure 5.38, RMS 35.7, per map 37.3, 32.4 and
/// 40.8; 1.0 exposure 4.49, RMS 36.6. At ambient 0.5 the maps' own best
/// exposures are 5.26, 5.38 and 5.80, AS-Frigate's falling from 5.76. Scored at
/// 5.3, 5.4 and 5.5, only DM-Deck16][ moves, 37.3, 37.3 and 37.4; 5.4 is the
/// nearest tenth to the fit (ut-ants-uta0156/sweepamb18.sh).
/// UTA-0187 made light meet texture on display values, which changes what a
/// unit of light is worth; refitted jointly with DISPLAY_LIGHT_POWER 1.6 and
/// AMBIENT_SCALE 1 on baker revision 21, its joint k was 6.16
/// (ut-ants-uta0187/sweep187b.py, r21-c1.6-a1).
/// UTA-0192 then took the toe out of the tone map, which had been darkening
/// every dim surface, so less exposure reaches the same displayed brightness.
/// Refitted on the SAME renders -- the display transform is applied offline, so
/// no re-render was needed (ut-ants-uta0192/tonefit.py, decompose.py).
/// Two scorings, because the tone map is the thing that moved. Tone mapped per
/// BLOCK, as every fit before this one: 4.19, pooled block RMS 32.49. Tone
/// mapped per PIXEL and then blocked: 4.52, pooled 32.29, per map 28.8, 36.8
/// and 32.3. The pixel figure is the faithful one: the original's blocks are
/// means of display-space bytes, so applying a non-linear operator to a
/// 40-pixel linear mean is the wrong order. The block figure is kept because
/// light.glsl's numbers are in that space.
/// Those are at AMBIENT_SCALE 1. Removing the toe also moves THAT constant's
/// optimum to 0.75 (light.glsl carries the re-score), and the exposure is the
/// joint k of the pair actually shipped: pooled 32.01 per pixel at A 0.75,
/// against 32.29 at A 1 and 33.27 at A 1.3, per map 28.7, 36.4 and 31.9
/// (ut-ants-uta0192/crosscheck2.py).
/// GRADED, from UTA-0192, by tests/device/RenderOutputStageTest.cpp: every
/// other device test sets linearOutput and reads back before the post chain, so
/// until then nothing in the tier graded this constant at all.
constexpr float EXPOSURE = 5.03f;
// UTA-0383: the bake stores a painted sky dimmed by this, so the two move together.
static_assert(EXPOSURE == ubundle::PAINTED_SKY_EXPOSURE);

/// UTA-0053's emissive bloom, as LearnOpenGL's physically based bloom builds it
/// (learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom, after Jimenez's
/// SIGGRAPH 2014 Call of Duty: Advanced Warfare talk): five levels, each half
/// the one before, an upsample radius of 0.005, and 0.04 of the result. Only
/// emission feeds it, so the frame adds it rather than mixing it in.
constexpr std::size_t BLOOM_LEVELS = 5;
constexpr float BLOOM_RADIUS = 0.005f;
constexpr float BLOOM_STRENGTH = 0.04f;

/// UTA-0263 SS 4.4: the flame clock's period in seconds. At 4096 a float still
/// resolves half a millisecond; a flame jumps once at each wrap, every 68 min.
constexpr double FLAME_CLOCK_WRAP = 4096.0;
// UTA-0337: how fast a painted sky's clouds drift, in their plane's units a
// second -- the plane at height 2/3, so 0.0075 is clouds 2 km up moving
// 10 m/s: a cloud crosses its own width in about two minutes. Not wrapped:
// the noise does not repeat, so a wrap would jump. A float still moves it
// smoothly after a day on the light clock (about 650 units, a step of 6e-5
// against 1.2e-4 a frame at 60 fps).
constexpr std::array<double, 2> PAINTED_SKY_WIND{0.0072, 0.0021};
/// UTA-0215: how many specks drift in the water about the eye, which
/// mote.vert places from their index alone.
constexpr std::uint32_t MOTE_COUNT = 800;

/// What identifies an uploaded bundle: the object, the size of every section
/// this renderer uploads, and a hash of a bounded sample of their bytes.
///
/// THE SAMPLE IS WHAT CATCHES A RELOAD. A caller that loads another map of the
/// same sizes into the same storage -- a std::optional re-emplaced, say --
/// hands draw the same address, and the address and sizes alone would keep
/// drawing the old map's upload. The sample covers every material id, every
/// texture's name, format and first and last block, and the first and last
/// vertex and index of each geometry, so it stays cheap per frame.
///
/// What it cannot see: an edit in place that changes only unsampled bytes. The
/// movers' transforms are read every frame and need no upload. The lights are
/// read every frame too, but upload sizes the light and shadow-face buffers by
/// how many draw directly and finds each one's zone from where it stands, so
/// both are sampled for every light (review-code 2026-10-15).
/// ZONE is sampled whole: it holds at most ZONE_LIMIT entries (UTA-0156).
/// AOCC by its size and the ends of its uvs and texels (UTA-0164).
/// SMSK by its sizes and the ends of its arrays (UTA-0326).
/// ROOM by each room's zone, its zone-to-room table whole, and the ends of its
/// nodes and leaf zones: upload finds each light's zone and the sky layers'
/// zone in it (UTA-0399).
struct BundleShape {
    const ubundle::Bundle* address = nullptr;
    std::size_t vertices = 0, indices = 0, batches = 0, textures = 0, materials = 0, movers = 0, moverIndices = 0,
                lights = 0, probes = 0, zones = 0, flames = 0;
    bool occlusion = false;
    bool shadowMask = false;
    bool skyView = false;
    std::uint64_t sample = 0;
    bool operator==(const BundleShape&) const = default;
};

/// FNV-1a, 64-bit, folded over whatever it is given.
class Fnv {
public:
    void add(std::span<const std::byte> bytes) noexcept {
        for (const std::byte b : bytes) hash_ = (hash_ ^ std::to_integer<std::uint64_t>(b)) * 0x100000001b3ULL;
    }
    template <class T>
    void addValue(const T& value) noexcept { add(std::as_bytes(std::span(&value, 1))); }
    template <class T>
    void addEnds(std::span<const T> values) noexcept {
        addValue(values.size());
        if (values.empty()) return;
        add(std::as_bytes(values.first(1)));
        add(std::as_bytes(values.last(1)));
    }
    [[nodiscard]] std::uint64_t value() const noexcept { return hash_; }

private:
    std::uint64_t hash_ = 0xcbf29ce484222325ULL;
};

void sampleGeometry(Fnv& fnv, const ubundle::Geometry& geometry) {
    fnv.addEnds(std::span<const ubundle::GeometryVertex>(geometry.vertices));
    fnv.addEnds(std::span<const std::uint32_t>(geometry.indices));
    for (const ubundle::GeometryBatch& batch : geometry.batches) {
        fnv.add(std::as_bytes(std::span(batch.material)));
        fnv.addValue(batch.polyFlags);
        fnv.addValue(batch.firstIndex);
        fnv.addValue(batch.indexCount);
        fnv.addValue(batch.panRate); // UTA-0269, uploaded with the draws
    }
}

BundleShape shapeOf(const ubundle::Bundle& bundle) {
    BundleShape shape;
    shape.address = &bundle;
    Fnv fnv;
    if (bundle.geometry) sampleGeometry(fnv, *bundle.geometry);
    if (bundle.movers)
        for (const ubundle::MoverShape& mover : *bundle.movers) {
            sampleGeometry(fnv, mover.geometry);
            fnv.addValue(mover.still); // UTA-0439 SS 4.4: it decides which vertices read AOCC and SMSK
        }
    if (bundle.materials) {
        for (const ubundle::MaterialRecord& record : *bundle.materials) {
            fnv.add(std::as_bytes(std::span(record.id)));
            fnv.addValue(record.metallic);
            fnv.addValue(record.parallaxDepth); // uploaded, so a change must re-upload
            fnv.addValue(record.tileKind);      // UTA-0277 SS 4.1, uploaded too
            if (record.flame) fnv.addValue(record.flame->ramp); // UTA-0263 SS 4.2, uploaded too
            if (record.liquid) { // UTA-0105 SS 4.2, uploaded too
                const ubundle::LiquidLook& look = *record.liquid;
                fnv.addValue(static_cast<std::uint8_t>(look.kind));
                fnv.addValue(look.amplitude);
                fnv.addValue(look.frequency);
                fnv.addValue(look.panning);
                fnv.addValue(look.moveIce); // UTA-0270
                fnv.addValue(look.pan);
                fnv.addValue(look.bump);
                fnv.addValue(look.size);
                fnv.addValue(look.ramp);
            }
            if (record.fire) { // UTA-0286 SS 4.2, uploaded too
                const ubundle::FireLook& look = *record.fire;
                fnv.addValue(look.size);
                fnv.addValue(look.renderHeat);
                fnv.addValue(look.rising);
                fnv.addValue(look.masked);
                fnv.addValue(look.sparksLimit);
                fnv.addValue(look.maxFrameRate);
                fnv.addValue(look.palette);
                fnv.add(std::as_bytes(std::span(look.sparks)));
            }
        }
    }
    // UTA-0156 SS 4.4: a bundle differing only in its zones must re-upload them.
    if (bundle.zones) {
        shape.zones = bundle.zones->size();
        for (const ubundle::Zone& zone : *bundle.zones) {
            fnv.addValue(zone.brightness);
            fnv.addValue(zone.hue);
            fnv.addValue(zone.saturation);
            fnv.addValue(zone.fog); // UTA-0015
            fnv.addValue(zone.panSpeed); // UTA-0276
            fnv.addValue(zone.water);    // UTA-0215: the caustics read it
        }
    }
    if (bundle.textures) {
        for (const ubundle::CompressedTexture& texture : *bundle.textures) {
            fnv.add(std::as_bytes(std::span(texture.name)));
            fnv.addValue(texture.format);
            fnv.addValue(texture.width);
            fnv.addValue(texture.height);
            fnv.addValue(texture.mipCount);
            const std::span<const std::byte> blocks(texture.blocks);
            const std::size_t block = std::min<std::size_t>(16, blocks.size());
            fnv.add(blocks.first(block));
            fnv.add(blocks.last(block));
        }
    }
    if (bundle.lightProbes) {
        fnv.addValue(bundle.lightProbes->spacing);
        fnv.addEnds(std::span<const ubundle::LightProbe>(bundle.lightProbes->probes));
        fnv.addEnds(std::span<const float>(bundle.lightProbes->sunSeen)); // UTA-0338, uploaded with the probes
        fnv.addEnds(std::span<const float>(bundle.lightProbes->skySeen)); // UTA-0387, the same
        fnv.addEnds(std::span<const std::array<float, 6>>(bundle.lightProbes->reach)); // UTA-0397, the same
    }
    // UTA-0164 SS 4.5: a bundle differing only in its occlusion must re-upload it.
    if (bundle.occlusion) {
        shape.occlusion = true;
        fnv.addValue(bundle.occlusion->texelSize);
        fnv.addValue(bundle.occlusion->width);
        fnv.addValue(bundle.occlusion->height);
        fnv.addEnds(std::span<const std::array<float, 2>>(bundle.occlusion->uv));
        fnv.addEnds(std::span<const std::uint8_t>(bundle.occlusion->texels));
    }
    // UTA-0326 SS 4.5: and one differing only in its shadow mask.
    if (bundle.shadowMask) {
        const ubundle::ShadowMask& mask = *bundle.shadowMask;
        shape.shadowMask = true;
        fnv.addValue(mask.texelSize);
        fnv.addValue(mask.width);
        fnv.addValue(mask.height);
        fnv.addEnds(std::span<const std::uint32_t>(mask.vertexChart));
        fnv.addEnds(std::span<const std::array<float, 2>>(mask.vertexTexel));
        fnv.addEnds(std::span<const ubundle::MaskChart>(mask.charts));
        fnv.addEnds(std::span<const ubundle::MaskPair>(mask.pairs));
        fnv.addEnds(std::span<const std::uint8_t>(mask.texels));
    }
    // UTA-0263 SS 4.3: a bundle differing only in its flames must re-upload them.
    if (bundle.flames) {
        shape.flames = bundle.flames->size();
        fnv.addEnds(std::span<const ubundle::Flame>(*bundle.flames));
    }
    // UTA-0350 SS 4.4: upload() reads the sky view, and whether there is one
    // decides between it and the painted sky.
    if (const std::optional<ubundle::SkyView> view = ubundle::skyViewOf(bundle)) {
        shape.skyView = true;
        fnv.addValue(view->location);
    }
    // UTA-0399: upload() finds each light's zone, and the sky layers', in the rooms.
    if (bundle.rooms) {
        const umap::RoomMap& rooms = *bundle.rooms;
        fnv.addValue(rooms.rooms.size());
        for (const umap::Room& room : rooms.rooms) fnv.addValue(room.zoneIndex);
        fnv.addValue(rooms.roomForZone.size());
        fnv.add(std::as_bytes(std::span(rooms.roomForZone)));
        // Field by field: a Node has padding, which a byte hash would read.
        fnv.addValue(rooms.nodes.size());
        if (!rooms.nodes.empty())
            for (const umap::RoomMap::Node* node : {&rooms.nodes.front(), &rooms.nodes.back()}) {
                fnv.addValue(node->normal);
                fnv.addValue(node->w);
                fnv.addValue(node->iFront);
                fnv.addValue(node->iBack);
                fnv.addValue(node->iLeaf);
                fnv.addValue(node->iZone);
            }
        fnv.addEnds(std::span<const std::uint8_t>(rooms.leafZone));
    }
    if (bundle.lights)
        for (const ubundle::Light& light : *bundle.lights) {
            fnv.addValue(drawsDirectly(light));
            fnv.addValue(light.location);
            fnv.addValue(light.stripFrom);
        }
    shape.sample = fnv.value();
    if (bundle.geometry) {
        shape.vertices = bundle.geometry->vertices.size();
        shape.indices = bundle.geometry->indices.size();
        shape.batches = bundle.geometry->batches.size();
    }
    if (bundle.textures) shape.textures = bundle.textures->size();
    if (bundle.materials) shape.materials = bundle.materials->size();
    if (bundle.lights) shape.lights = bundle.lights->size();
    if (bundle.lightProbes) shape.probes = bundle.lightProbes->probes.size();
    if (bundle.movers) {
        shape.movers = bundle.movers->size();
        for (const ubundle::MoverShape& mover : *bundle.movers) shape.moverIndices += mover.geometry.indices.size();
    }
    return shape;
}

/// IEEE 754 binary16 to float.
float halfToFloat(std::uint16_t half) noexcept {
    const std::uint32_t sign = (half >> 15) & 1u;
    const std::uint32_t exponent = (half >> 10) & 0x1Fu;
    const std::uint32_t mantissa = half & 0x3FFu;
    float magnitude;
    if (exponent == 0) {
        magnitude = std::ldexp(static_cast<float>(mantissa), -24);
    } else if (exponent == 31) {
        magnitude = mantissa == 0 ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
    } else {
        magnitude = std::ldexp(static_cast<float>(mantissa + 1024u), static_cast<int>(exponent) - 25);
    }
    return sign != 0 ? -magnitude : magnitude;
}

/// Whether `model` turns space inside out: its upper 3x3 has a negative
/// determinant, which a negative postScale on one axis gives. Such a model
/// reverses the screen winding of every triangle it places.
bool mirrors(const gpu::Mat4& m) noexcept {
    const double det = m[0] * (static_cast<double>(m[5]) * m[10] - static_cast<double>(m[9]) * m[6])
                       - m[4] * (static_cast<double>(m[1]) * m[10] - static_cast<double>(m[9]) * m[2])
                       + m[8] * (static_cast<double>(m[1]) * m[6] - static_cast<double>(m[5]) * m[2]);
    return det < 0;
}

using Box = std::array<std::array<float, 3>, 2>;

/// `local` carried by `model` and boxed again in world space.
Box worldBox(const Box& local, const gpu::Mat4& model) noexcept {
    Box out{{{INFINITY, INFINITY, INFINITY}, {-INFINITY, -INFINITY, -INFINITY}}};
    for (int corner = 0; corner < 8; ++corner) {
        const std::array<float, 3> p{local[corner & 1][0], local[(corner >> 1) & 1][1], local[(corner >> 2) & 1][2]};
        for (int row = 0; row < 3; ++row) {
            const float v = model[0 * 4 + row] * p[0] + model[1 * 4 + row] * p[1] + model[2 * 4 + row] * p[2]
                            + model[3 * 4 + row];
            out[0][row] = std::min(out[0][row], v);
            out[1][row] = std::max(out[1][row], v);
        }
    }
    return out;
}

/// SS 4.11 provision 3: where a UI pass draws, at output resolution, into the
/// presented image rather than into anything an upscaler would consume. uui
/// does not exist yet, so the pass is empty; its position is the provision.
void compositeUi(VkCommandBuffer /*commands*/) {}

/// A global memory dependency: `srcAccess` at `srcStage` made visible to
/// `dstAccess` at `dstStage`. An image transition orders only its own image,
/// so a buffer or a GENERAL image written by one pass and read by the next
/// needs this (review, 2026-09-26).
void memoryBarrier(VkCommandBuffer commands, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                   VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands, &dependency);
}

/// The viewport and scissor over `extent` from the top-left corner.
void setViewportTo(VkCommandBuffer commands, VkExtent2D extent) {
    const VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
}

/// Which of the scene's passes recordBatches draws for.
enum class ScenePass : std::uint8_t { Depth, Opaque, Translucent };

/// The scene's targets -- colour, velocity, emission and depth -- over the
/// drawn region. Each pass after the first loads what the one before stored.
struct SceneTargets {
    std::array<VkRenderingAttachmentInfo, 3> colours{};
    VkRenderingAttachmentInfo depth{};
    VkRect2D area{};

    /// Rendering into the first `colourCount` colours and the depth. Points
    /// into this object, so it is used before this object moves.
    [[nodiscard]] VkRenderingInfo rendering(std::uint32_t colourCount) const {
        VkRenderingInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        info.renderArea = area;
        info.layerCount = 1;
        info.colorAttachmentCount = colourCount;
        info.pColorAttachments = colourCount != 0 ? colours.data() : nullptr;
        info.pDepthAttachment = &depth;
        return info;
    }
};

/// A view's camera transforms: SS 4.3's, and the jittered one it draws with.
struct ViewMatrices {
    gpu::Mat4 view;
    gpu::Mat4 viewProj;        ///< unjittered
    gpu::Mat4 drawnProjection; ///< jittered when the tier jitters
};

/// UTA-0015's per-frame fog constants, and how many glowing lights missed the cap.
struct FogSetup {
    gpu::FogConstants constants{};
    std::uint32_t droppedVolumeLights = 0;
};

/// When drawView's steps began and ended, for SS 6's stats.
struct FrameTimes {
    std::chrono::steady_clock::time_point viewStarted; ///< drawView entered
    std::chrono::steady_clock::time_point started;     ///< the GPU work submitted
    std::chrono::steady_clock::duration recording{};
    double milliseconds = 0; ///< submit to finished
};

} // namespace

struct Renderer::Impl {
    Config config;

    // Declared first, so destroyed last: everything below is made from it.
    std::unique_ptr<Gpu> gpu;
    std::unique_ptr<Pipelines> pipelines;
    /// SS 4.3's presenting path; null on the surfaceless one. After the Gpu, so
    /// destroyed before it and before its surface.
    std::unique_ptr<Swapchain> swapchain;

    VkSampler materialSampler = VK_NULL_HANDLE;
    VkSampler nearestSampler = VK_NULL_HANDLE;
    VkSampler shadowSampler = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet sceneSet = VK_NULL_HANDLE;
    VkDescriptorSet postSet = VK_NULL_HANDLE;
    /// UTA-0154: EASU's set reads the upscale input, RCAS's reads EASU's output.
    VkDescriptorSet easuSet = VK_NULL_HANDLE, rcasSet = VK_NULL_HANDLE;
    /// UTA-0053: the bloom chain's source sets -- the emission target's, then
    /// each level's.
    VkDescriptorSet emissionSet = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, BLOOM_LEVELS> bloomSets{};
    VkSampler bloomSampler = VK_NULL_HANDLE;

    Image hdr, velocity, depth, output;
    /// UTA-0154: FSR 1's input and EASU's output. Only a renderer whose scale
    /// can drop below 1 makes them.
    Image upscaleInput, upscaled;
    Image shadowAtlas;
    /// UTA-0053: the forward pass's emission, and the bloom chain built from it.
    Image emission;
    std::array<Image, BLOOM_LEVELS> bloom;

    Buffer frameData, objects, lights;
    Buffer clusterCounts, clusterIndices, clusterBounds;
    Buffer probeCells, probes;
    std::uint32_t probeSpacing = 0, probeCount = 0, probeTableMask = 0, probeLongestRun = 0;
    Buffer shadowFaces;
    /// What upload sized `lights` and `shadowFaces` for, in records; draw
    /// refuses a frame that would write past either (review-code 2026-10-15).
    std::size_t lightCapacity = 0, shadowFaceCapacity = 0;
    Buffer zones; ///< UTA-0156 SS 4.4
    /// UTA-0263 SS 4.4: the camera-facing flames, one zero record when there
    /// are none so the binding is never empty, and how many to draw.
    Buffer flames;
    std::uint32_t flameCount = 0;
    /// UTA-0015 SS 4.3 and SS 4.4: the fog volume's images, one texel each below
    /// its tier; the volumetric lights a frame glows; the fog stages' set; and
    /// each drawn light's zone, found once per upload. UTA-0333: the haze's
    /// copy of the shadow atlas, one texel below the fog's tier. UTA-0356: the
    /// last frame's scattering, and how many frames it holds since a cut.
    Image fogScattering, fogIntegrated, hazeShadow, fogHistory;
    std::uint64_t fogFrames = 0;
    Buffer volumeLightIndices;
    VkDescriptorSet fogSet = VK_NULL_HANDLE;
    std::vector<std::uint8_t> lightZones;
    ShadowPlanner shadowPlanner;
    /// UTA-0322: per directLights light, whether a surface reads its tiles up
    /// close -- one of its SMSK pairs is moverReach. Empty with no SMSK, which
    /// marks every light near.
    std::vector<bool> nearLights;
    /// UTA-0163: where the level's sky is drawn from, its six faces three
    /// across and two down, whether a draw has still to fill them, and whether
    /// they are filled. The texture is the last in the scene set's array.
    std::optional<SkyView> skyView;
    Image sky;
    /// UTA-0164 SS 4.5: the level's occlusion atlas, empty when the bundle has
    /// no AOCC or the tier draws none. The array's texture after the sky's.
    Image occlusion;
    /// UTA-0326 SS 4.5: the level's shadow mask atlas, empty when the bundle
    /// has no SMSK; the array's texture after the occlusion atlas. Its charts
    /// and pairs hold one zero record each with no SMSK, so neither is empty.
    Image shadowMask;
    Buffer maskCharts;
    Buffer maskPairs;
    bool skyPending = false;
    bool skyReady = false;
    /// UTA-0337: the sky zone's panned sheets, and each one's light, captured
    /// as the sky's faces are; the array's last textures, after the mask. While
    /// the faces are captured, skyCapturing is set and skyLayerCapture says
    /// which: 0 the sky without its layers, k + 1 layer k's light alone.
    SkyLayers skyLayers;
    /// UTA-0337 part 3: outdoorExtent of the level, the outdoor haze's scale.
    float outdoorExtent = 0;
    std::vector<Image> skyLayerLights;
    bool skyCapturing = false;
    std::uint32_t skyLayerCapture = 0;
    /// Each mover's box in its own pivot space, for SS 4.8's redraw test.
    std::vector<Box> moverBounds;

    BundleShape shape;
    std::optional<MaterialSet> materials;
    std::optional<SceneGeometry> geometry;

    std::optional<gpu::Mat4> previousViewProj;
    std::optional<float> previousFlameSeconds; // UTA-0055 SS 4.5, cleared with previousViewProj
    std::vector<gpu::Mat4> previousModels;
    /// Per object, whether its model reverses winding -- SS 4.5's mirrored mover.
    std::vector<bool> mirrored;
    bool jitter = false;
    /// A `resize` the next draw has still to apply.
    bool resized = false;
    std::uint64_t frameIndex = 0;
    /// SS 4.9's clock: a flickering light's phase is measured from here.
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    /// UTA-0352: how long the last draw's bundle check took, upload included;
    /// FrameStats::prepareMilliseconds counts it.
    std::chrono::steady_clock::duration bundleCheck{};
    /// Set: every frame draws at this light time instead of reading the clock
    /// (Renderer::pinLightSeconds). UTA-0191 draws one view twice and needs the
    /// two to agree on a pulsing light's phase.
    std::optional<double> pinnedLightSeconds;
    std::optional<double> pinnedRenderScale; // UTA-0372
    /// The light time the last frame was drawn at, which UTA-0191's capture
    /// records. Kept rather than recomputed: reading the clock again would
    /// return a later time than the frame actually used.
    double lastLightSeconds = 0;
    /// UTA-0276: the camera's zone's pan speeds, u and v, which scale every
    /// panning batch this frame -- the original pans by the viewer's zone.
    std::array<float, 2> cameraPanSpeed{1, 1};
    /// UTA-0215: the camera's zone, whose water flag and tint the output stage applies.
    ubundle::Zone viewZone{};
    bool drawn = false;
    /// The last draw() drew nothing -- minimised, out of date, or failed -- so
    /// the targets hold an older frame (UTA-0228 L5).
    bool lastDrawSkipped = false;
    /// A failed Swapchain::create retired the current handle (UTA-0228 L2).
    bool swapchainRetired = false;
    FrameStats stats;
    /// UTA-0051 SS 4.3: chosen once, at create.
    Tier tier = Tier::Low;
    /// UTA-0051 SS 4.4: the scale dynamic resolution draws the next frame at,
    /// the scale the last frame was drawn at, and the averaged frame time the
    /// controller reads -- which is where its smoothing lives (INV-5).
    double renderScale = 1;
    double drawnScale = 1;
    double averagedMilliseconds = 0;
    /// UTA-0351: GPU_PASS_COUNT + 1 timestamps a frame, the steps' bounds;
    /// null on a queue that keeps none. A tick's length, and the bits it keeps.
    VkQueryPool passTimes = VK_NULL_HANDLE;
    double timestampNanoseconds = 0;
    std::uint64_t timestampMask = 0;

    ~Impl() {
        if (!gpu) return;
        const VkDevice device = gpu->device();
        vkDeviceWaitIdle(device);
        if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, pool, nullptr);
        if (passTimes != VK_NULL_HANDLE) vkDestroyQueryPool(device, passTimes, nullptr);
        for (VkSampler sampler : {materialSampler, nearestSampler, shadowSampler, bloomSampler})
            if (sampler != VK_NULL_HANDLE) vkDestroySampler(device, sampler, nullptr);
    }

    Result<void> createTargets();
    Result<void> createShadowAtlas();

    /// SS 4.3: the targets follow the swapchain's extent, which the surface may
    /// decide. True when that changed the target size.
    bool adoptSwapchainExtent() {
        if (!swapchain || swapchain->empty()) return false;
        const VkExtent2D extent = swapchain->extent();
        if (extent.width == config.width && extent.height == config.height) return false;
        config.width = extent.width;
        config.height = extent.height;
        return true;
    }
    Result<void> createSamplers();
    Result<void> createPassTimes();
    Result<void> createStandIns();
    Result<void> createFogVolumes();
    Result<void> upload(const ubundle::Bundle& bundle);
    Result<void> uploadAtlas(Image& image, std::uint32_t width, std::uint32_t height,
                             std::span<const std::uint8_t> texels);
    Result<void> uploadShadowMask(const ubundle::Bundle& bundle);
    Result<void> writeDescriptors();
    /// The texture slots of what upload made, by which images exist.
    [[nodiscard]] TextureSlots textureSlots() const {
        return textureSlotsOf(materials->textures().size(), sky.view() != VK_NULL_HANDLE,
                              occlusion.view() != VK_NULL_HANDLE, shadowMask.view() != VK_NULL_HANDLE,
                              skyLayerLights.size());
    }
    /// One view of `bundle` from `camera`, presented to `image` when there is
    /// one. With `skyFace`, UTA-0163's capture: drawn at scale 1 with no sky, and
    /// the target's central square copied into that face of the sky texture.
    Result<void> drawView(const ubundle::Bundle& bundle, const Camera& camera, std::optional<std::uint32_t> image,
                          std::optional<std::uint32_t> skyFace);

    // drawView's steps, in the order it takes them.
    [[nodiscard]] double scaleFor(bool skyFace, const TierSettings& settings) const;
    [[nodiscard]] gpu::FrameData frameDataOf(const ubundle::Bundle& bundle, const Camera& camera, VkExtent2D region,
                                             const ViewMatrices& matrices, double seconds, bool skyFace);
    [[nodiscard]] std::vector<gpu::Mat4> placeObjects(const ubundle::Bundle& bundle);
    [[nodiscard]] ShadowPlan planShadows(const ubundle::Bundle& bundle, std::span<const gpu::Mat4> models);
    std::uint32_t placeLights(gpu::FrameData& frame, std::vector<gpu::Light>& frameLights, const ShadowPlan& plan,
                              const Camera& camera);
    std::uint8_t placeViewZone(gpu::FrameData& frame, const ubundle::Bundle& bundle, const Camera& camera);
    void placeSkyLayers(gpu::FrameData& frame, const ubundle::Bundle& bundle, const Camera& camera,
                        VkExtent2D region, bool skyFace);
    [[nodiscard]] FogSetup fogSetupOf(const ubundle::Bundle& bundle, const Camera& camera, VkExtent2D region,
                                      std::span<const gpu::Light> frameLights, std::uint8_t cameraZone,
                                      std::uint32_t flashlight, bool skyFace);
    Result<void> uploadFrame(gpu::FrameData& frame, std::span<const gpu::Light> frameLights, const ShadowPlan& plan);
    void recordSkyFaceCopy(VkCommandBuffer commands, VkExtent2D region, std::uint32_t skyFace);
    void readStats(const ShadowPlan& plan, std::uint32_t droppedVolumeLights, double scale, const FrameTimes& times,
                   bool skyFace);

    void recordFrame(VkCommandBuffer commands, const ShadowPlan& shadows, VkExtent2D region,
                     const gpu::FogConstants& fog);

    // recordFrame's stages, in the order it records them.
    void stampPassEnd(VkCommandBuffer commands, GpuPass pass);
    void recordShadowTiles(VkCommandBuffer commands, const ShadowPlan& shadows);
    void recordClusterCulling(VkCommandBuffer commands);
    void recordFogVolume(VkCommandBuffer commands, const gpu::FogConstants& fog);
    [[nodiscard]] SceneTargets prepareSceneTargets(VkCommandBuffer commands, VkExtent2D region);
    void recordDepthPrePass(VkCommandBuffer commands, SceneTargets& targets, VkExtent2D region);
    void recordOpaque(VkCommandBuffer commands, const SceneTargets& targets, VkExtent2D region);
    void recordTranslucent(VkCommandBuffer commands, SceneTargets& targets, VkExtent2D region);
    void recordFlames(VkCommandBuffer commands, SceneTargets& targets, VkExtent2D region);
    [[nodiscard]] bool skyCaptureLeavesOut(const DrawItem& item, ScenePass pass) const;
    void recordBatches(VkCommandBuffer commands, ScenePass pass, VkExtent2D region);
    void recordBloom(VkCommandBuffer commands, VkExtent2D region);
    void recordBloomLevel(VkCommandBuffer commands, Image& target, VkPipeline pipeline, VkDescriptorSet sourceSet,
                          const Image& source, std::uint32_t mode, std::array<float, 2> drawnShare);
    void recordOutput(VkCommandBuffer commands, VkExtent2D region);
    void recordFullTargetPass(VkCommandBuffer commands, Image& target, VkPipeline pipeline, VkDescriptorSet set,
                              bool upscaleInputPass, VkExtent2D region);
};

/// Every target the window's size decides. SS 4.3's resize rebuilds them.
Result<void> Renderer::Impl::createTargets() {
    const std::uint32_t w = config.width, h = config.height;
    // UTA-0163: a transfer source too, for the sky's faces.
    UTA_TRY(hdr, Image::create(*gpu, {HDR_FORMAT, w, h, 1,
                                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT}));
    UTA_TRY(velocity, Image::create(*gpu, {VELOCITY_FORMAT, w, h, 1,
                                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT}));
    UTA_TRY(depth, Image::create(*gpu, {DEPTH_FORMAT, w, h, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT}));
    UTA_TRY(output, Image::create(*gpu, {OUTPUT_FORMAT, w, h, 1,
                                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT}));
    // UTA-0053: the emission target at the output's size, and the bloom chain
    // from half that down.
    const VkImageUsageFlags bloomUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    UTA_TRY(emission, Image::create(*gpu, {HDR_FORMAT, w, h, 1, bloomUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT}));
    for (std::size_t k = 0; k < BLOOM_LEVELS; ++k) {
        UTA_TRY(bloom[k], Image::create(*gpu, {HDR_FORMAT, std::max<std::uint32_t>(1, w >> (k + 1)),
                                               std::max<std::uint32_t>(1, h >> (k + 1)), 1, bloomUsage}));
    }
    // UTA-0154: at the output's size, so a change of scale makes no image
    // (UTA-0051 SS 4.4).
    if (config.dynamicResolution || config.fixedRenderScale) {
        UTA_TRY(upscaleInput, Image::create(*gpu, {HDR_FORMAT, w, h, 1,
                                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT}));
        UTA_TRY(upscaled, Image::create(*gpu, {HDR_FORMAT, w, h, 1,
                                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT}));
    }
    return {};
}

/// SS 4.8: one depth atlas for every shadowing light. No window decides its
/// size, so a resize keeps it and the tiles cached in it. Moved to its sampled
/// layout at once; no tile is read before a frame has drawn it.
Result<void> Renderer::Impl::createShadowAtlas() {
    const std::uint32_t side = shadowPlanner.detail().atlasSize; // UTA-0175: the tier's
    UTA_TRY(shadowAtlas, Image::create(*gpu, {DEPTH_FORMAT, side, side, 1,
                                              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                                                  | VK_IMAGE_USAGE_TRANSFER_DST_BIT}));
    // Cleared to the far plane, which shadows nothing, so no tile is ever read
    // holding whatever the allocation held.
    return gpu->run([&](VkCommandBuffer commands) {
        shadowAtlas.transition(commands, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkClearDepthStencilValue far{1.0f, 0};
        const VkImageSubresourceRange depth{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        vkCmdClearDepthStencilImage(commands, shadowAtlas.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &far, 1,
                                    &depth);
        shadowAtlas.transition(commands, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
    });
}

Result<void> Renderer::Impl::createSamplers() {
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    // SS 4.11: this item renders at output resolution and applies no mip bias.
    info.mipLodBias = 0.0f;
    info.maxLod = VK_LOD_CLAMP_NONE;
    UTA_CHECK(created(vkCreateSampler(gpu->device(), &info, nullptr, &materialSampler), materialSampler, "vkCreateSampler"));

    info.magFilter = VK_FILTER_NEAREST;
    info.minFilter = VK_FILTER_NEAREST;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxLod = 0.0f;
    UTA_CHECK(created(vkCreateSampler(gpu->device(), &info, nullptr, &nearestSampler), nearestSampler, "vkCreateSampler"));

    // UTA-0053: the bloom chain filters, and never wraps past an edge.
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    UTA_CHECK(created(vkCreateSampler(gpu->device(), &info, nullptr, &bloomSampler), bloomSampler, "vkCreateSampler"));

    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.compareEnable = VK_TRUE;
    info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    return created(vkCreateSampler(gpu->device(), &info, nullptr, &shadowSampler), shadowSampler, "vkCreateSampler");
}

Result<void> Renderer::Impl::createPassTimes() {
    std::uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu->physical(), &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu->physical(), &familyCount, families.data());
    const std::uint32_t validBits = families.at(gpu->queueFamily()).timestampValidBits;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(gpu->physical(), &properties);
    if (validBits == 0 || !(properties.limits.timestampPeriod > 0)) {
        UTA_LOG(logRender, LogLevel::Info, "the queue keeps no timestamps, so no step of a frame is timed");
        return {};
    }
    timestampNanoseconds = properties.limits.timestampPeriod;
    timestampMask = validBits >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << validBits) - 1;
    VkQueryPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = GPU_PASS_COUNT + 1;
    return created(vkCreateQueryPool(gpu->device(), &info, nullptr, &passTimes), passTimes, "vkCreateQueryPool");
}

Result<void> Renderer::Impl::createStandIns() {
    const VkBufferUsageFlags storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    UTA_TRY(frameData, Buffer::create(*gpu, sizeof(gpu::FrameData), storage, BufferMemory::HostWrite));
    // SS 4.6: the grid is fixed, so its buffers are sized once. The counts are
    // read back each frame for SS 6's overflow count; the indices only the GPU
    // touches (UTA-0209).
    UTA_TRY(clusterCounts,
            Buffer::create(*gpu, sizeof(std::uint32_t) * gpu::CLUSTER_COUNT, storage, BufferMemory::HostRead));
    UTA_TRY(clusterIndices, Buffer::create(*gpu, sizeof(std::uint32_t) * gpu::CLUSTER_COUNT * gpu::CLUSTER_CAPACITY,
                                           storage, BufferMemory::Device));
    UTA_TRY(clusterBounds, Buffer::create(*gpu, sizeof(gpu::ClusterBounds) * gpu::CLUSTER_COUNT, storage,
                                          BufferMemory::HostWrite));
    // UTA-0015 SS 4.4: VOLUME_LIGHTS, sized for its capacity.
    UTA_TRY(volumeLightIndices, Buffer::create(*gpu, sizeof(std::uint32_t) * VOLUME_LIGHT_CAPACITY, storage,
                                               BufferMemory::HostWrite));
    return {};
}

/// UTA-0015 SS 4.3: no window decides the grid, so a resize keeps both images.
/// Cleared once to no fog, which is all a tier below the fog's ever reads.
Result<void> Renderer::Impl::createFogVolumes() {
    const bool on = enabled(Feature::VolumetricFog, tier);
    const ImageDesc desc{VK_FORMAT_R16G16B16A16_SFLOAT,
                         on ? FOG_GRID[0] : 1,
                         on ? FOG_GRID[1] : 1,
                         1,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                             | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         on ? FOG_GRID[2] : 1};
    UTA_TRY(fogScattering, Image::create(*gpu, desc));
    UTA_TRY(fogIntegrated, Image::create(*gpu, desc));
    UTA_TRY(fogHistory, Image::create(*gpu, desc));
    const std::uint32_t hazeSide = on ? shadowPlanner.detail().atlasSize / HAZE_SHADOW_STEP : 1;
    UTA_TRY(hazeShadow, Image::create(*gpu, {VK_FORMAT_R32_SFLOAT, hazeSide, hazeSide, 1,
                                             VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT}));
    return gpu->run([&](VkCommandBuffer commands) {
        const VkClearColorValue noFog{{0.0f, 0.0f, 0.0f, 1.0f}};
        const VkImageSubresourceRange everything{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        for (Image* image : {&fogScattering, &fogIntegrated, &fogHistory}) {
            image->transition(commands, VK_IMAGE_LAYOUT_GENERAL);
            vkCmdClearColorImage(commands, image->handle(), VK_IMAGE_LAYOUT_GENERAL, &noFog, 1, &everything);
        }
        // The far plane, as the atlas is cleared: no tile is read before a
        // frame has drawn it, and then copied it.
        const VkClearColorValue far{{1.0f, 0.0f, 0.0f, 0.0f}};
        hazeShadow.transition(commands, VK_IMAGE_LAYOUT_GENERAL);
        vkCmdClearColorImage(commands, hazeShadow.handle(), VK_IMAGE_LAYOUT_GENERAL, &far, 1, &everything);
    });
}

/// UTA-0164 SS 4.5 and UTA-0326 SS 4.5: an atlas as one R8 image of one
/// level, through a staging buffer.
Result<void> Renderer::Impl::uploadAtlas(Image& image, std::uint32_t width, std::uint32_t height,
                                         std::span<const std::uint8_t> texels) {
    // A Bundle built in memory has not been through ubundle::read's checks.
    if (width == 0 || height == 0 || texels.size() != std::size_t{width} * height)
        return fail(ErrorCode::InvalidArgument,
                    std::format("an atlas of {}x{} holds {} texels", width, height, texels.size()));
    UTA_TRY(image, Image::create(*gpu, {VK_FORMAT_R8_UNORM, width, height, 1,
                                        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT}));
    UTA_TRY(Buffer staging, Buffer::create(*gpu, texels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                           BufferMemory::HostWrite));
    std::memcpy(staging.mapped(), texels.data(), texels.size());
    return gpu->run([&](VkCommandBuffer commands) {
        image.transition(commands, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(commands, staging.handle(), image.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &copy);
        image.transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
}

/// UTA-0326 SS 4.5: SMSK's charts and pairs, each pair's light as its index in
/// the frame's light buffer and a pair whose light the frame does not draw left
/// out; then its atlas. With no SMSK, one zero record each and no atlas.
Result<void> Renderer::Impl::uploadShadowMask(const ubundle::Bundle& bundle) {
    shadowMask = Image{};
    nearLights.clear();
    std::vector<gpu::MaskChart> charts;
    std::vector<gpu::MaskPair> pairs;
    if (bundle.shadowMask) {
        const ubundle::ShadowMask& mask = *bundle.shadowMask;
        const std::vector<std::uint32_t> drawn = drawnIndices(bundle);
        nearLights.assign(directLights(bundle).size(), false);
        for (const ubundle::MaskChart& chart : mask.charts) {
            if (std::uint64_t{chart.firstPair} + chart.pairCount > mask.pairs.size())
                return fail(ErrorCode::InvalidArgument, "an SMSK chart's pairs pass the end of its pairs");
            gpu::MaskChart placed{static_cast<std::uint32_t>(pairs.size()), 0, chart.width, chart.height};
            for (std::uint32_t k = chart.firstPair; k < chart.firstPair + chart.pairCount; ++k) {
                const ubundle::MaskPair& pair = mask.pairs[k];
                if (pair.light >= drawn.size())
                    return fail(ErrorCode::InvalidArgument,
                                std::format("an SMSK pair names light {} of LITE's {}", pair.light, drawn.size()));
                if (drawn[pair.light] == gpu::NONE) continue;
                pairs.push_back({drawn[pair.light], pair.x, pair.y, pair.moverReach});
                if (pair.moverReach != 0) nearLights[drawn[pair.light]] = true;
                ++placed.pairCount;
            }
            charts.push_back(placed);
        }
        UTA_CHECK(uploadAtlas(shadowMask, mask.width, mask.height, mask.texels));
    }
    if (charts.empty()) charts.push_back({});
    if (pairs.empty()) pairs.push_back({});
    UTA_TRY(maskCharts, Buffer::upload(*gpu, std::as_bytes(std::span(charts)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    UTA_TRY(maskPairs, Buffer::upload(*gpu, std::as_bytes(std::span(pairs)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    return {};
}

Result<void> Renderer::Impl::upload(const ubundle::Bundle& bundle) {
    geometry.reset();
    materials.reset();
    UTA_TRY(MaterialSet uploadedMaterials, MaterialSet::upload(*gpu, bundle, config.showMissingMaterials));
    materials.emplace(std::move(uploadedMaterials));
    // UTA-0163: a level with a sky takes one texture more, its faces.
    skyView = skyViewOf(bundle);
    // UTA-0164 SS 4.5: and one more for its occlusion atlas, from the tier up.
    const bool occluded = bundle.occlusion.has_value() && enabled(Feature::AmbientOcclusion, tier);
    // UTA-0326 SS 4.5: and one more for its shadow mask, at every tier.
    // UTA-0337: and one more for each sky layer's light.
    skyLayers = skyView ? skyLayersOf(bundle) : SkyLayers{};
    const std::uint32_t textureCount = textureSlotsOf(materials->textures().size(), skyView.has_value(), occluded,
                                                      bundle.shadowMask.has_value(), skyLayers.layers.size())
                                           .count;
    if (textureCount > pipelines->textureCapacity())
        return fail(ErrorCode::InvalidArgument,
                    std::format("the bundle needs {} textures and {} binds at most {}", textureCount, gpu->name(),
                                pipelines->textureCapacity()));
    sky = Image{};
    skyLayerLights.clear();
    skyReady = false;
    skyPending = skyView.has_value();
    if (skyView) {
        const ImageDesc faces{HDR_FORMAT, SKY_FACE_SIZE * SKY_COLUMNS, SKY_FACE_SIZE * SKY_ROWS, 1,
                              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
        UTA_TRY(sky, Image::create(*gpu, faces));
        for (std::size_t i = 0; i < skyLayers.layers.size(); ++i) {
            UTA_TRY(Image light, Image::create(*gpu, faces));
            skyLayerLights.push_back(std::move(light));
        }
        UTA_CHECK(gpu->run([&](VkCommandBuffer commands) {
            sky.transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            for (Image& light : skyLayerLights) light.transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }));
    }
    occlusion = Image{};
    if (occluded)
        UTA_CHECK(uploadAtlas(occlusion, bundle.occlusion->width, bundle.occlusion->height, bundle.occlusion->texels));
    UTA_CHECK(uploadShadowMask(bundle));
    UTA_TRY(SceneGeometry uploadedGeometry, SceneGeometry::upload(*gpu, bundle, *materials));
    geometry.emplace(std::move(uploadedGeometry));
    UTA_TRY(objects, Buffer::create(*gpu, sizeof(gpu::Object) * geometry->objectCount,
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemory::HostWrite));
    const std::vector<gpu::Light> uploaded = drawnLights(bundle, 0.0);
    // UTA-0015 SS 4.5: room for the flashlight after the bundle's lights.
    const std::size_t lightCount = uploaded.size() + 1;
    UTA_TRY(lights, Buffer::create(*gpu, sizeof(gpu::Light) * lightCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                   BufferMemory::HostWrite));
    lightCapacity = lightCount;
    // UTA-0015 SS 4.4: each drawn light's zone, which decides whether it may glow.
    lightZones.clear();
    const std::size_t zoneCount = bundle.zones ? bundle.zones->size() : 1;
    for (const gpu::Light& light : uploaded)
        lightZones.push_back(bundle.rooms ? ubundle::zoneAt(*bundle.rooms, light.location, zoneCount) : 0);

    // SS 4.7: no LPRB, or an empty one, is a table with no probes, which gives
    // zero indirect everywhere (SS 6).
    const ProbeTable table = probeTable(bundle.lightProbes);
    UTA_TRY(probeCells, Buffer::upload(*gpu, std::as_bytes(std::span(table.cells)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    UTA_TRY(probes, Buffer::upload(*gpu, std::as_bytes(std::span(table.probes)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    probeSpacing = table.spacing;
    probeCount = static_cast<std::uint32_t>(table.probes.size());
    probeTableMask = table.tableMask;
    probeLongestRun = table.longestRun;

    // UTA-0156 SS 4.4: a record per ZONE entry, or one zero record with no ZONE,
    // so the binding is never empty and zone 0 always reads. UTA-0089 SS 4.1:
    // each says whether the level's own geometry has a sky window in it; a
    // mover's does not count.
    std::vector<gpu::Zone> zoneRecords;
    outdoorExtent = 0;
    if (bundle.zones) {
        const std::vector<std::uint8_t> sky =
            bundle.geometry ? zonesSeeingSky(*bundle.geometry, bundle.zones->size())
                            : std::vector<std::uint8_t>(bundle.zones->size(), 0);
        for (std::size_t i = 0; i < bundle.zones->size(); ++i) {
            const ubundle::Zone& zone = (*bundle.zones)[i];
            zoneRecords.push_back(gpu::Zone{zone.brightness, zone.hue, zone.saturation, sky[i], zone.water});
        }
        if (bundle.geometry) outdoorExtent = urender::outdoorExtent(*bundle.geometry, sky);
    }
    if (zoneRecords.empty()) zoneRecords.push_back(gpu::Zone{});
    UTA_TRY(zones, Buffer::upload(*gpu, std::as_bytes(std::span(zoneRecords)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));

    // UTA-0263 SS 4.4: each FLAM record whose MATS record has a flame look,
    // which ubundle's validation already guarantees of every one.
    std::vector<gpu::FlameInstance> flameRecords;
    if (bundle.flames)
        for (const ubundle::Flame& flame : *bundle.flames)
            if (const std::uint32_t ramp = materials->rampOf(flame.material); ramp != gpu::NONE)
                flameRecords.push_back({flame.base, flame.width, flame.height, flame.seed, ramp, 0});
    flameCount = static_cast<std::uint32_t>(flameRecords.size());
    if (flameRecords.empty()) flameRecords.push_back(gpu::FlameInstance{});
    UTA_TRY(flames, Buffer::upload(*gpu, std::as_bytes(std::span(flameRecords)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));

    // SS 4.8: no tile shows this bundle's geometry yet, and each light holds at
    // most six faces.
    shadowPlanner.reset();
    // UTA-0163: and the sky's six after them.
    UTA_TRY(shadowFaces, Buffer::create(*gpu, sizeof(gpu::ShadowFace) * (lightCount * 6 + 6),
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemory::HostWrite));
    shadowFaceCapacity = lightCount * 6 + 6;
    moverBounds.clear();
    if (bundle.movers) {
        for (const ubundle::MoverShape& mover : *bundle.movers) {
            Box box{{{INFINITY, INFINITY, INFINITY}, {-INFINITY, -INFINITY, -INFINITY}}};
            for (const ubundle::GeometryVertex& vertex : mover.geometry.vertices)
                for (int axis = 0; axis < 3; ++axis) {
                    box[0][axis] = std::min(box[0][axis], vertex.position[axis]);
                    box[1][axis] = std::max(box[1][axis], vertex.position[axis]);
                }
            if (mover.geometry.vertices.empty()) box = {};
            moverBounds.push_back(box);
        }
    }
    previousModels.clear();
    return writeDescriptors();
}

Result<void> Renderer::Impl::writeDescriptors() {
    const VkDevice device = gpu->device();
    if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, pool, nullptr);
    pool = VK_NULL_HANDLE;

    const TextureSlots slots = textureSlots();
    const std::uint32_t textureCount = slots.count;
    const std::array sizes = {
        // The scene set's, then UTA-0015's VOLUME_LIGHTS.
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, gpu::MASK_PAIRS + 1 + 1},
        // The atlas and the three post sets' sources, then UTA-0053's: bloom in
        // each post set, and one source per bloom step; then UTA-0015's fog
        // volume and UTA-0356's fog history.
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                             textureCount + 4 + 3 + 1 + static_cast<std::uint32_t>(BLOOM_LEVELS) + 1 + 1},
        // UTA-0015: the fog set's two images; UTA-0333's haze shadow.
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 4 + 1 + static_cast<std::uint32_t>(BLOOM_LEVELS) + 1;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    poolInfo.pPoolSizes = sizes.data();
    UTA_CHECK(created(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool), pool, "vkCreateDescriptorPool"));

    VkDescriptorSetVariableDescriptorCountAllocateInfo variable{};
    variable.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    variable.descriptorSetCount = 1;
    variable.pDescriptorCounts = &textureCount;
    const VkDescriptorSetLayout sceneLayout = pipelines->sceneSetLayout();
    VkDescriptorSetAllocateInfo sceneAllocate{};
    sceneAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    sceneAllocate.pNext = &variable;
    sceneAllocate.descriptorPool = pool;
    sceneAllocate.descriptorSetCount = 1;
    sceneAllocate.pSetLayouts = &sceneLayout;
    UTA_CHECK(check(vkAllocateDescriptorSets(device, &sceneAllocate, &sceneSet), "vkAllocateDescriptorSets (scene)"));

    // The post set, then UTA-0154's EASU and RCAS sets, all one layout.
    const std::array<VkDescriptorSetLayout, 3> postLayouts{pipelines->postSetLayout(), pipelines->postSetLayout(),
                                                           pipelines->postSetLayout()};
    VkDescriptorSetAllocateInfo postAllocate{};
    postAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    postAllocate.descriptorPool = pool;
    postAllocate.descriptorSetCount = static_cast<std::uint32_t>(postLayouts.size());
    postAllocate.pSetLayouts = postLayouts.data();
    std::array<VkDescriptorSet, 3> postSets{};
    UTA_CHECK(check(vkAllocateDescriptorSets(device, &postAllocate, postSets.data()), "vkAllocateDescriptorSets (post)"));
    postSet = postSets[0];
    easuSet = postSets[1];
    rcasSet = postSets[2];

    // UTA-0053: one set per bloom source -- the emission target, then each level.
    std::array<VkDescriptorSetLayout, 1 + BLOOM_LEVELS> bloomLayouts{};
    bloomLayouts.fill(pipelines->bloomSetLayout());
    VkDescriptorSetAllocateInfo bloomAllocate = postAllocate;
    bloomAllocate.descriptorSetCount = static_cast<std::uint32_t>(bloomLayouts.size());
    bloomAllocate.pSetLayouts = bloomLayouts.data();
    std::array<VkDescriptorSet, 1 + BLOOM_LEVELS> bloomSources{};
    UTA_CHECK(check(vkAllocateDescriptorSets(device, &bloomAllocate, bloomSources.data()),
                    "vkAllocateDescriptorSets (bloom)"));
    emissionSet = bloomSources[0];
    for (std::size_t k = 0; k < BLOOM_LEVELS; ++k) bloomSets[k] = bloomSources[k + 1];

    // UTA-0015 SS 4.4: the fog stages' own set.
    const VkDescriptorSetLayout fogLayout = pipelines->fogSetLayout();
    VkDescriptorSetAllocateInfo fogAllocate = postAllocate;
    fogAllocate.descriptorSetCount = 1;
    fogAllocate.pSetLayouts = &fogLayout;
    UTA_CHECK(check(vkAllocateDescriptorSets(device, &fogAllocate, &fogSet), "vkAllocateDescriptorSets (fog)"));

    const std::array<const Buffer*, gpu::MASK_PAIRS + 1> buffers = {
        &frameData,     &objects,       &materials->records(), &lights,     &clusterCounts,
        &clusterIndices, &clusterBounds, &probeCells,          &probes,     &shadowFaces,
        &zones,          &flames,        &materials->ramps(),  &materials->liquids(),
        &maskCharts,     &maskPairs};
    std::array<VkDescriptorBufferInfo, gpu::MASK_PAIRS + 1> bufferInfos{};
    std::vector<VkWriteDescriptorSet> writes;
    for (std::uint32_t i = 0; i < buffers.size(); ++i) {
        bufferInfos[i] = {buffers[i]->handle(), 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = sceneSet;
        write.dstBinding = i;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &bufferInfos[i];
        writes.push_back(write);
    }

    const VkDescriptorImageInfo atlasInfo{shadowSampler, shadowAtlas.view(), VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet atlasWrite{};
    atlasWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    atlasWrite.dstSet = sceneSet;
    atlasWrite.dstBinding = gpu::SHADOW_ATLAS;
    atlasWrite.descriptorCount = 1;
    atlasWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    atlasWrite.pImageInfo = &atlasInfo;
    writes.push_back(atlasWrite);

    // UTA-0015 SS 4.3: the forward pass samples the integrated image, filtered
    // and clamped as the bloom chain is; the fog stages write both images.
    const VkDescriptorImageInfo fogVolumeInfo{bloomSampler, fogIntegrated.view(), VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet fogVolumeWrite = atlasWrite;
    fogVolumeWrite.dstBinding = gpu::FOG_VOLUME;
    fogVolumeWrite.pImageInfo = &fogVolumeInfo;
    writes.push_back(fogVolumeWrite);
    // The fog set, binding by binding (gpu::FogBinding).
    const auto fogWrite = [&](gpu::FogBinding binding, VkDescriptorType type, const VkDescriptorImageInfo* image) {
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = fogSet;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = type;
        write.pImageInfo = image;
        writes.push_back(write);
    };
    const VkDescriptorImageInfo scatteringInfo{VK_NULL_HANDLE, fogScattering.view(), VK_IMAGE_LAYOUT_GENERAL};
    fogWrite(gpu::FOG_SCATTERING, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &scatteringInfo);
    const VkDescriptorImageInfo integratedInfo{VK_NULL_HANDLE, fogIntegrated.view(), VK_IMAGE_LAYOUT_GENERAL};
    fogWrite(gpu::FOG_INTEGRATED, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &integratedInfo);
    const VkDescriptorImageInfo hazeShadowInfo{VK_NULL_HANDLE, hazeShadow.view(), VK_IMAGE_LAYOUT_GENERAL};
    fogWrite(gpu::FOG_HAZE_SHADOW, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &hazeShadowInfo);
    // UTA-0356: filtered, so a reprojected centre between texels reads both.
    const VkDescriptorImageInfo fogHistoryInfo{bloomSampler, fogHistory.view(), VK_IMAGE_LAYOUT_GENERAL};
    fogWrite(gpu::FOG_HISTORY, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &fogHistoryInfo);
    const VkDescriptorBufferInfo volumeLightInfo{volumeLightIndices.handle(), 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet volumeLightWrite{};
    volumeLightWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    volumeLightWrite.dstSet = fogSet;
    volumeLightWrite.dstBinding = gpu::FOG_VOLUME_LIGHTS;
    volumeLightWrite.descriptorCount = 1;
    volumeLightWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    volumeLightWrite.pBufferInfo = &volumeLightInfo;
    writes.push_back(volumeLightWrite);

    // Each texture written at its TextureSlots index, so this array cannot
    // disagree with the indices drawView hands the shaders.
    std::vector<VkDescriptorImageInfo> textureInfos(textureCount);
    const auto sampled = [this](const Image& image) {
        return VkDescriptorImageInfo{materialSampler, image.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    };
    for (std::size_t i = 0; i < materials->textures().size(); ++i) textureInfos[i] = sampled(materials->textures()[i]);
    if (slots.sky != gpu::NONE) textureInfos[slots.sky] = sampled(sky);
    if (slots.occlusion != gpu::NONE) textureInfos[slots.occlusion] = sampled(occlusion);
    if (slots.shadowMask != gpu::NONE) textureInfos[slots.shadowMask] = sampled(shadowMask);
    for (std::size_t i = 0; i < skyLayerLights.size(); ++i)
        textureInfos[slots.firstSkyLayerLight + i] = sampled(skyLayerLights[i]);
    VkWriteDescriptorSet textureWrite{};
    textureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    textureWrite.dstSet = sceneSet;
    textureWrite.dstBinding = gpu::TEXTURES;
    textureWrite.descriptorCount = textureCount;
    textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textureWrite.pImageInfo = textureInfos.data();
    writes.push_back(textureWrite);

    const VkDescriptorImageInfo hdrInfo{nearestSampler, hdr.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet hdrWrite{};
    hdrWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    hdrWrite.dstSet = postSet;
    hdrWrite.dstBinding = 0;
    hdrWrite.descriptorCount = 1;
    hdrWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    hdrWrite.pImageInfo = &hdrInfo;
    writes.push_back(hdrWrite);

    // UTA-0053: the post pass reads the bloom chain's top level, and each bloom
    // step reads its source.
    const VkDescriptorImageInfo bloomInfo{bloomSampler, bloom[0].view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet bloomWrite = hdrWrite;
    bloomWrite.dstBinding = 1;
    bloomWrite.pImageInfo = &bloomInfo;
    writes.push_back(bloomWrite);
    std::array<VkDescriptorImageInfo, 1 + BLOOM_LEVELS> sourceInfos{};
    sourceInfos[0] = {bloomSampler, emission.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    for (std::size_t k = 0; k < BLOOM_LEVELS; ++k)
        sourceInfos[k + 1] = {bloomSampler, bloom[k].view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    for (std::size_t i = 0; i < sourceInfos.size(); ++i) {
        VkWriteDescriptorSet sourceWrite = hdrWrite;
        sourceWrite.dstSet = i == 0 ? emissionSet : bloomSets[i - 1];
        sourceWrite.pImageInfo = &sourceInfos[i];
        writes.push_back(sourceWrite);
    }

    // UTA-0154: only a renderer that made the upscale images binds them.
    const VkDescriptorImageInfo easuInfo{nearestSampler, upscaleInput.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo rcasInfo{nearestSampler, upscaled.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    if (upscaleInput.handle() != VK_NULL_HANDLE) {
        VkWriteDescriptorSet easuWrite = hdrWrite;
        easuWrite.dstSet = easuSet;
        easuWrite.pImageInfo = &easuInfo;
        writes.push_back(easuWrite);
        VkWriteDescriptorSet rcasWrite = hdrWrite;
        rcasWrite.dstSet = rcasSet;
        rcasWrite.pImageInfo = &rcasInfo;
        writes.push_back(rcasWrite);
    }

    vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return {};
}

void Renderer::Impl::recordFrame(VkCommandBuffer commands, const ShadowPlan& shadows, VkExtent2D region,
                                 const gpu::FogConstants& fog) {
    // UTA-0351: a timestamp at the frame's start and at each step's end, once
    // everything before it is done. Query k + 1 is step k's end.
    if (passTimes != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(commands, passTimes, 0, GPU_PASS_COUNT + 1);
        vkCmdWriteTimestamp2(commands, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, passTimes, 0);
    }

    // Fires: UTA-0286 SS 4.4's pictures, before anything samples them.
    materials->recordFireUploads(commands);
    stampPassEnd(commands, GpuPass::Fires);

    recordShadowTiles(commands, shadows);
    stampPassEnd(commands, GpuPass::Shadows);

    recordClusterCulling(commands); // 0.
    stampPassEnd(commands, GpuPass::Clusters);

    recordFogVolume(commands, fog); // 0.5.
    stampPassEnd(commands, GpuPass::Fog);

    // 1. The forward pass: the depth pre-pass, then the opaque surfaces.
    SceneTargets targets = prepareSceneTargets(commands, region);
    recordDepthPrePass(commands, targets, region);
    stampPassEnd(commands, GpuPass::Depth);
    recordOpaque(commands, targets, region);
    stampPassEnd(commands, GpuPass::Opaque);

    recordTranslucent(commands, targets, region); // 2.
    stampPassEnd(commands, GpuPass::Translucent);

    recordFlames(commands, targets, region); // 2.5.
    stampPassEnd(commands, GpuPass::Flames);

    recordBloom(commands, region); // 3.
    stampPassEnd(commands, GpuPass::Bloom);

    recordOutput(commands, region); // 4.
    stampPassEnd(commands, GpuPass::Post);
    static_assert(GPU_PASS_COUNT == static_cast<std::size_t>(GpuPass::Post) + 1, "each GpuPass ends at one stamp above");

    // 5. The UI composite seam.
    compositeUi(commands);
}

void Renderer::Impl::stampPassEnd(VkCommandBuffer commands, GpuPass pass) {
    if (passTimes != VK_NULL_HANDLE)
        vkCmdWriteTimestamp2(commands, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, passTimes,
                             static_cast<std::uint32_t>(pass) + 1);
}

/// Shadow tiles: only those this frame's plan says to draw (SS 4.8). Run for
/// a level with no geometry too: the plan counts its tiles as drawn, so each
/// is at least cleared, and nothing it says is drawn is left stale.
void Renderer::Impl::recordShadowTiles(VkCommandBuffer commands, const ShadowPlan& shadows) {
    if (shadows.draws.empty()) return;
    shadowAtlas.transition(commands, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo atlasAttachment{};
    atlasAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    atlasAttachment.imageView = shadowAtlas.view();
    atlasAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    atlasAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // the kept tiles stay
    atlasAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo shadowPass{};
    shadowPass.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    shadowPass.renderArea = {{0, 0}, {shadowPlanner.detail().atlasSize, shadowPlanner.detail().atlasSize}};
    shadowPass.layerCount = 1;
    shadowPass.pDepthAttachment = &atlasAttachment;
    vkCmdBeginRendering(commands, &shadowPass);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->shadow());
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->shadowLayout(), 0, 1, &sceneSet, 0,
                            nullptr);
    if (!geometry->draws.empty()) {
        const VkBuffer vertexBuffer = geometry->vertices.handle();
        const VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(commands, 0, 1, &vertexBuffer, &zero);
        vkCmdBindIndexBuffer(commands, geometry->indices.handle(), 0, VK_INDEX_TYPE_UINT32);
    }
    for (const ShadowDraw& draw : shadows.draws) {
        const VkRect2D tile{{static_cast<std::int32_t>(draw.tile.x), static_cast<std::int32_t>(draw.tile.y)},
                            {draw.tile.size, draw.tile.size}};
        VkClearAttachment clear{};
        clear.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        clear.clearValue.depthStencil = {1.0f, 0};
        const VkClearRect clearRect{tile, 0, 1};
        vkCmdClearAttachments(commands, 1, &clear, 1, &clearRect);
        const VkViewport tileViewport{static_cast<float>(draw.tile.x), static_cast<float>(draw.tile.y),
                                      static_cast<float>(draw.tile.size), static_cast<float>(draw.tile.size), 0, 1};
        vkCmdSetViewport(commands, 0, 1, &tileViewport);
        vkCmdSetScissor(commands, 0, 1, &tile);
        for (const DrawItem& item : geometry->draws) {
            // What blocks light: opaque and masked surfaces. A translucent
            // or modulated one lets it through, as ubake's SurfaceRays does
            // (UTA-0271), the sky is not in the level, and UT99's lighting
            // lets light through a non-solid brush (UTA-0168).
            if ((item.polyFlags & (gpu::PF_TRANSLUCENT | gpu::PF_MODULATED | gpu::PF_NOT_SOLID | gpu::PF_FAKE_BACKDROP))
                != 0)
                continue;
            const gpu::ShadowConstants constants{shadows.faces[draw.face].viewProj, item.objectIndex,
                                                 item.materialIndex, item.polyFlags, 0};
            vkCmdPushConstants(commands, pipelines->shadowLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants),
                               &constants);
            vkCmdDrawIndexed(commands, item.indexCount, 1, item.firstIndex, item.firstVertex, 0);
        }
    }
    vkCmdEndRendering(commands);
    shadowAtlas.transition(commands, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
    // The transition's scope ends at the fragment stage; the fog's scatter
    // pass samples the atlas from compute.
    memoryBarrier(commands, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    // UTA-0333: each tile just drawn, copied into the haze's quarter-size
    // atlas, which the fog's scatter pass reads.
    if (enabled(Feature::VolumetricFog, tier)) {
        const std::array<VkDescriptorSet, 2> sets{sceneSet, fogSet};
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->hazeShadow());
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->fogLayout(), 0,
                                static_cast<std::uint32_t>(sets.size()), sets.data(), 0, nullptr);
        for (const ShadowDraw& draw : shadows.draws) {
            const gpu::ReduceConstants tile{draw.tile.x, draw.tile.y, draw.tile.size, 0};
            vkCmdPushConstants(commands, pipelines->fogLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(tile), &tile);
            const std::uint32_t groups = (draw.tile.size / HAZE_SHADOW_STEP + 7) / 8; // local size 8 x 8
            vkCmdDispatch(commands, groups, groups, 1);
        }
        memoryBarrier(commands, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    }
}

/// 0. Clustered light culling, a compute pass (SS 4.6).
void Renderer::Impl::recordClusterCulling(VkCommandBuffer commands) {
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->clusters());
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->sceneLayout(), 0, 1, &sceneSet, 0,
                            nullptr);
    vkCmdDispatch(commands, (gpu::CLUSTER_COUNT + 31) / 32, 1, 1); // cluster.comp's local size is 32
    // The culled lists are read by the fog's compute passes and the forward pass.
    memoryBarrier(commands, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

/// 0.5. The fog volume (UTA-0015 SS 4.3): each froxel's light and extinction
/// from the lists just culled, then each column integrated front to back.
/// Both images stay in GENERAL, which the forward pass samples. The second
/// pipeline shares the layout, so the sets and the constants stay bound.
void Renderer::Impl::recordFogVolume(VkCommandBuffer commands, const gpu::FogConstants& fog) {
    if (!enabled(Feature::VolumetricFog, tier)) return;
    const std::array<VkDescriptorSet, 2> fogSets{sceneSet, fogSet};
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->fogScatter());
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->fogLayout(), 0,
                            static_cast<std::uint32_t>(fogSets.size()), fogSets.data(), 0, nullptr);
    vkCmdPushConstants(commands, pipelines->fogLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(fog), &fog);
    // fog_scatter.comp's local size is 4 x 4 x 4; fog_integrate.comp's 8 x 8.
    vkCmdDispatch(commands, (FOG_GRID[0] + 3) / 4, (FOG_GRID[1] + 3) / 4, (FOG_GRID[2] + 3) / 4);
    // Scattering is written above and read by the integration and the copy
    // below; the scatter's read of the history ends before the copy writes it.
    memoryBarrier(commands, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
                  VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->fogIntegrate());
    vkCmdDispatch(commands, (FOG_GRID[0] + 7) / 8, (FOG_GRID[1] + 7) / 8, 1);
    // UTA-0356: this frame's scattering is the next frame's history. The
    // next scatter reads the copy, and writes scattering only after it is read.
    VkImageCopy copy{};
    copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.dstSubresource = copy.srcSubresource;
    copy.extent = {FOG_GRID[0], FOG_GRID[1], FOG_GRID[2]};
    vkCmdCopyImage(commands, fogScattering.handle(), VK_IMAGE_LAYOUT_GENERAL, fogHistory.handle(),
                   VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
    memoryBarrier(commands, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    // The integrated volume is sampled by the forward pass.
    memoryBarrier(commands, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

/// The scene's targets made ready to draw into, each cleared by the first
/// pass that renders to it. The scene draws into the top-left region, and the
/// output stage upscales it over the whole target (UTA-0051 SS 4.4, UTA-0154).
SceneTargets Renderer::Impl::prepareSceneTargets(VkCommandBuffer commands, VkExtent2D region) {
    hdr.transition(commands, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    velocity.transition(commands, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    emission.transition(commands, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    depth.transition(commands, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    SceneTargets targets;
    targets.colours[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    targets.colours[0].imageView = hdr.view();
    targets.colours[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    targets.colours[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    targets.colours[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    targets.colours[1] = targets.colours[0];
    targets.colours[1].imageView = velocity.view();
    targets.colours[2] = targets.colours[0];
    targets.colours[2].imageView = emission.view(); // UTA-0053
    targets.depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    targets.depth.imageView = depth.view();
    targets.depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    targets.depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    targets.depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    targets.depth.clearValue.depthStencil = {1.0f, 0};
    targets.area = {{0, 0}, region};
    return targets;
}

/// UTA-0260: the opaque surfaces' depth first. Indoors several surfaces lie
/// behind most pixels, and the forward pass lit each one a nearer surface
/// later covered. With the depth drawn, it lights what is seen and no more.
/// The same vertex stage draws both, its position declared invariant, so a
/// surface meets its own depth exactly.
void Renderer::Impl::recordDepthPrePass(VkCommandBuffer commands, SceneTargets& targets, VkExtent2D region) {
    const VkRenderingInfo depthOnly = targets.rendering(0);
    vkCmdBeginRendering(commands, &depthOnly);
    recordBatches(commands, ScenePass::Depth, region);
    vkCmdEndRendering(commands);
    memoryBarrier(commands, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    targets.depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
}

/// Every opaque, masked and sky batch, writing colour, velocity, emission and
/// depth.
void Renderer::Impl::recordOpaque(VkCommandBuffer commands, const SceneTargets& targets, VkExtent2D region) {
    const VkRenderingInfo rendering = targets.rendering(3);
    vkCmdBeginRendering(commands, &rendering);
    recordBatches(commands, ScenePass::Opaque, region);
    vkCmdEndRendering(commands);
}

/// 2. The translucent pass: colour only, depth-tested and not written.
void Renderer::Impl::recordTranslucent(VkCommandBuffer commands, SceneTargets& targets, VkExtent2D region) {
    // Rasterization order holds within one rendering instance, not across two:
    // the opaque pass's stores must reach this pass's loads and depth tests.
    memoryBarrier(commands,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                      | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                      | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                      | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                      | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    targets.colours[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    targets.depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    const VkRenderingInfo rendering = targets.rendering(1);
    vkCmdBeginRendering(commands, &rendering);
    recordBatches(commands, ScenePass::Translucent, region);
    vkCmdEndRendering(commands);
}

/// 2.5. Camera-facing flames (UTA-0263 SS 4.4). Additive, so their order does
/// not matter and nothing is sorted. They write emission, which the
/// translucent pass binds no target for, so this pass binds all three;
/// velocity takes the flame's zero, added. UTA-0215: the water's specks are
/// drawn in the same pass, under water only.
void Renderer::Impl::recordFlames(VkCommandBuffer commands, SceneTargets& targets, VkExtent2D region) {
    const bool motes = viewZone.water != 0 && enabled(Feature::WaterMotes, tier);
    // UTA-0337: a layer's light holds that layer alone.
    if ((flameCount == 0 && !motes) || (skyCapturing && skyLayerCapture != 0)) return;
    memoryBarrier(commands,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                      | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                      | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                      | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    targets.colours[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    targets.colours[2].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    const VkRenderingInfo rendering = targets.rendering(3);
    vkCmdBeginRendering(commands, &rendering);
    setViewportTo(commands, region);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->sceneLayout(), 0, 1, &sceneSet, 0,
                            nullptr);
    if (flameCount != 0) {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->flame());
        vkCmdDraw(commands, 6, flameCount, 0, 0); // flame.vert's six corners, one instance a flame
    }
    if (motes) {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->mote());
        vkCmdDraw(commands, 6, MOTE_COUNT, 0, 0); // mote.vert's six corners, one instance a speck
    }
    vkCmdEndRendering(commands);
}

/// UTA-0337: while UTA-0163 captures the sky, which batches it leaves out.
/// The sky's faces (skyLayerCapture 0) are captured without the sky zone's
/// layers. A layer's light (skyLayerCapture i + 1, for layer i) colours that
/// layer alone, while everything still draws its depth, so solid things
/// still hide it. Object 0 is the level, and the layers are its batches
/// (Geometry.h's DrawItem).
bool Renderer::Impl::skyCaptureLeavesOut(const DrawItem& item, ScenePass pass) const {
    if (!skyCapturing) return false;
    const bool level = item.objectIndex == 0;
    const auto layerOf = [&](const SkyLayer& layer) { return item.firstIndex == layer.firstIndex; };
    if (skyLayerCapture == 0) return level && std::ranges::any_of(skyLayers.layers, layerOf);
    if (pass == ScenePass::Depth) return false;
    return !level || !layerOf(skyLayers.layers[skyLayerCapture - 1]);
}

/// The batches `pass` draws, inside a rendering instance the caller began.
void Renderer::Impl::recordBatches(VkCommandBuffer commands, ScenePass pass, VkExtent2D region) {
    setViewportTo(commands, region);
    if (geometry->draws.empty()) return;
    // The vertices, then their SurfaceVertex stream.
    const std::array<VkBuffer, 2> vertexBuffers = {geometry->vertices.handle(), geometry->surfaceVertices.handle()};
    const std::array<VkDeviceSize, 2> zeros = {0, 0};
    vkCmdBindVertexBuffers(commands, 0, 2, vertexBuffers.data(), zeros.data());
    vkCmdBindIndexBuffer(commands, geometry->indices.handle(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->sceneLayout(), 0, 1, &sceneSet, 0,
                            nullptr);
    // UTA-0269: the pan in double, then its fraction, so a float never
    // holds rate * seconds, which loses the fraction within the hour.
    // UTA-0276: times the camera's zone's speed on that axis.
    const auto panned = [this](float rate, std::size_t axis) {
        // A pan that is not finite reads as none (review-code 2026-10-15).
        const double pan =
            std::fmod(static_cast<double>(rate) * static_cast<double>(cameraPanSpeed[axis]) * lastLightSeconds, 1.0);
        return std::isfinite(pan) ? static_cast<float>(pan) : 0.0F;
    };
    for (const DrawItem& item : geometry->draws) {
        if (skyCaptureLeavesOut(item, pass)) continue;
        // UTA-0271: a modulated surface blends too, so it is drawn with them.
        const bool blended = (item.polyFlags & (gpu::PF_TRANSLUCENT | gpu::PF_MODULATED)) != 0;
        if (blended != (pass == ScenePass::Translucent)) continue;
        // A masked surface's holes are its shader's to cut, so its depth is
        // not drawn ahead; it is tested and written in the forward pass.
        if (pass == ScenePass::Depth && (item.polyFlags & gpu::PF_MASKED) != 0) continue;
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pass == ScenePass::Depth ? pipelines->depthFor(item.polyFlags)
                                                   : pipelines->sceneFor(item.polyFlags));
        // A mirrored mover winds the other way on screen, so its front face is the other one.
        const VkFrontFace front = mirrored[item.objectIndex] ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
        vkCmdSetFrontFace(commands, front);
        // UTA-0311: UT99 adds a mover's polygons to the level's BSP after the
        // level's own, so a coplanar level surface is drawn and the mover's is
        // not. Pushing a mover's depth back by about a step of the depth
        // buffer, plus one pixel's slope, makes the level win that tie here.
        // Both passes set the same bias, so a mover still meets its own depth.
        const bool mover = item.objectIndex != 0;
        vkCmdSetDepthBias(commands, mover ? 2.0f : 0.0f, 0.0f, mover ? 1.0f : 0.0f);
        const gpu::DrawConstants constants{item.objectIndex, item.materialIndex, item.polyFlags, 0,
                                           {panned(item.panRate[0], 0), panned(item.panRate[1], 1)}};
        vkCmdPushConstants(commands, pipelines->sceneLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(constants), &constants);
        vkCmdDrawIndexed(commands, item.indexCount, 1, item.firstIndex, item.firstVertex, 0);
    }
}

/// 3. Emissive bloom (UTA-0053): down the chain from the emission target,
/// then back up it, each upsample added into the level above. Every read is
/// clamped to the part of its source the region drew (UTA-0051 SS 4.4).
void Renderer::Impl::recordBloom(VkCommandBuffer commands, VkExtent2D region) {
    if (!enabled(Feature::Bloom, tier)) {
        // post.frag reads nothing from it at strength 0, but its set still names it.
        bloom[0].transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        return;
    }
    const std::array<float, 2> drawnShare{static_cast<float>(region.width) / static_cast<float>(config.width),
                                          static_cast<float>(region.height) / static_cast<float>(config.height)};
    emission.transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    recordBloomLevel(commands, bloom[0], pipelines->bloomDownsample(), emissionSet, emission,
                     gpu::BLOOM_DOWNSAMPLE_FIRST, drawnShare);
    for (std::size_t k = 1; k < BLOOM_LEVELS; ++k)
        recordBloomLevel(commands, bloom[k], pipelines->bloomDownsample(), bloomSets[k - 1], bloom[k - 1],
                         gpu::BLOOM_DOWNSAMPLE, drawnShare);
    for (std::size_t k = BLOOM_LEVELS - 1; k > 0; --k)
        recordBloomLevel(commands, bloom[k - 1], pipelines->bloomUpsample(), bloomSets[k], bloom[k],
                         gpu::BLOOM_UPSAMPLE, drawnShare);
}

/// One step of the bloom chain: `source` filtered into `target` by `mode`.
/// `drawnShare` is the share of the target's width and height the region drew.
void Renderer::Impl::recordBloomLevel(VkCommandBuffer commands, Image& target, VkPipeline pipeline,
                                      VkDescriptorSet sourceSet, const Image& source, std::uint32_t mode,
                                      std::array<float, 2> drawnShare) {
    const ImageDesc& to = target.desc();
    const ImageDesc& from = source.desc();
    target.transition(commands, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo attachment{};
    attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment.imageView = target.view();
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    // An upsample adds into what that level's downsample left there.
    attachment.loadOp = mode == gpu::BLOOM_UPSAMPLE ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    pass.renderArea = {{0, 0}, {to.width, to.height}};
    pass.layerCount = 1;
    pass.colorAttachmentCount = 1;
    pass.pColorAttachments = &attachment;
    vkCmdBeginRendering(commands, &pass);
    setViewportTo(commands, {to.width, to.height});
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->bloomLayout(), 0, 1, &sourceSet, 0,
                            nullptr);
    const auto sourceWidth = static_cast<float>(from.width), sourceHeight = static_cast<float>(from.height);
    const gpu::BloomConstants constants{
        {1.0f / sourceWidth, 1.0f / sourceHeight},
        {1.0f / static_cast<float>(to.width), 1.0f / static_cast<float>(to.height)},
        {std::max(0.0f, drawnShare[0] - 0.5f / sourceWidth), std::max(0.0f, drawnShare[1] - 0.5f / sourceHeight)},
        BLOOM_RADIUS,
        mode};
    vkCmdPushConstants(commands, pipelines->bloomLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants),
                       &constants);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
    target.transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

/// 4. The output stage: the bloom added, then exposure and the tone map. A
/// frame drawn at full size is tone mapped straight into `output`. A smaller
/// region is tone mapped into FSR 1's input, which EASU upscales and RCAS
/// sharpens into `output` (UTA-0154).
void Renderer::Impl::recordOutput(VkCommandBuffer commands, VkExtent2D region) {
    hdr.transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (region.width == config.width && region.height == config.height) {
        recordFullTargetPass(commands, output, pipelines->post(), postSet, false, region);
        return;
    }
    recordFullTargetPass(commands, upscaleInput, pipelines->upscaleInput(), postSet, true, region);
    upscaleInput.transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    recordFullTargetPass(commands, upscaled, pipelines->easu(), easuSet, false, region);
    upscaled.transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    recordFullTargetPass(commands, output, pipelines->rcas(), rcasSet, false, region);
}

/// One of the output stage's passes, drawn over the whole of `target`.
void Renderer::Impl::recordFullTargetPass(VkCommandBuffer commands, Image& target, VkPipeline pipeline,
                                          VkDescriptorSet set, bool upscaleInputPass, VkExtent2D region) {
    const VkExtent2D extent{config.width, config.height};
    target.transition(commands, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo attachment{};
    attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment.imageView = target.view();
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    pass.renderArea = {{0, 0}, extent};
    pass.layerCount = 1;
    pass.colorAttachmentCount = 1;
    pass.pColorAttachments = &attachment;
    vkCmdBeginRendering(commands, &pass);
    setViewportTo(commands, extent);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->postLayout(), 0, 1, &set, 0, nullptr);
    const bool bloomOn = enabled(Feature::Bloom, tier);
    const gpu::PostConstants constants{EXPOSURE, config.linearOutput ? 1u : 0u, {region.width, region.height},
                                       upscaleInputPass ? 1u : 0u,
                                       // UTA-0292: the emission target holds light terms, not glow.
                                       bloomOn && !config.lightTerms ? BLOOM_STRENGTH : 0.0f,
                                       0.0f, // reserved; UTA-0215 SS 4.4's clock follows
                                       std::isfinite(lastLightSeconds)
                                           ? static_cast<float>(std::fmod(lastLightSeconds, FLAME_CLOCK_WRAP))
                                           : 0.0f,
                                       viewZone.water != 0
                                           ? std::array<float, 4>{viewZone.viewFog[0], viewZone.viewFog[1],
                                                                  viewZone.viewFog[2], 1.0f}
                                           : std::array<float, 4>{}};
    vkCmdPushConstants(commands, pipelines->postLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants),
                       &constants);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
}

// -- Renderer ------------------------------------------------------------------

Renderer::Renderer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Renderer::Renderer(Renderer&&) noexcept = default;
Renderer& Renderer::operator=(Renderer&&) noexcept = default;
Renderer::~Renderer() = default;

Result<Renderer> Renderer::create(const Config& config) {
    if (config.width == 0 || config.height == 0)
        return fail(ErrorCode::InvalidArgument,
                    std::format("a {}x{} target has no pixels to draw", config.width, config.height));
    // SS 4.3: createSurface selects the path, and the extensions must agree.
    const bool extensions = !config.instanceExtensions.empty();
    const bool presenting = static_cast<bool>(config.createSurface);
    if (extensions != presenting)
        return fail(ErrorCode::InvalidArgument,
                    extensions ? "instanceExtensions names a window's extensions but createSurface is unset"
                               : "createSurface is set but instanceExtensions names none");
    if (!std::isfinite(config.hazeScale) || config.hazeScale < 0)
        return fail(ErrorCode::InvalidArgument,
                    std::format("a hazeScale of {} is not a finite value of at least 0 (UTA-0015 SS 4.5)",
                                config.hazeScale));
    auto impl = std::make_unique<Impl>();
    impl->config = config;
    UTA_TRY(impl->gpu,
            Gpu::create(config.validation, config.instanceExtensions, config.createSurface, config.shaderStatistics));
    impl->gpu->failOnValidationError(config.failOnValidationError);
    // UTA-0051 SS 4.3: once, from the chosen device, unless the caller named one.
    impl->tier = config.tier.value_or(defaultTier(impl->gpu->type(), impl->gpu->deviceLocalBytes()));
    if (config.tier) {
        UTA_LOG(logRender, LogLevel::Info, "quality tier: {}, given", tierName(impl->tier));
    } else {
        UTA_LOG(logRender, LogLevel::Info, "quality tier: {}, chosen from the device ({} MiB device-local)",
                tierName(impl->tier), impl->gpu->deviceLocalBytes() / (1024u * 1024u));
    }
    UTA_TRY(impl->pipelines,
            Pipelines::create(*impl->gpu, {HDR_FORMAT, VELOCITY_FORMAT, DEPTH_FORMAT, OUTPUT_FORMAT, HDR_FORMAT},
                              impl->tier, config.lightTerms));
    if (presenting) {
        UTA_TRY(impl->swapchain, Swapchain::create(*impl->gpu, config.width, config.height));
        impl->adoptSwapchainExtent();
    }
    UTA_CHECK(impl->createTargets());
    impl->shadowPlanner = ShadowPlanner(shadowDetailOf(impl->tier));
    UTA_CHECK(impl->createShadowAtlas());
    UTA_CHECK(impl->createSamplers());
    UTA_CHECK(impl->createStandIns());
    UTA_CHECK(impl->createFogVolumes());
    UTA_CHECK(impl->createPassTimes());
    return Renderer(std::move(impl));
}

Result<void> Renderer::draw(const ubundle::Bundle& bundle, const Camera& camera) {
    Impl& impl = *impl_;
    // SS 4.3: the swapchain is rebuilt after a resize, when acquire or present
    // said it no longer matches its surface, and while the window is minimised
    // -- so it notices the window coming back.
    impl.lastDrawSkipped = true; // until a frame is drawn below
    if (impl.swapchain && (impl.resized || impl.swapchain->stale() || impl.swapchain->empty())) {
        // A failed create still retires `old`, and a retired swapchain may not
        // be passed as `old` again, so the next attempt passes none.
        Result<std::unique_ptr<Swapchain>> rebuilt = Swapchain::create(
            *impl.gpu, impl.config.width, impl.config.height,
            impl.swapchainRetired ? VK_NULL_HANDLE : impl.swapchain->handle());
        if (!rebuilt) {
            impl.swapchainRetired = true;
            return std::unexpected(rebuilt.error());
        }
        impl.swapchainRetired = false;
        impl.swapchain = std::move(*rebuilt);
        if (impl.adoptSwapchainExtent()) impl.resized = true;
    }
    if (impl.swapchain && impl.swapchain->empty()) return {}; // a minimised window draws nothing

    // SS 4.3: a resize takes effect here -- every size-bound target, and the
    // output stage's view of the colour one.
    if (impl.resized) {
        UTA_CHECK(impl.createTargets());
        if (impl.materials) UTA_CHECK(impl.writeDescriptors());
        impl.resized = false;
    }
    const auto checkStarted = std::chrono::steady_clock::now();
    if (const BundleShape shape = shapeOf(bundle); !(shape == impl.shape) || !impl.geometry) {
        impl.shape = BundleShape{};
        UTA_CHECK(impl.upload(bundle));
        impl.shape = shape;
        impl.fogFrames = 0; // UTA-0356: another level's fog is no history for this one
    }
    impl.bundleCheck = std::chrono::steady_clock::now() - checkStarted;

    // UTA-0163: a new level's sky, drawn once, before its first frame. Nothing
    // is presented; the faces go into the sky texture.
    if (impl.skyPending) {
        // UTA-0337: then each layer's light, into its own six faces.
        impl.skyCapturing = true;
        for (std::uint32_t capture = 0; capture <= impl.skyLayers.layers.size(); ++capture) {
            impl.skyLayerCapture = capture;
            for (std::uint32_t face = 0; face < 6; ++face) {
                const Camera faceCamera = skyFaceCamera(*impl.skyView, face, impl.config.width, impl.config.height);
                const Result<void> drawn = impl.drawView(bundle, faceCamera, std::nullopt, face);
                if (!drawn) {
                    impl.skyCapturing = false;
                    impl.skyLayerCapture = 0;
                }
                UTA_CHECK(drawn);
            }
        }
        impl.skyCapturing = false;
        impl.skyLayerCapture = 0;
        // Cleared once every face is in, so a failed face is drawn again.
        impl.skyPending = false;
        impl.skyReady = true;
        impl.previousViewProj.reset(); // the faces' views are not the player's last
        impl.previousFlameSeconds.reset();
        impl.fogFrames = 0;            // UTA-0356: nor is their fog the history
    }

    // Before anything is planned: a skipped frame must change nothing (Swapchain.h).
    std::optional<std::uint32_t> image;
    if (impl.swapchain) {
        UTA_TRY(image, impl.swapchain->acquire());
        if (!image) return {}; // out of date: rebuilt at the top of the next frame
    }

    Result<void> drawn = impl.drawView(bundle, camera, image, std::nullopt);
    if (!drawn && image) impl.swapchain->markStale();
    if (drawn) impl.lastDrawSkipped = false;
    return drawn;
}

Result<void> Renderer::Impl::drawView(const ubundle::Bundle& bundle, const Camera& camera,
                                      std::optional<std::uint32_t> image, std::optional<std::uint32_t> skyFace) {
    FrameTimes times;
    times.viewStarted = std::chrono::steady_clock::now();
    // UTA-0051 SS 4.4: the scale this frame draws at, and the top-left region of
    // the targets it covers.
    const TierSettings settings = settingsOf(tier);
    const double scale = scaleFor(skyFace.has_value(), settings);
    const auto regionOf = [scale](std::uint32_t full) {
        const auto scaled = static_cast<std::uint32_t>(std::ceil(scale * full));
        return std::clamp<std::uint32_t>(scaled, 1, full);
    };
    const VkExtent2D region{regionOf(config.width), regionOf(config.height)};

    // SS 4.3: the projection is this library's, and the previous frame's view
    // is cached here rather than supplied -- so a camera that did not move
    // produces zero motion. It, the jitter and the cluster grid take the region.
    const gpu::Mat4 view = viewOf(camera);
    const gpu::Mat4 projection = projectionOf(camera, region.width, region.height);
    const ViewMatrices matrices{
        view, multiply(projection, view),
        jitter ? jittered(projection, haltonJitter(frameIndex), region.width, region.height) : projection};

    const double seconds = pinnedLightSeconds
                               ? *pinnedLightSeconds
                               : std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    lastLightSeconds = seconds;
    // UTA-0286 SS 4.4: the fire looks run on the same clock.
    materials->advanceFires(seconds, pinnedLightSeconds.has_value());
    std::vector<gpu::Light> frameLights = drawnLights(bundle, seconds);

    gpu::FrameData frame = frameDataOf(bundle, camera, region, matrices, seconds, skyFace.has_value());
    // UTA-0438 SS 4.3: drawnLights put the special-lit lights last.
    frame.specialCount = specialLightCount(bundle);
    frame.specialFirst = static_cast<std::uint32_t>(frameLights.size()) - frame.specialCount;
    std::vector<gpu::Mat4> models = placeObjects(bundle);
    const ShadowPlan plan = planShadows(bundle, models);
    const std::uint32_t flashlight = placeLights(frame, frameLights, plan, camera);
    const std::uint8_t cameraZone = placeViewZone(frame, bundle, camera);
    placeSkyLayers(frame, bundle, camera, region, skyFace.has_value());
    const FogSetup fog = fogSetupOf(bundle, camera, region, frameLights, cameraZone, flashlight, skyFace.has_value());
    UTA_CHECK(uploadFrame(frame, frameLights, plan));

    // UTA-0051 SS 4.4: the measurement is this call, which waits for the GPU. It
    // leaves out present, which waits for the display under FIFO.
    times.started = std::chrono::steady_clock::now();
    const Result<void> ran = gpu->run([&](VkCommandBuffer commands) {
        const auto recordStarted = std::chrono::steady_clock::now();
        recordFrame(commands, plan, region, fog.constants);
        // SS 6's counts are read on the host once this finishes; the fence
        // alone does not make the compute pass's writes visible to it.
        memoryBarrier(commands, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        if (image) swapchain->recordBlit(commands, output, *image);
        if (skyFace) recordSkyFaceCopy(commands, region, *skyFace);
        times.recording = std::chrono::steady_clock::now() - recordStarted;
    });
    if (!ran) return ran;
    times.milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - times.started).count();
    if (image) UTA_CHECK(swapchain->present(gpu->queue(), *image));

    readStats(plan, fog.droppedVolumeLights, scale, times, skyFace.has_value());
    drawnScale = scale;
    // UTA-0163: a sky face is not the player's frame, so its time steers nothing.
    if (!skyFace && config.dynamicResolution && !config.fixedRenderScale && !pinnedRenderScale) {
        averagedMilliseconds =
            averagedMilliseconds == 0 ? times.milliseconds : averagedMilliseconds * 0.8 + times.milliseconds * 0.2;
        renderScale = nextRenderScale(renderScale, averagedMilliseconds, settings);
    }

    previousViewProj = matrices.viewProj;
    previousFlameSeconds = frame.flameSeconds;
    previousModels = std::move(models);
    ++frameIndex;
    if (!skyFace) ++fogFrames;
    drawn = true;
    return {};
}

/// UTA-0051 SS 4.4: the scale a view draws at. A fixed scale wins over a
/// pinned one, and both over dynamic resolution. A face of the sky is cut from
/// the whole target (UTA-0163).
double Renderer::Impl::scaleFor(bool skyFace, const TierSettings& settings) const {
    // A scale that is not finite reads as full (review-code 2026-10-15).
    const auto clamped = [&](double asked) {
        return std::isfinite(asked) ? std::clamp(asked, settings.minimumRenderScale, 1.0) : 1.0;
    };
    if (skyFace) return 1;
    if (config.fixedRenderScale) return clamped(*config.fixedRenderScale);
    // Only over dynamic resolution: without it or a fixed scale createTargets
    // makes no upscale images, so a reduced region would have nowhere to go.
    if (config.dynamicResolution && pinnedRenderScale) return clamped(*pinnedRenderScale);
    if (config.dynamicResolution) return renderScale;
    return 1;
}

/// The view's FrameData that the camera, the clock, the clusters, the probes,
/// the textures and the sky decide. Writes the cluster bounds.
gpu::FrameData Renderer::Impl::frameDataOf(const ubundle::Bundle& bundle, const Camera& camera, VkExtent2D region,
                                           const ViewMatrices& matrices, double seconds, bool skyFace) {
    gpu::FrameData frame{};
    frame.viewProj = multiply(matrices.drawnProjection, matrices.view);
    frame.viewProjUnjittered = matrices.viewProj;
    frame.previousViewProjUnjittered = previousViewProj.value_or(matrices.viewProj);
    frame.view = matrices.view;
    frame.eye = camera.location;
    frame.exposure = EXPOSURE;
    frame.nearPlane = camera.nearPlane;
    frame.farPlane = camera.farPlane;
    // scene.frag divides by this to find a pixel's cluster, so it is the region's.
    frame.viewportSize = {static_cast<float>(region.width), static_cast<float>(region.height)};
    // UTA-0263 SS 4.4: flames run on the light clock. A float holds it to a
    // millisecond for hours only, so it wraps; a pinned time that is not
    // finite reads as 0.
    frame.flameSeconds = std::isfinite(seconds) ? static_cast<float>(std::fmod(seconds, FLAME_CLOCK_WRAP)) : 0.0f;
    frame.previousFlameSeconds = previousFlameSeconds.value_or(frame.flameSeconds);
    const ClusterGrid grid = clusterGrid(camera, region.width, region.height);
    std::memcpy(clusterBounds.mapped(), grid.bounds.data(), grid.bounds.size() * sizeof(gpu::ClusterBounds));
    frame.clusterGrid = gpu::CLUSTER_GRID;
    frame.clusterDepthScale = grid.depthScale;
    frame.clusterDepthBias = grid.depthBias;
    frame.probeSpacing = probeSpacing;
    frame.probeCount = probeCount;
    frame.probeTableMask = probeTableMask;
    frame.probeLongestRun = probeLongestRun;
    const TextureSlots slots = textureSlots();
    // UTA-0163: a face of the sky is drawn without the sky, which is not drawn yet.
    frame.skyTexture = skyReady && !skyFace ? slots.sky : gpu::NONE;
    frame.skyCapture = skyFace ? 1u : 0u; // UTA-0281
    // UTA-0350 SS 4.4: a painted sky stands in where there is no sky view.
    if (bundle.paintedSky && !skyView) {
        frame.paintedSky = 1;
        frame.paintedSkyTop = bundle.paintedSky->top;
        frame.paintedSkyHorizon = bundle.paintedSky->horizon;
        frame.paintedSkyClouds = bundle.paintedSky->clouds;
        // UTA-0337: the clouds drift with the light clock; the bake took their
        // mean, so the light they give does not move.
        for (std::size_t axis = 0; axis < 2; ++axis)
            frame.paintedSkyDrift[axis] = static_cast<float>(PAINTED_SKY_WIND[axis] * lastLightSeconds);
    }
    // UTA-0337 part 3: the outdoor haze scales to the level, since one
    // distance washes out a wide map's near ground or misses a small map's walls.
    // UTA-0391: a painted sky's recipe may start it further out.
    const OutdoorHaze haze = outdoorHazeOf(
        outdoorExtent, bundle.paintedSky && !skyView ? bundle.paintedSky->hazeStart : OUTDOOR_HAZE_START_SHARE);
    frame.outdoorHazeStart = haze.start;
    frame.outdoorHazeDepth = haze.depth;
    frame.occlusionTexture = slots.occlusion;   // UTA-0164 SS 4.5
    frame.shadowMaskTexture = slots.shadowMask; // UTA-0326 SS 4.5
    return frame;
}

/// Each object's model this frame -- the level's, then each mover's -- written
/// with last frame's for the GPU, and which of them mirror.
std::vector<gpu::Mat4> Renderer::Impl::placeObjects(const ubundle::Bundle& bundle) {
    std::vector<gpu::Mat4> models(geometry->objectCount, identity());
    if (bundle.movers)
        for (std::size_t i = 0; i < bundle.movers->size(); ++i) models[i + 1] = moverModel((*bundle.movers)[i]);
    if (previousModels.size() != models.size()) previousModels = models;
    mirrored.assign(models.size(), false);
    for (std::size_t i = 0; i < models.size(); ++i) mirrored[i] = mirrors(models[i]);
    for (std::size_t i = 0; i < models.size(); ++i) {
        const gpu::Object object{models[i], previousModels[i], normalMatrixOf(models[i])};
        std::memcpy(objects.mapped() + i * sizeof(gpu::Object), &object, sizeof(object));
    }
    return models;
}

/// SS 4.8: each light's tiles, and which must be drawn this frame -- a mover
/// that moved redraws the lights whose radius its box, before or after,
/// reaches. A tile's size follows its light's reach alone (UTA-0166), so
/// neither the camera nor the region moves it.
ShadowPlan Renderer::Impl::planShadows(const ubundle::Bundle& bundle, std::span<const gpu::Mat4> models) {
    std::vector<Box> moved;
    for (std::size_t i = 1; i < models.size(); ++i) {
        if (models[i] == previousModels[i]) continue;
        moved.push_back(worldBox(moverBounds[i - 1], previousModels[i]));
        moved.push_back(worldBox(moverBounds[i - 1], models[i]));
    }
    return shadowPlanner.plan(directLights(bundle), moved, nearLights);
}

/// Each light's shadow faces from the plan, then the flashlight, last so it
/// holds no tile (UTA-0015 SS 4.5). Returns the flashlight's index, or NONE.
std::uint32_t Renderer::Impl::placeLights(gpu::FrameData& frame, std::vector<gpu::Light>& frameLights,
                                          const ShadowPlan& plan, const Camera& camera) {
    for (std::size_t i = 0; i < frameLights.size(); ++i) {
        frameLights[i].shadowFace = plan.firstFace[i];
        frameLights[i].shadowFaceCount = plan.faceCount[i];
    }
    std::uint32_t flashlight = gpu::NONE;
    if (camera.flashlight) {
        flashlight = static_cast<std::uint32_t>(frameLights.size());
        frameLights.push_back(flashlightOf(camera));
    }
    frame.lightCount = static_cast<std::uint32_t>(frameLights.size());
    frame.flashlight = flashlight; // UTA-0326 SS 4.5
    // UTA-0338 SS 4.5: applySun put the sun last in LITE, if the bundle has one.
    frame.sun = gpu::NONE;
    for (std::size_t i = 0; i < frameLights.size(); ++i)
        if (frameLights[i].effect == ubundle::SUN_EFFECT) frame.sun = static_cast<std::uint32_t>(i);
    return flashlight;
}

/// The zone the camera is in (UTA-0089 SS 4.2): its pan speeds, its water and
/// its ViewFog. Returns its index.
std::uint8_t Renderer::Impl::placeViewZone(gpu::FrameData& frame, const ubundle::Bundle& bundle,
                                           const Camera& camera) {
    const std::size_t zoneCount = bundle.zones ? bundle.zones->size() : 1;
    const std::uint8_t cameraZone = bundle.rooms ? ubundle::zoneAt(*bundle.rooms, camera.location, zoneCount) : 0;
    frame.cameraZone = cameraZone;
    viewZone = bundle.zones ? (*bundle.zones)[cameraZone < bundle.zones->size() ? cameraZone : 0] : ubundle::Zone{};
    cameraPanSpeed = viewZone.panSpeed;
    frame.cameraUnderwater = viewZone.water; // UTA-0215 SS 4.3
    // UTA-0215 SS 4.8: the light the output stage shows as ViewFog. The tone map
    // is the identity this low, so only the sRGB decode and the exposure undo.
    for (std::size_t c = 0; c < 3; ++c) {
        const double shown = std::clamp(static_cast<double>(viewZone.viewFog[c]), 0.0, 1.0);
        const double light = shown <= 0.04045 ? shown / 12.92 : std::pow((shown + 0.055) / 1.055, 2.4);
        frame.waterFog[c] = static_cast<float>(config.linearOutput ? light : light / EXPOSURE);
    }
    return cameraZone;
}

/// UTA-0337: the sky's layers, panned at the sky zone's speeds as the faces
/// would have drawn them now. Laid over the sky only once its faces and every
/// layer's light are in; while they are captured, the planes still tell a
/// layer's sheet from the batch's others.
void Renderer::Impl::placeSkyLayers(gpu::FrameData& frame, const ubundle::Bundle& bundle, const Camera& camera,
                                    VkExtent2D region, bool skyFace) {
    if (!skyView) return;
    const std::array<float, 2> speed = bundle.zones && skyLayers.zone < bundle.zones->size()
                                           ? (*bundle.zones)[skyLayers.zone].panSpeed
                                           : std::array<float, 2>{1, 1};
    frame.skyOrigin = {skyView->location[0], skyView->location[1], skyView->location[2], 0};
    frame.skyPixelAngle = static_cast<float>(2.0 * std::tan(camera.verticalFovDegrees * std::numbers::pi / 360.0)
                                             / static_cast<double>(region.height));
    frame.skyLayerCapture = skyFace ? skyLayerCapture : 0;
    frame.skyLayerCount = skyReady && !skyFace ? static_cast<std::uint32_t>(skyLayers.layers.size()) : 0;
    const std::uint32_t firstLight = textureSlots().firstSkyLayerLight;
    for (std::size_t i = 0; i < skyLayers.layers.size(); ++i) {
        const SkyLayer& layer = skyLayers.layers[i];
        gpu::SkyLayer& out = frame.skyLayers[i];
        out.plane = layer.plane;
        out.u = layer.u;
        out.v = layer.v;
        for (std::size_t axis = 0; axis < 2; ++axis) {
            // As UTA-0269's batches pan: in double, then its fraction.
            const double pan = std::fmod(
                static_cast<double>(layer.panRate[axis]) * static_cast<double>(speed[axis]) * lastLightSeconds, 1.0);
            out.pan[axis] = std::isfinite(pan) ? static_cast<float>(pan) : 0.0F;
        }
        out.material = materials->indexOf(layer.material);
        out.light = firstLight + static_cast<std::uint32_t>(i);
        out.kind = layer.kind;
    }
}

/// UTA-0015 SS 4.4: which volumetric lights glow, from the camera's zone,
/// written for the fog's passes, and the constants those passes take.
FogSetup Renderer::Impl::fogSetupOf(const ubundle::Bundle& bundle, const Camera& camera, VkExtent2D region,
                                    std::span<const gpu::Light> frameLights, std::uint8_t cameraZone,
                                    std::uint32_t flashlight, bool skyFace) {
    const std::array<ubundle::Zone, 1> noZone{};
    const std::span<const ubundle::Zone> zoneSpan =
        bundle.zones ? std::span<const ubundle::Zone>(*bundle.zones) : std::span<const ubundle::Zone>(noZone);
    const VolumeLightChoice glowing = volumeLights(frameLights, lightZones, cameraZone, zoneSpan, camera.location);
    if (!glowing.indices.empty())
        std::memcpy(volumeLightIndices.mapped(), glowing.indices.data(),
                    glowing.indices.size() * sizeof(std::uint32_t));
    const double tanVertical = std::tan(camera.verticalFovDegrees * std::numbers::pi / 360.0);
    FogSetup setup;
    setup.droppedVolumeLights = glowing.dropped;
    gpu::FogConstants& fog = setup.constants;
    fog.viewToWorld = viewToWorldOf(camera);
    fog.tanHalfFov = {static_cast<float>(tanVertical * region.width / region.height), static_cast<float>(tanVertical)};
    fog.volumeLightCount = static_cast<std::uint32_t>(glowing.indices.size());
    fog.flashlight = flashlight;
    fog.hazeScale = config.hazeScale;
    // UTA-0356: frame k since a cut samples each froxel at the R3 sequence's
    // k-th point (Roberts, "The Unreasonable Effectiveness of Quasirandom
    // Sequences", 2018) from the centre, and keeps a running mean of the first
    // ten, then 0.1 of each new frame. A sky face is its own single frame.
    const std::uint64_t fogFrame = skyFace ? 0 : fogFrames;
    constexpr std::array<double, 3> R3{0.8191725133961645, 0.6710436067037893, 0.5497004779019703};
    for (std::size_t a = 0; a < R3.size(); ++a) {
        const double at = 0.5 + static_cast<double>(fogFrame) * R3[a];
        fog.jitter[a] = static_cast<float>(at - std::floor(at));
    }
    fog.historyWeight = static_cast<float>(std::max(1.0 / static_cast<double>(fogFrame + 1), 0.1));
    return setup;
}

/// The lights, the shadow faces and then the FrameData, written for the GPU.
/// The lights and shadow faces are the only per-frame writes sized by upload
/// to what the bundle held then; the cluster, object and volume-light writes
/// have sizes fixed at creation or by the geometry. The sky's six faces
/// follow the lights', so they count against the room.
Result<void> Renderer::Impl::uploadFrame(gpu::FrameData& frame, std::span<const gpu::Light> frameLights,
                                         const ShadowPlan& plan) {
    if (frameLights.size() > lightCapacity || plan.faces.size() + 6 > shadowFaceCapacity)
        return fail(ErrorCode::InvalidArgument, "the bundle's drawn lights outgrew the buffers its upload sized; "
                                                "it changed in place without a reload");
    if (!frameLights.empty()) std::memcpy(lights.mapped(), frameLights.data(), frameLights.size() * sizeof(gpu::Light));
    if (!plan.faces.empty())
        std::memcpy(shadowFaces.mapped(), plan.faces.data(), plan.faces.size() * sizeof(gpu::ShadowFace));
    frame.shadowFaceCount = static_cast<std::uint32_t>(plan.faces.size());
    // UTA-0163: the sky's six faces follow the lights'.
    frame.skyFirstFace = frame.shadowFaceCount;
    if (skyView)
        for (std::uint32_t face = 0; face < 6; ++face) {
            const gpu::ShadowFace sample = skyFaceSample(face);
            std::memcpy(shadowFaces.mapped() + (frame.skyFirstFace + face) * sizeof(gpu::ShadowFace), &sample,
                        sizeof(sample));
        }
    std::memcpy(frameData.mapped(), &frame, sizeof(frame));
    return {};
}

/// UTA-0163: the target's central square, which the face camera made span 90
/// degrees, into the face's cell of the sky texture -- or, UTA-0337, a layer's
/// light into that layer's faces.
void Renderer::Impl::recordSkyFaceCopy(VkCommandBuffer commands, VkExtent2D region, std::uint32_t skyFace) {
    const std::uint32_t side = std::min(region.width, region.height);
    const auto x0 = static_cast<std::int32_t>((region.width - side) / 2);
    const auto y0 = static_cast<std::int32_t>((region.height - side) / 2);
    const auto column = static_cast<std::int32_t>(skyFace % SKY_COLUMNS * SKY_FACE_SIZE);
    const auto row = static_cast<std::int32_t>(skyFace / SKY_COLUMNS * SKY_FACE_SIZE);
    const auto size = static_cast<std::int32_t>(SKY_FACE_SIZE);
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0] = {x0, y0, 0};
    blit.srcOffsets[1] = {x0 + static_cast<std::int32_t>(side), y0 + static_cast<std::int32_t>(side), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0] = {column, row, 0};
    blit.dstOffsets[1] = {column + size, row + size, 1};
    hdr.transition(commands, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Image& target = skyLayerCapture == 0 ? sky : skyLayerLights[skyLayerCapture - 1];
    target.transition(commands, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdBlitImage(commands, hdr.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target.handle(),
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
    target.transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

/// SS 6's stats of the frame just finished, and UTA-0351's GPU step times.
void Renderer::Impl::readStats(const ShadowPlan& plan, std::uint32_t droppedVolumeLights, double scale,
                               const FrameTimes& times, bool skyFace) {
    // How many clusters dropped lights past their cap, read back from the
    // pass that decided -- the frame has finished, so the counts are final. One
    // sequential copy out of the mapping: where the GPU offers no cached type,
    // each read of it is uncached, so a read per cluster costs thousands.
    stats = {};
    std::array<std::uint32_t, gpu::CLUSTER_COUNT> counts{};
    std::memcpy(counts.data(), clusterCounts.mapped(), sizeof(counts));
    for (const std::uint32_t count : counts)
        if ((count & gpu::CLUSTER_OVERFLOW) != 0) ++stats.overflowedClusters;
    stats.unshadowedLights = plan.unshadowed;
    stats.renderedShadowTiles = static_cast<std::uint32_t>(plan.draws.size());
    stats.droppedVolumeLights = droppedVolumeLights;
    stats.tier = tier;
    stats.renderScale = scale;
    stats.frameMilliseconds = times.milliseconds;
    // UTA-0352: a sky face's draw made no bundle check of its own.
    using Milliseconds = std::chrono::duration<double, std::milli>;
    stats.prepareMilliseconds =
        Milliseconds(times.started - times.viewStarted + (skyFace ? std::chrono::steady_clock::duration{} : bundleCheck))
            .count();
    stats.recordMilliseconds = Milliseconds(times.recording).count();
    if (passTimes != VK_NULL_HANDLE) {
        // UTA-0351: the frame has finished, so every bound is written.
        std::array<std::uint64_t, GPU_PASS_COUNT + 1> ticks{};
        const VkResult read = vkGetQueryPoolResults(gpu->device(), passTimes, 0, GPU_PASS_COUNT + 1, sizeof(ticks),
                                                    ticks.data(), sizeof(std::uint64_t),
                                                    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (read == VK_SUCCESS) {
            for (std::size_t pass = 0; pass < GPU_PASS_COUNT; ++pass)
                stats.passMilliseconds[pass] =
                    static_cast<double>((ticks[pass + 1] - ticks[pass]) & timestampMask) * timestampNanoseconds / 1e6;
            stats.passesTimed = true;
        }
    }
}

Result<std::vector<std::byte>> Renderer::readback(Target target) {
    // Both paths. The presenting path draws into impl.output too and blits THAT
    // into the acquired swapchain image (Swapchain::recordBlit), and both paths
    // submit and wait before presenting -- so the pixels are here and finished
    // either way. UTA-0191's capture folder is what needed the presented frame.
    Impl& impl = *impl_;
    if (!impl.drawn) return fail(ErrorCode::InvalidArgument, "readback before any frame was drawn");
    if (impl.lastDrawSkipped)
        return fail(ErrorCode::InvalidArgument,
                    "readback after a skipped frame: the pixels are an older frame's, not the last draw's");
    if (target != Target::Colour && impl.drawnScale < 1.0)
        return fail(ErrorCode::InvalidArgument,
                    std::format("{} readback after a frame drawn at scale {}: its region is smaller than the "
                                "target (UTA-0051 SS 4.4)",
                                target == Target::Velocity ? "velocity" : "emission", impl.drawnScale));

    Image& image = target == Target::Colour ? impl.output : target == Target::Velocity ? impl.velocity : impl.emission;
    const std::size_t pixels = static_cast<std::size_t>(impl.config.width) * impl.config.height;
    // RGBA8 and RG16F are both four bytes a pixel; the emission's RGBA16F is eight.
    const std::size_t bytesPerPixel = target == Target::Emission ? 8 : 4;
    UTA_TRY(Buffer host, Buffer::create(*impl.gpu, pixels * bytesPerPixel, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                        BufferMemory::HostRead));
    UTA_CHECK(impl.gpu->run([&](VkCommandBuffer commands) {
        image.transition(commands, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {impl.config.width, impl.config.height, 1};
        vkCmdCopyImageToBuffer(commands, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, host.handle(), 1, &copy);
        memoryBarrier(commands, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    }));

    if (target == Target::Colour) return std::vector<std::byte>(host.mapped(), host.mapped() + pixels * 4);

    // The float targets: each channel a 16-bit half, widened to a float.
    const std::size_t halves = pixels * bytesPerPixel / sizeof(std::uint16_t);
    std::vector<std::byte> floats(halves * sizeof(float));
    for (std::size_t i = 0; i < halves; ++i) {
        std::uint16_t half = 0;
        std::memcpy(&half, host.mapped() + i * 2, sizeof(half));
        const float value = halfToFloat(half);
        std::memcpy(floats.data() + i * sizeof(float), &value, sizeof(value));
    }
    return floats;
}

FrameStats Renderer::lastFrameStats() const noexcept { return impl_->stats; }

void Renderer::setJitter(bool enabled) noexcept { impl_->jitter = enabled; }

void Renderer::cut() noexcept {
    impl_->previousViewProj.reset();
    impl_->previousFlameSeconds.reset();
    impl_->fogFrames = 0;
}

void Renderer::setLinearOutput(bool enabled) noexcept { impl_->config.linearOutput = enabled; }

double Renderer::lightSeconds() const noexcept { return impl_->lastLightSeconds; }

bool Renderer::validating() const noexcept { return impl_->gpu->validating(); }

std::string Renderer::deviceName() const { return impl_->gpu->name(); }

std::vector<ShaderStatistics> Renderer::shaderStatistics() const { return impl_->pipelines->statistics(); }

void Renderer::pinLightSeconds(double seconds) noexcept { impl_->pinnedLightSeconds = seconds; }

void Renderer::unpinLightSeconds() noexcept { impl_->pinnedLightSeconds.reset(); }

void Renderer::pinRenderScale(double scale) noexcept { impl_->pinnedRenderScale = scale; }

void Renderer::unpinRenderScale() noexcept { impl_->pinnedRenderScale.reset(); }

Result<void> Renderer::resize(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0)
        return fail(ErrorCode::InvalidArgument, std::format("a {}x{} target has no pixels to draw", width, height));
    Impl& impl = *impl_;
    if (width == impl.config.width && height == impl.config.height) return {};
    impl.config.width = width;
    impl.config.height = height;
    impl.resized = true;
    impl.drawn = false; // the last frame is another size, so there is none to read back
    return {};
}

} // namespace uta::urender
