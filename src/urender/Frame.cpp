// The Renderer: its targets, and the frame it draws --
// docs/specs/UTA-0014-vulkan-draw-path.md SS 4.3, SS 4.10 and SS 4.11.
//
// THE FRAME, IN ORDER:
//   0. clustered light culling, a compute pass (SS 4.6);
//   1. the forward pass: every opaque, masked and sky batch, writing colour,
//      velocity and depth;
//   2. the translucent pass: colour only, depth-tested and not written;
//   3. the output stage: exposure and the tone map into the _SRGB target;
//   4. the UI composite seam, after the output stage (SS 4.11 provision 3).
//
// THE SURFACELESS PATH SUBMITS AND WAITS. draw returns when the frame is
// finished, so readback needs no synchronisation of its own.

#include "urender/Renderer.h"

#include "urender/Clusters.h"
#include "urender/Device.h"
#include "urender/Geometry.h"
#include "urender/Lights.h"
#include "urender/Materials.h"
#include "urender/Pipelines.h"
#include "urender/Placement.h"
#include "urender/Resources.h"
#include "urender/ShaderTypes.h"

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

/// SS 4.10: fixed, with no adaptation. 1.0 carries UTA-0112 SS 4.3's unit
/// surface to 0.978 through PBR Neutral's shoulder -- sRGB 253. No exposure
/// reaches 255 exactly short of about thirteen, which would wash out every
/// surface below the unit one to reach it.
constexpr float EXPOSURE = 1.0f;

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
struct BundleShape {
    const ubundle::Bundle* address = nullptr;
    std::size_t vertices = 0, indices = 0, batches = 0, textures = 0, materials = 0, movers = 0, moverIndices = 0,
                lights = 0;
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
    shape.sample = fnv.value();
    if (bundle.geometry) {
        shape.vertices = bundle.geometry->vertices.size();
        shape.indices = bundle.geometry->indices.size();
        shape.batches = bundle.geometry->batches.size();
    }
    if (bundle.textures) shape.textures = bundle.textures->size();
    if (bundle.materials) shape.materials = bundle.materials->size();
    if (bundle.lights) shape.lights = bundle.lights->size();
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

    VkSampler materialSampler = VK_NULL_HANDLE;
    VkSampler nearestSampler = VK_NULL_HANDLE;
    VkSampler shadowSampler = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet sceneSet = VK_NULL_HANDLE;
    VkDescriptorSet postSet = VK_NULL_HANDLE;

    Image hdr, velocity, depth, output;
    Image shadowAtlas;

    Buffer frameData, objects, lights;
    Buffer clusterCounts, clusterIndices, clusterBounds;
    /// Stand-ins for the passes not yet drawing: probes and shadow faces. A
    /// storage binding cannot be left empty.
    Buffer probeGrid, probes, shadowFaces;

    BundleShape shape;
    std::optional<MaterialSet> materials;
    std::optional<SceneGeometry> geometry;

    std::optional<gpu::Mat4> previousViewProj;
    std::vector<gpu::Mat4> previousModels;
    bool jitter = false;
    std::uint64_t frameIndex = 0;
    /// SS 4.9's clock: a flickering light's phase is measured from here.
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    bool drawn = false;
    FrameStats stats;

    ~Impl() {
        if (!gpu) return;
        const VkDevice device = gpu->device();
        vkDeviceWaitIdle(device);
        if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, pool, nullptr);
        for (VkSampler sampler : {materialSampler, nearestSampler, shadowSampler})
            if (sampler != VK_NULL_HANDLE) vkDestroySampler(device, sampler, nullptr);
    }

    Result<void> createTargets();
    Result<void> createSamplers();
    Result<void> createStandIns();
    Result<void> upload(const ubundle::Bundle& bundle);
    Result<void> writeDescriptors();
    void recordFrame(VkCommandBuffer commands);
};

