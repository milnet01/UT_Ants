// The Renderer: its targets, and the frame it draws --
// docs/specs/UTA-0014-vulkan-draw-path.md SS 4.3, SS 4.10 and SS 4.11.
//
// THE FRAME, IN ORDER:
//   0. clustered light culling, a compute pass (SS 4.6);
//   1. the forward pass: every opaque, masked and sky batch, writing colour,
//      velocity, emission and depth;
//   2. the translucent pass: colour only, depth-tested and not written;
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
#include "urender/Geometry.h"
#include "urender/Lights.h"
#include "urender/Materials.h"
#include "urender/Pipelines.h"
#include "urender/Placement.h"
#include "urender/Probes.h"
#include "urender/Shadows.h"
#include "urender/Resources.h"
#include "urender/ShaderTypes.h"
#include "urender/Swapchain.h"
#include "urender/Tiers.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <optional>
#include <span>

namespace uta::urender {

namespace {

constexpr VkFormat HDR_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat VELOCITY_FORMAT = VK_FORMAT_R16G16_SFLOAT;
constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
/// SS 4.10 names it, because a read-back pixel is compared against a literal
/// and B8G8R8A8 would swap two channels of it without failing anything else.
constexpr VkFormat OUTPUT_FORMAT = VK_FORMAT_R8G8B8A8_SRGB;

/// SS 4.10: fixed, with no adaptation. Measured, not chosen: the exposure whose
/// displayed frames best match the original game's over DM-Deck16]['s
/// PlayerStart views, drawn from the same cameras. UTA-0156 fitted 2.41 and set
/// 2.4; UTA-0162's strip lights moved the fit to 3.05, and 3.0 has the lower
/// block error of the two nearest. UTA-0156's cylinder fade moved it to 3.18,
/// and 3.2 has the lowest block error of 3.0, 3.1 and 3.2. The method and
/// scripts are on UTA-0156.
constexpr float EXPOSURE = 3.2f;

/// UTA-0053's emissive bloom, as LearnOpenGL's physically based bloom builds it
/// (learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom, after Jimenez's
/// SIGGRAPH 2014 Call of Duty: Advanced Warfare talk): five levels, each half
/// the one before, an upsample radius of 0.005, and 0.04 of the result. Only
/// emission feeds it, so the frame adds it rather than mixing it in.
constexpr std::size_t BLOOM_LEVELS = 5;
constexpr float BLOOM_RADIUS = 0.005f;
constexpr float BLOOM_STRENGTH = 0.04f;

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
/// lights and the movers' transforms are read every frame and need no upload.
/// ZONE is sampled whole: it holds at most ZONE_LIMIT entries (UTA-0156).
struct BundleShape {
    const ubundle::Bundle* address = nullptr;
    std::size_t vertices = 0, indices = 0, batches = 0, textures = 0, materials = 0, movers = 0, moverIndices = 0,
                lights = 0, probes = 0, zones = 0;
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
    }
}

BundleShape shapeOf(const ubundle::Bundle& bundle) {
    BundleShape shape;
    shape.address = &bundle;
    Fnv fnv;
    if (bundle.geometry) sampleGeometry(fnv, *bundle.geometry);
    if (bundle.movers)
        for (const ubundle::MoverShape& mover : *bundle.movers) sampleGeometry(fnv, mover.geometry);
    if (bundle.materials) {
        for (const ubundle::MaterialRecord& record : *bundle.materials) {
            fnv.add(std::as_bytes(std::span(record.id)));
            fnv.addValue(record.metallic);
        }
    }
    // UTA-0156 SS 4.4: a bundle differing only in its zones must re-upload them.
    if (bundle.zones) {
        shape.zones = bundle.zones->size();
        for (const ubundle::ZoneAmbient& zone : *bundle.zones) {
            fnv.addValue(zone.brightness);
            fnv.addValue(zone.hue);
            fnv.addValue(zone.saturation);
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
    Buffer zones; ///< UTA-0156 SS 4.4
    ShadowPlanner shadowPlanner;
    /// Each mover's box in its own pivot space, for SS 4.8's redraw test.
    std::vector<Box> moverBounds;

    BundleShape shape;
    std::optional<MaterialSet> materials;
    std::optional<SceneGeometry> geometry;

    std::optional<gpu::Mat4> previousViewProj;
    std::vector<gpu::Mat4> previousModels;
    /// Per object, whether its model reverses winding -- SS 4.5's mirrored mover.
    std::vector<bool> mirrored;
    bool jitter = false;
    /// A `resize` the next draw has still to apply.
    bool resized = false;
    std::uint64_t frameIndex = 0;
    /// SS 4.9's clock: a flickering light's phase is measured from here.
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    bool drawn = false;
    FrameStats stats;
    /// UTA-0051 SS 4.3: chosen once, at create.
    Tier tier = Tier::Low;
    /// UTA-0051 SS 4.4: the scale dynamic resolution draws the next frame at,
    /// the scale the last frame was drawn at, and the averaged frame time the
    /// controller reads -- which is where its smoothing lives (INV-5).
    double renderScale = 1;
    double drawnScale = 1;
    double averagedMilliseconds = 0;

    ~Impl() {
        if (!gpu) return;
        const VkDevice device = gpu->device();
        vkDeviceWaitIdle(device);
        if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, pool, nullptr);
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
    Result<void> createStandIns();
    Result<void> upload(const ubundle::Bundle& bundle);
    Result<void> writeDescriptors();
    void recordFrame(VkCommandBuffer commands, const ShadowPlan& shadows, VkExtent2D region);
};

/// Every target the window's size decides. SS 4.3's resize rebuilds them.
Result<void> Renderer::Impl::createTargets() {
    const std::uint32_t w = config.width, h = config.height;
    UTA_TRY(hdr, Image::create(*gpu, {HDR_FORMAT, w, h, 1,
                                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT}));
    UTA_TRY(velocity, Image::create(*gpu, {VELOCITY_FORMAT, w, h, 1,
                                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT}));
    UTA_TRY(depth, Image::create(*gpu, {DEPTH_FORMAT, w, h, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT}));
    UTA_TRY(output, Image::create(*gpu, {OUTPUT_FORMAT, w, h, 1,
                                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT}));
    // UTA-0053: the emission target at the output's size, and the bloom chain
    // from half that down.
    const VkImageUsageFlags bloomUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    UTA_TRY(emission, Image::create(*gpu, {HDR_FORMAT, w, h, 1, bloomUsage}));
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
    UTA_TRY(shadowAtlas, Image::create(*gpu, {DEPTH_FORMAT, SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE, 1,
                                              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT}));
    return gpu->run([&](VkCommandBuffer commands) {
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
    UTA_CHECK(check(vkCreateSampler(gpu->device(), &info, nullptr, &materialSampler), "vkCreateSampler"));

    info.magFilter = VK_FILTER_NEAREST;
    info.minFilter = VK_FILTER_NEAREST;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxLod = 0.0f;
    UTA_CHECK(check(vkCreateSampler(gpu->device(), &info, nullptr, &nearestSampler), "vkCreateSampler"));

    // UTA-0053: the bloom chain filters, and never wraps past an edge.
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    UTA_CHECK(check(vkCreateSampler(gpu->device(), &info, nullptr, &bloomSampler), "vkCreateSampler"));

    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.compareEnable = VK_TRUE;
    info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    return check(vkCreateSampler(gpu->device(), &info, nullptr, &shadowSampler), "vkCreateSampler");
}

Result<void> Renderer::Impl::createStandIns() {
    const VkBufferUsageFlags storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    UTA_TRY(frameData, Buffer::create(*gpu, sizeof(gpu::FrameData), storage, true));
    // SS 4.6: the grid is fixed, so its buffers are sized once. The counts are
    // host-visible because each frame reads them back for SS 6's overflow count.
    UTA_TRY(clusterCounts, Buffer::create(*gpu, sizeof(std::uint32_t) * gpu::CLUSTER_COUNT, storage, true));
    UTA_TRY(clusterIndices, Buffer::create(*gpu, sizeof(std::uint32_t) * gpu::CLUSTER_COUNT * gpu::CLUSTER_CAPACITY,
                                           storage, true));
    UTA_TRY(clusterBounds, Buffer::create(*gpu, sizeof(gpu::ClusterBounds) * gpu::CLUSTER_COUNT, storage, true));
    return {};
}

Result<void> Renderer::Impl::upload(const ubundle::Bundle& bundle) {
    geometry.reset();
    materials.reset();
    UTA_TRY(MaterialSet uploadedMaterials, MaterialSet::upload(*gpu, bundle));
    materials.emplace(std::move(uploadedMaterials));
    if (materials->textures().size() > pipelines->textureCapacity())
        return fail(ErrorCode::InvalidArgument,
                    std::format("the bundle needs {} textures and {} binds at most {}", materials->textures().size(),
                                gpu->name(), pipelines->textureCapacity()));
    UTA_TRY(SceneGeometry uploadedGeometry, SceneGeometry::upload(*gpu, bundle, *materials));
    geometry.emplace(std::move(uploadedGeometry));
    UTA_TRY(objects, Buffer::create(*gpu, sizeof(gpu::Object) * geometry->objectCount,
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true));
    const std::size_t lightCount = std::max<std::size_t>(1, drawnLights(bundle, 0.0).size());
    UTA_TRY(lights, Buffer::create(*gpu, sizeof(gpu::Light) * lightCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true));

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
    // so the binding is never empty and zone 0 always reads.
    std::vector<gpu::Zone> zoneRecords;
    if (bundle.zones)
        for (const ubundle::ZoneAmbient& zone : *bundle.zones)
            zoneRecords.push_back(gpu::Zone{zone.brightness, zone.hue, zone.saturation, 0});
    if (zoneRecords.empty()) zoneRecords.push_back(gpu::Zone{});
    UTA_TRY(zones, Buffer::upload(*gpu, std::as_bytes(std::span(zoneRecords)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));

    // SS 4.8: no tile shows this bundle's geometry yet, and each light holds at
    // most six faces.
    shadowPlanner.reset();
    UTA_TRY(shadowFaces, Buffer::create(*gpu, sizeof(gpu::ShadowFace) * lightCount * 6,
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true));
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

    const auto textureCount = static_cast<std::uint32_t>(materials->textures().size());
    const std::array sizes = {
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, gpu::ZONES + 1},
        // The atlas and the three post sets' sources, then UTA-0053's: bloom in
        // each post set, and one source per bloom step.
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                             textureCount + 4 + 3 + 1 + static_cast<std::uint32_t>(BLOOM_LEVELS)},
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 4 + 1 + static_cast<std::uint32_t>(BLOOM_LEVELS);
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    poolInfo.pPoolSizes = sizes.data();
    UTA_CHECK(check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool), "vkCreateDescriptorPool"));

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

    const std::array<const Buffer*, gpu::ZONES + 1> buffers = {
        &frameData, &objects, &materials->records(), &lights, &clusterCounts,
        &clusterIndices, &clusterBounds, &probeCells, &probes, &shadowFaces, &zones};
    std::array<VkDescriptorBufferInfo, gpu::ZONES + 1> bufferInfos{};
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

    std::vector<VkDescriptorImageInfo> textureInfos;
    textureInfos.reserve(textureCount);
    for (const Image& texture : materials->textures())
        textureInfos.push_back({materialSampler, texture.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
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

void Renderer::Impl::recordFrame(VkCommandBuffer commands, const ShadowPlan& shadows, VkExtent2D region) {
    const VkExtent2D extent{config.width, config.height};
    const VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    const VkRect2D scissor{{0, 0}, extent};
    // UTA-0051 SS 4.4: the scene draws into the top-left region, and the
    // output stage upscales it over the whole target (UTA-0154).
    const VkViewport regionViewport{0, 0, static_cast<float>(region.width), static_cast<float>(region.height), 0, 1};
    const VkRect2D regionScissor{{0, 0}, region};

    // -- Shadow tiles: only those this frame's plan says to draw (SS 4.8) ------
    if (!shadows.draws.empty() && !geometry->draws.empty()) {
        shadowAtlas.transition(commands, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo atlasAttachment{};
        atlasAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        atlasAttachment.imageView = shadowAtlas.view();
        atlasAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        atlasAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // the kept tiles stay
        atlasAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo shadowPass{};
        shadowPass.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        shadowPass.renderArea = {{0, 0}, {SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE}};
        shadowPass.layerCount = 1;
        shadowPass.pDepthAttachment = &atlasAttachment;
        vkCmdBeginRendering(commands, &shadowPass);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->shadow());
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->shadowLayout(), 0, 1,
                                &sceneSet, 0, nullptr);
        const VkBuffer vertexBuffer = geometry->vertices.handle();
        const VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(commands, 0, 1, &vertexBuffer, &zero);
        vkCmdBindIndexBuffer(commands, geometry->indices.handle(), 0, VK_INDEX_TYPE_UINT32);
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
                // one lets it through, and the sky is not in the level.
                if ((item.polyFlags & (gpu::PF_TRANSLUCENT | gpu::PF_FAKE_BACKDROP)) != 0) continue;
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
    }

    // -- 0. Clustered light culling -------------------------------------------
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->clusters());
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->sceneLayout(), 0, 1, &sceneSet, 0,
                            nullptr);
    vkCmdDispatch(commands, (gpu::CLUSTER_COUNT + 63) / 64, 1, 1); // cluster.comp's local size is 64

    // -- 1. The forward pass --------------------------------------------------
    hdr.transition(commands, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    velocity.transition(commands, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    emission.transition(commands, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    depth.transition(commands, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    std::array<VkRenderingAttachmentInfo, 3> colours{};
    colours[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colours[0].imageView = hdr.view();
    colours[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colours[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colours[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colours[1] = colours[0];
    colours[1].imageView = velocity.view();
    colours[2] = colours[0];
    colours[2].imageView = emission.view(); // UTA-0053
    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = depth.view();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea = regionScissor;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 3;
    rendering.pColorAttachments = colours.data();
    rendering.pDepthAttachment = &depthAttachment;

    const auto drawBatches = [&](bool translucent) {
        vkCmdSetViewport(commands, 0, 1, &regionViewport);
        vkCmdSetScissor(commands, 0, 1, &regionScissor);
        if (geometry->draws.empty()) return;
        const VkBuffer vertexBuffer = geometry->vertices.handle();
        const VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(commands, 0, 1, &vertexBuffer, &zero);
        vkCmdBindIndexBuffer(commands, geometry->indices.handle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->sceneLayout(), 0, 1, &sceneSet,
                                0, nullptr);
        for (const DrawItem& item : geometry->draws) {
            if (((item.polyFlags & gpu::PF_TRANSLUCENT) != 0) != translucent) continue;
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->sceneFor(item.polyFlags));
            // A mirrored mover winds the other way on screen, so its front face is the other one.
            const VkFrontFace front = mirrored[item.objectIndex] ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
            vkCmdSetFrontFace(commands, front);
            const gpu::DrawConstants constants{item.objectIndex, item.materialIndex, item.polyFlags};
            vkCmdPushConstants(commands, pipelines->sceneLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants),
                               &constants);
            vkCmdDrawIndexed(commands, item.indexCount, 1, item.firstIndex, item.firstVertex, 0);
        }
    };

    vkCmdBeginRendering(commands, &rendering);
    drawBatches(false);
    vkCmdEndRendering(commands);

    // -- 2. The translucent pass -----------------------------------------------
    colours[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    rendering.colorAttachmentCount = 1;
    vkCmdBeginRendering(commands, &rendering);
    drawBatches(true);
    vkCmdEndRendering(commands);

    // -- 3. Emissive bloom (UTA-0053) -----------------------------------------
    // Down the chain from the emission target, then back up it, each upsample
    // added into the level above. Every read is clamped to the part of its
    // source the region drew (UTA-0051 SS 4.4).
    const bool bloomOn = enabled(Feature::Bloom, tier);
    if (bloomOn) {
        const float fractionX = static_cast<float>(region.width) / static_cast<float>(extent.width);
        const float fractionY = static_cast<float>(region.height) / static_cast<float>(extent.height);
        emission.transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        const auto bloomPass = [&](Image& target, VkPipeline pipeline, VkDescriptorSet sourceSet, const Image& source,
                                   std::uint32_t mode) {
            const ImageDesc& to = target.desc();
            const ImageDesc& from = source.desc();
            target.transition(commands, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            VkRenderingAttachmentInfo attachment{};
            attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            attachment.imageView = target.view();
            attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            // An upsample adds into what that level's downsample left there.
            attachment.loadOp =
                mode == gpu::BLOOM_UPSAMPLE ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            const VkRect2D area{{0, 0}, {to.width, to.height}};
            const VkViewport levelViewport{0, 0, static_cast<float>(to.width), static_cast<float>(to.height), 0, 1};
            VkRenderingInfo pass{};
            pass.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            pass.renderArea = area;
            pass.layerCount = 1;
            pass.colorAttachmentCount = 1;
            pass.pColorAttachments = &attachment;
            vkCmdBeginRendering(commands, &pass);
            vkCmdSetViewport(commands, 0, 1, &levelViewport);
            vkCmdSetScissor(commands, 0, 1, &area);
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->bloomLayout(), 0, 1,
                                    &sourceSet, 0, nullptr);
            const auto sourceWidth = static_cast<float>(from.width), sourceHeight = static_cast<float>(from.height);
            const gpu::BloomConstants constants{
                {1.0f / sourceWidth, 1.0f / sourceHeight},
                {1.0f / static_cast<float>(to.width), 1.0f / static_cast<float>(to.height)},
                {std::max(0.0f, fractionX - 0.5f / sourceWidth), std::max(0.0f, fractionY - 0.5f / sourceHeight)},
                BLOOM_RADIUS,
                mode};
            vkCmdPushConstants(commands, pipelines->bloomLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants),
                               &constants);
            vkCmdDraw(commands, 3, 1, 0, 0);
            vkCmdEndRendering(commands);
            target.transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        };
        bloomPass(bloom[0], pipelines->bloomDownsample(), emissionSet, emission, gpu::BLOOM_DOWNSAMPLE_FIRST);
        for (std::size_t k = 1; k < BLOOM_LEVELS; ++k)
            bloomPass(bloom[k], pipelines->bloomDownsample(), bloomSets[k - 1], bloom[k - 1], gpu::BLOOM_DOWNSAMPLE);
        for (std::size_t k = BLOOM_LEVELS - 1; k > 0; --k)
            bloomPass(bloom[k - 1], pipelines->bloomUpsample(), bloomSets[k], bloom[k], gpu::BLOOM_UPSAMPLE);
    } else {
        // post.frag reads nothing from it at strength 0, but its set still names it.
        bloom[0].transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    // -- 4. The output stage ---------------------------------------------------
    // A frame drawn at full size is tone mapped straight into `output`. A
    // smaller region is tone mapped into FSR 1's input, which EASU upscales and
    // RCAS sharpens into `output` (UTA-0154).
    hdr.transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const auto fullTargetPass = [&](Image& target, VkPipeline pipeline, VkDescriptorSet set, bool upscaleInputPass) {
        target.transition(commands, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo attachment{};
        attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attachment.imageView = target.view();
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo pass{};
        pass.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        pass.renderArea = scissor;
        pass.layerCount = 1;
        pass.colorAttachmentCount = 1;
        pass.pColorAttachments = &attachment;
        vkCmdBeginRendering(commands, &pass);
        vkCmdSetViewport(commands, 0, 1, &viewport);
        vkCmdSetScissor(commands, 0, 1, &scissor);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->postLayout(), 0, 1, &set, 0,
                                nullptr);
        const gpu::PostConstants constants{EXPOSURE, config.linearOutput ? 1u : 0u, {region.width, region.height},
                                           upscaleInputPass ? 1u : 0u, bloomOn ? BLOOM_STRENGTH : 0.0f};
        vkCmdPushConstants(commands, pipelines->postLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants),
                           &constants);
        vkCmdDraw(commands, 3, 1, 0, 0);
        vkCmdEndRendering(commands);
    };
    if (region.width == extent.width && region.height == extent.height) {
        fullTargetPass(output, pipelines->post(), postSet, false);
    } else {
        fullTargetPass(upscaleInput, pipelines->upscaleInput(), postSet, true);
        upscaleInput.transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        fullTargetPass(upscaled, pipelines->easu(), easuSet, false);
        upscaled.transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        fullTargetPass(output, pipelines->rcas(), rcasSet, false);
    }

    // -- 5. The UI composite seam ----------------------------------------------
    compositeUi(commands);
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
    auto impl = std::make_unique<Impl>();
    impl->config = config;
    UTA_TRY(impl->gpu, Gpu::create(config.validation, config.instanceExtensions, config.createSurface));
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
                              impl->tier));
    if (presenting) {
        UTA_TRY(impl->swapchain, Swapchain::create(*impl->gpu, config.width, config.height));
        impl->adoptSwapchainExtent();
    }
    UTA_CHECK(impl->createTargets());
    UTA_CHECK(impl->createShadowAtlas());
    UTA_CHECK(impl->createSamplers());
    UTA_CHECK(impl->createStandIns());
    return Renderer(std::move(impl));
}

Result<void> Renderer::draw(const ubundle::Bundle& bundle, const Camera& camera) {
    Impl& impl = *impl_;
    // SS 4.3: the swapchain is rebuilt after a resize, when acquire or present
    // said it no longer matches its surface, and while the window is minimised
    // -- so it notices the window coming back.
    if (impl.swapchain && (impl.resized || impl.swapchain->stale() || impl.swapchain->empty())) {
        UTA_TRY(std::unique_ptr<Swapchain> rebuilt,
                Swapchain::create(*impl.gpu, impl.config.width, impl.config.height, impl.swapchain->handle()));
        impl.swapchain = std::move(rebuilt);
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
    if (const BundleShape shape = shapeOf(bundle); !(shape == impl.shape) || !impl.geometry) {
        impl.shape = BundleShape{};
        UTA_CHECK(impl.upload(bundle));
        impl.shape = shape;
    }

    // Before anything is planned: a skipped frame must change nothing (Swapchain.h).
    std::optional<std::uint32_t> image;
    if (impl.swapchain) {
        UTA_TRY(image, impl.swapchain->acquire());
        if (!image) return {}; // out of date: rebuilt at the top of the next frame
    }

    // UTA-0051 SS 4.4: the scale this frame draws at, and the top-left region of
    // the targets it covers. A fixed scale wins over dynamic resolution.
    const TierSettings settings = settingsOf(impl.tier);
    double scale = 1;
    if (impl.config.fixedRenderScale) {
        scale = std::clamp(*impl.config.fixedRenderScale, settings.minimumRenderScale, 1.0);
    } else if (impl.config.dynamicResolution) {
        scale = impl.renderScale;
    }
    const auto regionOf = [scale](std::uint32_t full) {
        const auto scaled = static_cast<std::uint32_t>(std::ceil(scale * full));
        return std::clamp<std::uint32_t>(scaled, 1, full);
    };
    const VkExtent2D region{regionOf(impl.config.width), regionOf(impl.config.height)};

    // SS 4.3: the projection is this library's, and the previous frame's view
    // is cached here rather than supplied -- so a camera that did not move
    // produces zero motion. It, the jitter and the cluster grid take the region.
    const gpu::Mat4 view = viewOf(camera);
    const gpu::Mat4 projection = projectionOf(camera, region.width, region.height);
    const gpu::Mat4 viewProj = multiply(projection, view);
    const gpu::Mat4 drawnProjection =
        impl.jitter ? jittered(projection, haltonJitter(impl.frameIndex), region.width, region.height) : projection;

    gpu::FrameData frame{};
    frame.viewProj = multiply(drawnProjection, view);
    frame.viewProjUnjittered = viewProj;
    frame.previousViewProjUnjittered = impl.previousViewProj.value_or(viewProj);
    frame.view = view;
    frame.eye = camera.location;
    frame.exposure = EXPOSURE;
    frame.nearPlane = camera.nearPlane;
    frame.farPlane = camera.farPlane;
    // scene.frag divides by this to find a pixel's cluster, so it is the region's.
    frame.viewportSize = {static_cast<float>(region.width), static_cast<float>(region.height)};
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - impl.start).count();
    std::vector<gpu::Light> lights = drawnLights(bundle, seconds);
    const ClusterGrid grid = clusterGrid(camera, region.width, region.height);
    std::memcpy(impl.clusterBounds.mapped(), grid.bounds.data(), grid.bounds.size() * sizeof(gpu::ClusterBounds));
    frame.clusterGrid = gpu::CLUSTER_GRID;
    frame.lightCount = static_cast<std::uint32_t>(lights.size());
    frame.clusterDepthScale = grid.depthScale;
    frame.clusterDepthBias = grid.depthBias;
    frame.probeSpacing = impl.probeSpacing;
    frame.probeCount = impl.probeCount;
    frame.probeTableMask = impl.probeTableMask;
    frame.probeLongestRun = impl.probeLongestRun;

    std::vector<gpu::Mat4> models(impl.geometry->objectCount, identity());
    if (bundle.movers)
        for (std::size_t i = 0; i < bundle.movers->size(); ++i) models[i + 1] = moverModel((*bundle.movers)[i]);
    if (impl.previousModels.size() != models.size()) impl.previousModels = models;
    impl.mirrored.assign(models.size(), false);
    for (std::size_t i = 0; i < models.size(); ++i) impl.mirrored[i] = mirrors(models[i]);
    for (std::size_t i = 0; i < models.size(); ++i) {
        const gpu::Object object{models[i], impl.previousModels[i], normalMatrixOf(models[i])};
        std::memcpy(impl.objects.mapped() + i * sizeof(gpu::Object), &object, sizeof(object));
    }

    // SS 4.8: each light's tiles, and which must be drawn this frame -- a mover
    // that moved redraws the lights whose radius its box, before or after, reaches.
    // Planned at the full target size, never the region's: a tile's size follows
    // its light's size on the target, so a change of scale must not re-place it
    // (UTA-0051 SS 4.4).
    std::vector<Box> moved;
    for (std::size_t i = 1; i < models.size(); ++i) {
        if (models[i] == impl.previousModels[i]) continue;
        moved.push_back(worldBox(impl.moverBounds[i - 1], impl.previousModels[i]));
        moved.push_back(worldBox(impl.moverBounds[i - 1], models[i]));
    }
    const ShadowPlan plan =
        impl.shadowPlanner.plan(directLights(bundle), camera, impl.config.width, impl.config.height, moved);
    for (std::size_t i = 0; i < lights.size(); ++i) {
        lights[i].shadowFace = plan.firstFace[i];
        lights[i].shadowFaceCount = plan.faceCount[i];
    }
    if (!lights.empty()) std::memcpy(impl.lights.mapped(), lights.data(), lights.size() * sizeof(gpu::Light));
    if (!plan.faces.empty())
        std::memcpy(impl.shadowFaces.mapped(), plan.faces.data(), plan.faces.size() * sizeof(gpu::ShadowFace));
    frame.shadowFaceCount = static_cast<std::uint32_t>(plan.faces.size());
    std::memcpy(impl.frameData.mapped(), &frame, sizeof(frame));

    // UTA-0051 SS 4.4: the measurement is this call, which waits for the GPU. It
    // leaves out present, which waits for the display under FIFO.
    const auto started = std::chrono::steady_clock::now();
    UTA_CHECK(impl.gpu->run([&](VkCommandBuffer commands) {
        impl.recordFrame(commands, plan, region);
        if (image) impl.swapchain->recordBlit(commands, impl.output, *image);
    }));
    const double milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    if (image) UTA_CHECK(impl.swapchain->present(impl.gpu->queue(), *image));

    // SS 6: how many clusters dropped lights past their cap, read back from the
    // pass that decided -- the frame has finished, so the counts are final.
    impl.stats = {};
    for (std::uint32_t c = 0; c < gpu::CLUSTER_COUNT; ++c) {
        std::uint32_t count = 0;
        std::memcpy(&count, impl.clusterCounts.mapped() + c * sizeof(count), sizeof(count));
        if ((count & gpu::CLUSTER_OVERFLOW) != 0) ++impl.stats.overflowedClusters;
    }
    impl.stats.unshadowedLights = plan.unshadowed;
    impl.stats.renderedShadowTiles = static_cast<std::uint32_t>(plan.draws.size());
    impl.stats.tier = impl.tier;
    impl.stats.renderScale = scale;
    impl.stats.frameMilliseconds = milliseconds;
    impl.drawnScale = scale;
    if (impl.config.dynamicResolution && !impl.config.fixedRenderScale) {
        impl.averagedMilliseconds =
            impl.averagedMilliseconds == 0 ? milliseconds : impl.averagedMilliseconds * 0.8 + milliseconds * 0.2;
        impl.renderScale = nextRenderScale(impl.renderScale, impl.averagedMilliseconds, settings);
    }

    impl.previousViewProj = viewProj;
    impl.previousModels = std::move(models);
    ++impl.frameIndex;
    impl.drawn = true;
    return {};
}

Result<std::vector<std::byte>> Renderer::readback(Target target) {
    Impl& impl = *impl_;
    if (impl.swapchain)
        return fail(ErrorCode::InvalidArgument,
                    "readback is the surfaceless path's; the presenting path's frames go to the swapchain");
    if (!impl.drawn) return fail(ErrorCode::InvalidArgument, "readback before any frame was drawn");
    if (target == Target::Velocity && impl.drawnScale < 1.0)
        return fail(ErrorCode::InvalidArgument,
                    std::format("velocity readback after a frame drawn at scale {}: its region is smaller than the "
                                "target (UTA-0051 SS 4.4)",
                                impl.drawnScale));

    Image& image = target == Target::Colour ? impl.output : impl.velocity;
    const std::size_t pixels = static_cast<std::size_t>(impl.config.width) * impl.config.height;
    // RGBA8 and RG16F are both four bytes a pixel.
    UTA_TRY(Buffer host, Buffer::create(*impl.gpu, pixels * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true));
    UTA_CHECK(impl.gpu->run([&](VkCommandBuffer commands) {
        image.transition(commands, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {impl.config.width, impl.config.height, 1};
        vkCmdCopyImageToBuffer(commands, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, host.handle(), 1, &copy);
    }));

    if (target == Target::Colour) return std::vector<std::byte>(host.mapped(), host.mapped() + pixels * 4);

    std::vector<std::byte> floats(pixels * 2 * sizeof(float));
    for (std::size_t i = 0; i < pixels * 2; ++i) {
        std::uint16_t half = 0;
        std::memcpy(&half, host.mapped() + i * 2, sizeof(half));
        const float value = halfToFloat(half);
        std::memcpy(floats.data() + i * sizeof(float), &value, sizeof(value));
    }
    return floats;
}

FrameStats Renderer::lastFrameStats() const noexcept { return impl_->stats; }

void Renderer::setJitter(bool enabled) noexcept { impl_->jitter = enabled; }

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