Result<void> Renderer::Impl::createTargets() {
    const std::uint32_t w = config.width, h = config.height;
    UTA_TRY(hdr, Image::create(*gpu, {HDR_FORMAT, w, h, 1,
                                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT}));
    UTA_TRY(velocity, Image::create(*gpu, {VELOCITY_FORMAT, w, h, 1,
                                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT}));
    UTA_TRY(depth, Image::create(*gpu, {DEPTH_FORMAT, w, h, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT}));
    UTA_TRY(output, Image::create(*gpu, {OUTPUT_FORMAT, w, h, 1,
                                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT}));
    // A one-texel atlas until shadows draw: the binding must hold something.
    UTA_TRY(shadowAtlas, Image::create(*gpu, {DEPTH_FORMAT, 1, 1, 1,
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
    for (Buffer* buffer : {&probeGrid, &probes, &shadowFaces}) {
        UTA_TRY(*buffer, Buffer::create(*gpu, 256, storage, true));
        std::memset(buffer->mapped(), 0, 256);
    }
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
    previousModels.clear();
    return writeDescriptors();
}

Result<void> Renderer::Impl::writeDescriptors() {
    const VkDevice device = gpu->device();
    if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, pool, nullptr);
    pool = VK_NULL_HANDLE;

    const auto textureCount = static_cast<std::uint32_t>(materials->textures().size());
    const std::array sizes = {
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, gpu::SHADOW_FACES + 1},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textureCount + 2},
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 2;
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

    const VkDescriptorSetLayout postLayout = pipelines->postSetLayout();
    VkDescriptorSetAllocateInfo postAllocate{};
    postAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    postAllocate.descriptorPool = pool;
    postAllocate.descriptorSetCount = 1;
    postAllocate.pSetLayouts = &postLayout;
    UTA_CHECK(check(vkAllocateDescriptorSets(device, &postAllocate, &postSet), "vkAllocateDescriptorSets (post)"));

    const std::array<const Buffer*, gpu::SHADOW_FACES + 1> buffers = {
        &frameData, &objects, &materials->records(), &lights, &clusterCounts,
        &clusterIndices, &clusterBounds, &probeGrid, &probes, &shadowFaces};
    std::array<VkDescriptorBufferInfo, gpu::SHADOW_FACES + 1> bufferInfos{};
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

    vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return {};
}

void Renderer::Impl::recordFrame(VkCommandBuffer commands) {
    const VkExtent2D extent{config.width, config.height};
    const VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    const VkRect2D scissor{{0, 0}, extent};

    // -- 0. Clustered light culling -------------------------------------------
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->clusters());
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines->sceneLayout(), 0, 1, &sceneSet, 0,
                            nullptr);
    vkCmdDispatch(commands, (gpu::CLUSTER_COUNT + 63) / 64, 1, 1); // cluster.comp's local size is 64

    // -- 1. The forward pass --------------------------------------------------
    hdr.transition(commands, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    velocity.transition(commands, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    depth.transition(commands, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    std::array<VkRenderingAttachmentInfo, 2> colours{};
    colours[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colours[0].imageView = hdr.view();
    colours[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colours[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colours[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colours[1] = colours[0];
    colours[1].imageView = velocity.view();
    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = depth.view();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea = scissor;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 2;
    rendering.pColorAttachments = colours.data();
    rendering.pDepthAttachment = &depthAttachment;

    const auto drawBatches = [&](bool translucent) {
        vkCmdSetViewport(commands, 0, 1, &viewport);
        vkCmdSetScissor(commands, 0, 1, &scissor);
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

    // -- 3. The output stage ---------------------------------------------------
    hdr.transition(commands, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    output.transition(commands, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo outputAttachment{};
    outputAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    outputAttachment.imageView = output.view();
    outputAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    outputAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    outputAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo post{};
    post.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    post.renderArea = scissor;
    post.layerCount = 1;
    post.colorAttachmentCount = 1;
    post.pColorAttachments = &outputAttachment;
    vkCmdBeginRendering(commands, &post);
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->post());
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines->postLayout(), 0, 1, &postSet, 0,
                            nullptr);
    const gpu::PostConstants constants{EXPOSURE, config.linearOutput ? 1u : 0u};
    vkCmdPushConstants(commands, pipelines->postLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants),
                       &constants);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);

    // -- 4. The UI composite seam ----------------------------------------------
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
    if (config.surface != 0)
        return fail(ErrorCode::InvalidArgument,
                    "the presenting path is not built yet; pass surface = 0 for the surfaceless path");

    auto impl = std::make_unique<Impl>();
    impl->config = config;
    UTA_TRY(impl->gpu, Gpu::create(config.validation));
    UTA_TRY(impl->pipelines, Pipelines::create(*impl->gpu, {HDR_FORMAT, VELOCITY_FORMAT, DEPTH_FORMAT, OUTPUT_FORMAT}));
    UTA_CHECK(impl->createTargets());
    UTA_CHECK(impl->createSamplers());
    UTA_CHECK(impl->createStandIns());
    return Renderer(std::move(impl));
}

Result<void> Renderer::draw(const ubundle::Bundle& bundle, const Camera& camera) {
    Impl& impl = *impl_;
    if (const BundleShape shape = shapeOf(bundle); !(shape == impl.shape) || !impl.geometry) {
        impl.shape = BundleShape{};
        UTA_CHECK(impl.upload(bundle));
        impl.shape = shape;
    }

    // SS 4.3: the projection is this library's, and the previous frame's view
    // is cached here rather than supplied -- so a camera that did not move
    // produces zero motion.
    const gpu::Mat4 view = viewOf(camera);
    const gpu::Mat4 projection = projectionOf(camera, impl.config.width, impl.config.height);
    const gpu::Mat4 viewProj = multiply(projection, view);
    const gpu::Mat4 drawnProjection =
        impl.jitter ? jittered(projection, haltonJitter(impl.frameIndex), impl.config.width, impl.config.height)
                    : projection;

    gpu::FrameData frame{};
    frame.viewProj = multiply(drawnProjection, view);
    frame.viewProjUnjittered = viewProj;
    frame.previousViewProjUnjittered = impl.previousViewProj.value_or(viewProj);
    frame.view = view;
    frame.eye = camera.location;
    frame.exposure = EXPOSURE;
    frame.nearPlane = camera.nearPlane;
    frame.farPlane = camera.farPlane;
    frame.viewportSize = {static_cast<float>(impl.config.width), static_cast<float>(impl.config.height)};
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - impl.start).count();
    const std::vector<gpu::Light> lights = drawnLights(bundle, seconds);
    if (!lights.empty()) std::memcpy(impl.lights.mapped(), lights.data(), lights.size() * sizeof(gpu::Light));
    const ClusterGrid grid = clusterGrid(camera, impl.config.width, impl.config.height);
    std::memcpy(impl.clusterBounds.mapped(), grid.bounds.data(), grid.bounds.size() * sizeof(gpu::ClusterBounds));
    frame.clusterGrid = gpu::CLUSTER_GRID;
    frame.lightCount = static_cast<std::uint32_t>(lights.size());
    frame.clusterDepthScale = grid.depthScale;
    frame.clusterDepthBias = grid.depthBias;
    std::memcpy(impl.frameData.mapped(), &frame, sizeof(frame));

    std::vector<gpu::Mat4> models(impl.geometry->objectCount, identity());
    if (bundle.movers)
        for (std::size_t i = 0; i < bundle.movers->size(); ++i) models[i + 1] = moverModel((*bundle.movers)[i]);
    if (impl.previousModels.size() != models.size()) impl.previousModels = models;
    for (std::size_t i = 0; i < models.size(); ++i) {
        const gpu::Object object{models[i], impl.previousModels[i], normalMatrixOf(models[i])};
        std::memcpy(impl.objects.mapped() + i * sizeof(gpu::Object), &object, sizeof(object));
    }

    UTA_CHECK(impl.gpu->run([&](VkCommandBuffer commands) { impl.recordFrame(commands); }));

    // SS 6: how many clusters dropped lights past their cap, read back from the
    // pass that decided -- the frame has finished, so the counts are final.
    impl.stats = {};
    for (std::uint32_t c = 0; c < gpu::CLUSTER_COUNT; ++c) {
        std::uint32_t count = 0;
        std::memcpy(&count, impl.clusterCounts.mapped() + c * sizeof(count), sizeof(count));
        if ((count & gpu::CLUSTER_OVERFLOW) != 0) ++impl.stats.overflowedClusters;
    }

    impl.previousViewProj = viewProj;
    impl.previousModels = std::move(models);
    ++impl.frameIndex;
    impl.drawn = true;
    return {};
}

Result<std::vector<std::byte>> Renderer::readback(Target target) {
    Impl& impl = *impl_;
    if (!impl.drawn) return fail(ErrorCode::InvalidArgument, "readback before any frame was drawn");

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

} // namespace uta::urender
