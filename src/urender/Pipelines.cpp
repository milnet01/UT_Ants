// The descriptor layouts and the pipelines -- Pipelines.h.

#include "urender/Pipelines.h"

#include "ubundle/Bundle.h"
#include "urender/Geometry.h"
#include "urender/ShaderTypes.h"
#include "urender/Tiers.h"

#include "bloom.frag.spv.h"
#include "cluster.comp.spv.h"
#include "fog_integrate.comp.spv.h"
#include "fog_scatter.comp.spv.h"
#include "fsr_easu.frag.spv.h"
#include "flame.frag.spv.h"
#include "flame.vert.spv.h"
#include "mote.frag.spv.h"
#include "mote.vert.spv.h"
#include "fsr_rcas.frag.spv.h"
#include "post.frag.spv.h"
#include "post.vert.spv.h"
#include "scene.frag.spv.h"
#include "scene.vert.spv.h"
#include "shadow.frag.spv.h"
#include "shadow_reduce.comp.spv.h"
#include "shadow.vert.spv.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace uta::urender {

namespace {

/// A ceiling on the texture array however generous the device: a bundle
/// needing more than this is refused rather than bound.
constexpr std::uint32_t TEXTURE_CEILING = 65536;

struct Module {
    VkDevice device = VK_NULL_HANDLE;
    VkShaderModule handle = VK_NULL_HANDLE;
    ~Module() {
        if (handle != VK_NULL_HANDLE) vkDestroyShaderModule(device, handle, nullptr);
    }
};

VkPipelineShaderStageCreateInfo stage(VkShaderStageFlagBits kind, VkShaderModule module) {
    VkPipelineShaderStageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage = kind;
    info.module = module;
    info.pName = "main";
    return info;
}

/// The device every pipeline here is made on, and the flags it is made with:
/// UTA-0352's statistics capture where the Gpu keeps them, else none.
struct Making {
    VkDevice device = VK_NULL_HANDLE;
    VkPipelineCreateFlags flags = 0;
};

/// The states every graphics pipeline here starts from; each builder overrides
/// only what its pass does differently. Depth is tested or-equal -- so the
/// sky's depth at exactly the far plane draws where nothing nearer did -- and
/// written, and applies only where the pass has a depth target. Viewport and
/// scissor are dynamic, set per pass. The front face is left
/// counter-clockwise, Vulkan's zero, unless a builder says otherwise.
struct GraphicsStates {
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    VkPipelineViewportStateCreateInfo viewport{};
    VkPipelineRasterizationStateCreateInfo raster{};
    VkPipelineMultisampleStateCreateInfo multisample{};
    VkPipelineDepthStencilStateCreateInfo depth{};
    VkPipelineColorBlendStateCreateInfo blend{};
    VkPipelineRenderingCreateInfo rendering{}; ///< dynamic rendering: no VkRenderPass
    std::span<const VkDynamicState> dynamics;
};

constexpr std::array VIEWPORT_AND_SCISSOR = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

GraphicsStates sharedStates() {
    GraphicsStates states;
    states.vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    states.assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    states.assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    states.viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    states.viewport.viewportCount = 1;
    states.viewport.scissorCount = 1;
    states.raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    states.raster.polygonMode = VK_POLYGON_MODE_FILL;
    states.raster.cullMode = VK_CULL_MODE_NONE;
    states.raster.lineWidth = 1.0f;
    states.multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    states.multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    states.depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    states.depth.depthTestEnable = VK_TRUE;
    states.depth.depthWriteEnable = VK_TRUE;
    states.depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    states.blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    states.rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    states.dynamics = VIEWPORT_AND_SCISSOR;
    return states;
}

/// The pipeline `states` describe; `pass` names it in a failure.
Result<VkPipeline> graphicsPipeline(const Making& making, VkPipelineLayout layout,
                                    std::span<const VkPipelineShaderStageCreateInfo> stages,
                                    const GraphicsStates& states, std::string_view pass) {
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(states.dynamics.size());
    dynamic.pDynamicStates = states.dynamics.data();

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &states.rendering;
    info.flags = making.flags;
    info.stageCount = static_cast<std::uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &states.vertexInput;
    info.pInputAssemblyState = &states.assembly;
    info.pViewportState = &states.viewport;
    info.pRasterizationState = &states.raster;
    info.pMultisampleState = &states.multisample;
    info.pDepthStencilState =
        states.rendering.depthAttachmentFormat != VK_FORMAT_UNDEFINED ? &states.depth : nullptr;
    info.pColorBlendState = &states.blend;
    info.pDynamicState = &dynamic;
    info.layout = layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    UTA_CHECK(check(vkCreateGraphicsPipelines(making.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
                    std::format("vkCreateGraphicsPipelines ({})", pass)));
    return pipeline;
}

/// How a scene pipeline blends, and scene_'s first index (Pipelines.h).
enum class Blending : std::uint8_t { Opaque, Translucent, Modulated };
constexpr std::array BLENDINGS = {Blending::Opaque, Blending::Translucent, Blending::Modulated};

struct SceneVariant {
    /// Opaque writes depth; Translucent and Modulated draw in the blended
    /// pass: colour alone, depth tested and not written. UTA-0271: Modulated
    /// multiplies, as UT99 draws PF_Modulated.
    Blending blending;
    bool twoSided;
    /// UTA-0260: the vertex stage alone, writing depth and no colour.
    bool depthOnly;
};

/// scene.frag's specialization constants, in constant_id order.
struct SceneConstants {
    ParallaxSteps parallax;
    VkBool32 waterLook;
    VkBool32 tileVariation; ///< UTA-0180
    VkBool32 caustics;      ///< UTA-0215
    VkBool32 tileShuffle;   ///< UTA-0277
    VkBool32 lightTerms;    ///< UTA-0292: Config::lightTerms
};

Result<VkPipeline> scenePipeline(const Making& making, VkPipelineLayout layout, const TargetFormats& formats,
                                 VkShaderModule vertex, VkShaderModule fragment, SceneVariant variant,
                                 SceneConstants constants) {
    // UTA-0040 SS 4.4: scene.frag's constant_id 0 and 1, so a tier with no
    // steps compiles the march out. UTA-0089 SS 4.5: constant_id 2, so a tier
    // without the water look compiles it out.
    const std::array entries = {
        VkSpecializationMapEntry{0, offsetof(SceneConstants, parallax) + offsetof(ParallaxSteps, minimum),
                                 sizeof(ParallaxSteps::minimum)},
        VkSpecializationMapEntry{1, offsetof(SceneConstants, parallax) + offsetof(ParallaxSteps, maximum),
                                 sizeof(ParallaxSteps::maximum)},
        VkSpecializationMapEntry{2, offsetof(SceneConstants, waterLook), sizeof(VkBool32)},
        VkSpecializationMapEntry{3, offsetof(SceneConstants, tileVariation), sizeof(VkBool32)}, // UTA-0180
        VkSpecializationMapEntry{4, offsetof(SceneConstants, caustics), sizeof(VkBool32)},      // UTA-0215
        VkSpecializationMapEntry{5, offsetof(SceneConstants, tileShuffle), sizeof(VkBool32)},   // UTA-0277
        VkSpecializationMapEntry{6, offsetof(SceneConstants, lightTerms), sizeof(VkBool32)},    // UTA-0292
    };
    const VkSpecializationInfo specialization{static_cast<std::uint32_t>(entries.size()), entries.data(),
                                              sizeof(constants), &constants};
    std::array stages = {stage(VK_SHADER_STAGE_VERTEX_BIT, vertex), stage(VK_SHADER_STAGE_FRAGMENT_BIT, fragment)};
    stages[1].pSpecializationInfo = &specialization;

    // ubundle::GeometryVertex, uploaded as it is laid out in memory; then a
    // SurfaceVertex, a stream of its own: UTA-0164 SS 4.5's occlusion uv and
    // UTA-0326 SS 4.5's mask chart and texel.
    const std::array bindings = {
        VkVertexInputBindingDescription{0, sizeof(ubundle::GeometryVertex), VK_VERTEX_INPUT_RATE_VERTEX},
        VkVertexInputBindingDescription{1, sizeof(SurfaceVertex), VK_VERTEX_INPUT_RATE_VERTEX},
    };
    const std::array attributes = {
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                          offsetof(ubundle::GeometryVertex, position)},
        VkVertexInputAttributeDescription{1, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                          offsetof(ubundle::GeometryVertex, normal)},
        VkVertexInputAttributeDescription{2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ubundle::GeometryVertex, u)},
        // UTA-0156 SS 4.4: the vertex's zone, for its ambient light.
        VkVertexInputAttributeDescription{3, 0, VK_FORMAT_R8_UINT, offsetof(ubundle::GeometryVertex, zone)},
        // UTA-0055 SS 4.5: the zone byte and the three sway bytes after it, as
        // one four-byte read; three-byte formats are not widely supported.
        VkVertexInputAttributeDescription{7, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(ubundle::GeometryVertex, zone)},
        VkVertexInputAttributeDescription{4, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(SurfaceVertex, occlusionUv)},
        VkVertexInputAttributeDescription{5, 1, VK_FORMAT_R32_UINT, offsetof(SurfaceVertex, maskChart)},
        VkVertexInputAttributeDescription{6, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(SurfaceVertex, maskTexel)},
    };
    GraphicsStates states = sharedStates();
    states.vertexInput.vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size());
    states.vertexInput.pVertexBindingDescriptions = bindings.data();
    states.vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    states.vertexInput.pVertexAttributeDescriptions = attributes.data();
    const bool translucent = variant.blending != Blending::Opaque;

    states.raster.cullMode = variant.twoSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
    // UTA-0109 SS 4.3 winds each triangle so its corners turn about the stored
    // normal; the projection's +Y-down flip makes that clockwise on screen. It is
    // dynamic state, set per draw: a mirrored mover reverses it (Frame.cpp).
    states.raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    // UTA-0311: a mover's depth is biased away so the level wins a coplanar
    // tie. Dynamic too, set per draw: zero for the level (Frame.cpp).
    states.raster.depthBiasEnable = VK_TRUE;

    // SS 4.5: translucent batches are depth-tested and not depth-written.
    states.depth.depthWriteEnable = translucent ? VK_FALSE : VK_TRUE;

    const VkColorComponentFlags all = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                      | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendAttachmentState opaque{};
    opaque.colorWriteMask = all;
    VkPipelineColorBlendAttachmentState blended = opaque;
    // UT99's translucency: the surface adds, and hides what is behind it in
    // proportion to its own colour. Order-independent, which matters because
    // a batch is a whole level's run and cannot be sorted.
    blended.blendEnable = VK_TRUE;
    blended.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blended.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    blended.colorBlendOp = VK_BLEND_OP_ADD;
    // The target's alpha is kept. Nothing reads it: post.frag takes the
    // colour target's .rgb alone.
    blended.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blended.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blended.alphaBlendOp = VK_BLEND_OP_ADD;
    // UTA-0271: UT99's modulation multiplies what is behind by the surface.
    // scene.frag outputs the whole factor, so the colour behind takes it once.
    VkPipelineColorBlendAttachmentState modulating = blended;
    modulating.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
    modulating.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;

    // The opaque pass writes colour, velocity and UTA-0053's emission; the
    // translucent pass binds the colour attachment alone, which is how it writes
    // no velocity (SS 4.11).
    const std::array opaqueAttachments = {opaque, opaque, opaque};
    if (variant.depthOnly) {
        states.blend.attachmentCount = 0;
    } else if (translucent) {
        states.blend.attachmentCount = 1;
        states.blend.pAttachments = variant.blending == Blending::Modulated ? &modulating : &blended;
    } else {
        states.blend.attachmentCount = 3;
        states.blend.pAttachments = opaqueAttachments.data();
    }

    // Front face is core dynamic state from Vulkan 1.3 (VK_EXT_extended_dynamic_state
    // was promoted), so it needs no feature beyond SS 4.4's.
    static constexpr std::array dynamics = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                            VK_DYNAMIC_STATE_FRONT_FACE, VK_DYNAMIC_STATE_DEPTH_BIAS};
    states.dynamics = dynamics;

    // UTA-0053: the opaque pass also writes emission, for the bloom chain.
    const std::array colourFormats = {formats.hdr, formats.velocity, formats.emission};
    states.rendering.colorAttachmentCount = variant.depthOnly ? 0 : translucent ? 1 : 3;
    states.rendering.pColorAttachmentFormats = colourFormats.data();
    states.rendering.depthAttachmentFormat = formats.depth;

    // UTA-0260: depth alone needs no fragment stage, and the vertex stage is
    // the forward pass's own, so both passes place a surface at one depth.
    const std::span<const VkPipelineShaderStageCreateInfo> drawn(stages.data(), variant.depthOnly ? 1 : stages.size());
    return graphicsPipeline(making, layout, drawn, states, "scene");
}

/// UTA-0263 SS 4.4's camera-facing flames: no vertex input -- flame.vert
/// builds each quad from its instance -- both faces, depth-tested and not
/// written, and additive into colour and emission. The pass binds all three
/// forward targets, so its formats match the opaque pass.
Result<VkPipeline> flamePipeline(const Making& making, VkPipelineLayout layout, const TargetFormats& formats,
                                 VkShaderModule vertex, VkShaderModule fragment) {
    const std::array stages = {stage(VK_SHADER_STAGE_VERTEX_BIT, vertex), stage(VK_SHADER_STAGE_FRAGMENT_BIT, fragment)};

    GraphicsStates states = sharedStates();
    states.raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    states.depth.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState additive{};
    additive.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    additive.blendEnable = VK_TRUE;
    additive.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    additive.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    additive.colorBlendOp = VK_BLEND_OP_ADD;
    // The target's alpha is kept, as the translucent scene pass keeps it.
    // Nothing reads it: post.frag and bloom.frag take .rgb alone.
    additive.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    additive.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    additive.alphaBlendOp = VK_BLEND_OP_ADD;
    // One state for all three: independentBlend is not enabled. Velocity is
    // added too, and flame.frag writes 0 there, so it stays exactly as it was.
    const std::array attachments = {additive, additive, additive};
    states.blend.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    states.blend.pAttachments = attachments.data();

    const std::array colourFormats = {formats.hdr, formats.velocity, formats.emission};
    states.rendering.colorAttachmentCount = static_cast<std::uint32_t>(colourFormats.size());
    states.rendering.pColorAttachmentFormats = colourFormats.data();
    states.rendering.depthAttachmentFormat = formats.depth;
    return graphicsPipeline(making, layout, stages, states, "flame");
}

/// SS 4.8's tile pass. Both faces cast -- a shadow must not leak through a
/// wall seen edge-on from its back -- and a slope-scaled rasterisation depth
/// bias is what keeps a lit surface from shadowing itself. shadows.glsl applies
/// no normal offset.
Result<VkPipeline> shadowPipeline(const Making& making, VkPipelineLayout layout, VkFormat depthFormat,
                                  VkShaderModule vertex, VkShaderModule fragment) {
    const std::array stages = {stage(VK_SHADER_STAGE_VERTEX_BIT, vertex),
                               stage(VK_SHADER_STAGE_FRAGMENT_BIT, fragment)};
    const VkVertexInputBindingDescription binding{0, sizeof(ubundle::GeometryVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array attributes = {
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                          offsetof(ubundle::GeometryVertex, position)},
        VkVertexInputAttributeDescription{1, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                          offsetof(ubundle::GeometryVertex, normal)},
        VkVertexInputAttributeDescription{2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ubundle::GeometryVertex, u)},
    };
    GraphicsStates states = sharedStates();
    states.vertexInput.vertexBindingDescriptionCount = 1;
    states.vertexInput.pVertexBindingDescriptions = &binding;
    states.vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    states.vertexInput.pVertexAttributeDescriptions = attributes.data();
    states.raster.depthBiasEnable = VK_TRUE;
    states.raster.depthBiasConstantFactor = 1.25f;
    states.raster.depthBiasSlopeFactor = 1.75f;
    states.rendering.depthAttachmentFormat = depthFormat;
    return graphicsPipeline(making, layout, stages, states, "shadow");
}

/// A full-target pass. `additive` adds into what the target holds -- UTA-0053's
/// bloom upsample -- where every other use replaces it.
Result<VkPipeline> postPipeline(const Making& making, VkPipelineLayout layout, VkFormat output, VkShaderModule vertex,
                                VkShaderModule fragment, bool additive = false) {
    const std::array stages = {stage(VK_SHADER_STAGE_VERTEX_BIT, vertex),
                               stage(VK_SHADER_STAGE_FRAGMENT_BIT, fragment)};
    GraphicsStates states = sharedStates();
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                                | VK_COLOR_COMPONENT_A_BIT;
    if (additive) {
        attachment.blendEnable = VK_TRUE;
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.colorBlendOp = VK_BLEND_OP_ADD;
        // The source's alpha replaces the target's. Nothing reads it:
        // bloom.frag writes 1 and takes .rgb alone, as post.frag does.
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    states.blend.attachmentCount = 1;
    states.blend.pAttachments = &attachment;
    states.rendering.colorAttachmentCount = 1;
    states.rendering.pColorAttachmentFormats = &output;
    return graphicsPipeline(making, layout, stages, states, "post");
}

} // namespace

Result<VkShaderModule> shaderModule(VkDevice device, std::span<const std::uint32_t> words) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = words.size_bytes();
    info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    UTA_CHECK(check(vkCreateShaderModule(device, &info, nullptr, &module), "vkCreateShaderModule"));
    return module;
}

Result<std::unique_ptr<Pipelines>> Pipelines::create(const Gpu& gpu, const TargetFormats& formats, Tier tier,
                                                     bool lightTerms) {
    std::unique_ptr<Pipelines> p(new Pipelines());
    p->device_ = gpu.device();
    const VkDevice device = p->device_;
    p->captured_ = gpu.capturesShaderStatistics();
    const Making making{device, p->captured_ ? VkPipelineCreateFlags{VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR}
                                             : VkPipelineCreateFlags{0}};

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(gpu.physical(), &properties);
    const VkPhysicalDeviceLimits& limits = properties.limits;
    // A combined image sampler counts against both the sampled-image and the
    // sampler limits; two of each are kept back, for the shadow atlas and the
    // fog volume, which the fragment stage reads beside the textures.
    constexpr std::uint32_t RESERVED = 2;
    // maxPerStageResources counts every resource the fragment stage sees
    // together: the scene set's storage buffers, the two kept back, the
    // textures and the colour outputs, here bounded by maxColorAttachments
    // (review-code 2026-10-15).
    const std::uint32_t besideTextures = (gpu::MASK_PAIRS - gpu::FRAME + 1) + limits.maxColorAttachments;
    // A stage too small to hold even those leaves room for the two kept back
    // alone, so the capacity below is 0 and Frame.cpp refuses every bundle at
    // load rather than binding past the limit.
    const std::uint32_t stageRoom =
        limits.maxPerStageResources > besideTextures ? limits.maxPerStageResources - besideTextures : RESERVED;
    p->textureCapacity_ = std::min({limits.maxPerStageDescriptorSampledImages, limits.maxPerStageDescriptorSamplers,
                                    limits.maxDescriptorSetSampledImages, limits.maxDescriptorSetSamplers, stageRoom,
                                    TEXTURE_CEILING + RESERVED})
                          - RESERVED;

    // -- The scene set ------------------------------------------------------
    const VkShaderStageFlags everyStage =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    std::array<VkDescriptorSetLayoutBinding, gpu::TEXTURES + 1> bindings{};
    std::array<VkDescriptorBindingFlags, gpu::TEXTURES + 1> bindingFlags{};
    for (std::uint32_t i = gpu::FRAME; i <= gpu::MASK_PAIRS; ++i)
        bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, everyStage, nullptr};
    // UTA-0015: the fog's first stage reads shadows too.
    bindings[gpu::SHADOW_ATLAS] = {gpu::SHADOW_ATLAS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                   VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[gpu::FOG_VOLUME] = {gpu::FOG_VOLUME, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[gpu::TEXTURES] = {gpu::TEXTURES, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, p->textureCapacity_,
                               VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    // descriptorBindingPartiallyBound is what lets an absent :emit slot go
    // unbound; the variable count is what sizes the array to the bundle.
    bindingFlags[gpu::TEXTURES] =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

    VkDescriptorSetLayoutBindingFlagsCreateInfo flagInfo{};
    flagInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    flagInfo.bindingCount = static_cast<std::uint32_t>(bindingFlags.size());
    flagInfo.pBindingFlags = bindingFlags.data();
    VkDescriptorSetLayoutCreateInfo sceneInfo{};
    sceneInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    sceneInfo.pNext = &flagInfo;
    sceneInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    sceneInfo.pBindings = bindings.data();
    UTA_CHECK(created(vkCreateDescriptorSetLayout(device, &sceneInfo, nullptr, &p->sceneSetLayout_), p->sceneSetLayout_,
                    "vkCreateDescriptorSetLayout (scene)"));

    const VkPushConstantRange drawRange{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                        sizeof(gpu::DrawConstants)};
    VkPipelineLayoutCreateInfo sceneLayoutInfo{};
    sceneLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    sceneLayoutInfo.setLayoutCount = 1;
    sceneLayoutInfo.pSetLayouts = &p->sceneSetLayout_;
    sceneLayoutInfo.pushConstantRangeCount = 1;
    sceneLayoutInfo.pPushConstantRanges = &drawRange;
    UTA_CHECK(created(vkCreatePipelineLayout(device, &sceneLayoutInfo, nullptr, &p->sceneLayout_), p->sceneLayout_,
                    "vkCreatePipelineLayout (scene)"));

    // -- The post set -------------------------------------------------------
    // Binding 0 is the pass's source; binding 1 is UTA-0053's bloom, which only
    // post.frag reads.
    const std::array postBindings = {
        VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                                     nullptr},
        VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                                     nullptr},
    };
    VkDescriptorSetLayoutCreateInfo postInfo{};
    postInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    postInfo.bindingCount = static_cast<std::uint32_t>(postBindings.size());
    postInfo.pBindings = postBindings.data();
    UTA_CHECK(created(vkCreateDescriptorSetLayout(device, &postInfo, nullptr, &p->postSetLayout_), p->postSetLayout_,
                    "vkCreateDescriptorSetLayout (post)"));
    const VkPushConstantRange postRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(gpu::PostConstants)};
    VkPipelineLayoutCreateInfo postLayoutInfo{};
    postLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    postLayoutInfo.setLayoutCount = 1;
    postLayoutInfo.pSetLayouts = &p->postSetLayout_;
    postLayoutInfo.pushConstantRangeCount = 1;
    postLayoutInfo.pPushConstantRanges = &postRange;
    UTA_CHECK(created(vkCreatePipelineLayout(device, &postLayoutInfo, nullptr, &p->postLayout_), p->postLayout_,
                    "vkCreatePipelineLayout (post)"));

    // -- The pipelines ------------------------------------------------------
    Module sceneVertex{device}, sceneFragment{device}, postVertex{device}, postFragment{device};
    UTA_TRY(sceneVertex.handle, shaderModule(device, scene_vert_spv));
    UTA_TRY(sceneFragment.handle, shaderModule(device, scene_frag_spv));
    UTA_TRY(postVertex.handle, shaderModule(device, post_vert_spv));
    UTA_TRY(postFragment.handle, shaderModule(device, post_frag_spv));

    const SceneConstants sceneConstants{parallaxStepsOf(tier),
                                        enabled(Feature::WaterLook, tier) ? VK_TRUE : VK_FALSE,
                                        enabled(Feature::TileVariation, tier) ? VK_TRUE : VK_FALSE,
                                        enabled(Feature::Caustics, tier) ? VK_TRUE : VK_FALSE,
                                        enabled(Feature::TileShuffle, tier) ? VK_TRUE : VK_FALSE,
                                        lightTerms ? VK_TRUE : VK_FALSE};
    for (const Blending blending : BLENDINGS) {
        for (int twoSided = 0; twoSided < 2; ++twoSided) {
            UTA_TRY(p->scene_[std::to_underlying(blending)][twoSided],
                    scenePipeline(making, p->sceneLayout_, formats, sceneVertex.handle, sceneFragment.handle,
                                  {.blending = blending, .twoSided = twoSided == 1, .depthOnly = false},
                                  sceneConstants));
        }
    }
    for (int twoSided = 0; twoSided < 2; ++twoSided) {
        UTA_TRY(p->depth_[twoSided],
                scenePipeline(making, p->sceneLayout_, formats, sceneVertex.handle, sceneFragment.handle,
                              {.blending = Blending::Opaque, .twoSided = twoSided == 1, .depthOnly = true},
                              sceneConstants));
    }
    // UTA-0263 SS 4.4.
    Module flameVertex{device}, flameFragment{device};
    UTA_TRY(flameVertex.handle, shaderModule(device, flame_vert_spv));
    UTA_TRY(flameFragment.handle, shaderModule(device, flame_frag_spv));
    UTA_TRY(p->flame_, flamePipeline(making, p->sceneLayout_, formats, flameVertex.handle, flameFragment.handle));
    // UTA-0215: the water's specks are drawn as the flames are.
    Module moteVertex{device}, moteFragment{device};
    UTA_TRY(moteVertex.handle, shaderModule(device, mote_vert_spv));
    UTA_TRY(moteFragment.handle, shaderModule(device, mote_frag_spv));
    UTA_TRY(p->mote_, flamePipeline(making, p->sceneLayout_, formats, moteVertex.handle, moteFragment.handle));
    UTA_TRY(p->post_, postPipeline(making, p->postLayout_, formats.output, postVertex.handle, postFragment.handle));
    // UTA-0154: FSR 1's stages. The upscale input and EASU's output are
    // HDR-format images; RCAS writes the output.
    Module easuFragment{device}, rcasFragment{device};
    UTA_TRY(easuFragment.handle, shaderModule(device, fsr_easu_frag_spv));
    UTA_TRY(rcasFragment.handle, shaderModule(device, fsr_rcas_frag_spv));
    UTA_TRY(p->upscaleInput_,
            postPipeline(making, p->postLayout_, formats.hdr, postVertex.handle, postFragment.handle));
    UTA_TRY(p->easu_, postPipeline(making, p->postLayout_, formats.hdr, postVertex.handle, easuFragment.handle));
    UTA_TRY(p->rcas_, postPipeline(making, p->postLayout_, formats.output, postVertex.handle, rcasFragment.handle));

    // -- The bloom chain (UTA-0053) -------------------------------------------
    const VkDescriptorSetLayoutBinding sourceBinding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                                     VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo bloomInfo{};
    bloomInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    bloomInfo.bindingCount = 1;
    bloomInfo.pBindings = &sourceBinding;
    UTA_CHECK(created(vkCreateDescriptorSetLayout(device, &bloomInfo, nullptr, &p->bloomSetLayout_), p->bloomSetLayout_,
                    "vkCreateDescriptorSetLayout (bloom)"));
    const VkPushConstantRange bloomRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(gpu::BloomConstants)};
    VkPipelineLayoutCreateInfo bloomLayoutInfo{};
    bloomLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    bloomLayoutInfo.setLayoutCount = 1;
    bloomLayoutInfo.pSetLayouts = &p->bloomSetLayout_;
    bloomLayoutInfo.pushConstantRangeCount = 1;
    bloomLayoutInfo.pPushConstantRanges = &bloomRange;
    UTA_CHECK(created(vkCreatePipelineLayout(device, &bloomLayoutInfo, nullptr, &p->bloomLayout_), p->bloomLayout_,
                    "vkCreatePipelineLayout (bloom)"));
    Module bloomFragment{device};
    UTA_TRY(bloomFragment.handle, shaderModule(device, bloom_frag_spv));
    UTA_TRY(p->bloomDownsample_,
            postPipeline(making, p->bloomLayout_, formats.emission, postVertex.handle, bloomFragment.handle));
    UTA_TRY(p->bloomUpsample_,
            postPipeline(making, p->bloomLayout_, formats.emission, postVertex.handle, bloomFragment.handle, true));

    // -- The light clusters ---------------------------------------------------
    Module clusterCompute{device};
    UTA_TRY(clusterCompute.handle, shaderModule(device, cluster_comp_spv));
    VkComputePipelineCreateInfo clusterInfo{};
    clusterInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    clusterInfo.flags = making.flags;
    clusterInfo.stage = stage(VK_SHADER_STAGE_COMPUTE_BIT, clusterCompute.handle);
    clusterInfo.layout = p->sceneLayout_;
    UTA_CHECK(created(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &clusterInfo, nullptr, &p->clusters_), p->clusters_,
                    "vkCreateComputePipelines (clusters)"));

    // -- The fog volume (UTA-0015 SS 4.4) --------------------------------------
    // Set 1, gpu::FogBinding's.
    constexpr VkShaderStageFlags COMPUTE = VK_SHADER_STAGE_COMPUTE_BIT;
    const std::array fogBindings = {
        VkDescriptorSetLayoutBinding{gpu::FOG_SCATTERING, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, COMPUTE, nullptr},
        VkDescriptorSetLayoutBinding{gpu::FOG_INTEGRATED, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, COMPUTE, nullptr},
        VkDescriptorSetLayoutBinding{gpu::FOG_VOLUME_LIGHTS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, COMPUTE, nullptr},
        VkDescriptorSetLayoutBinding{gpu::FOG_HAZE_SHADOW, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, COMPUTE, nullptr},
        VkDescriptorSetLayoutBinding{gpu::FOG_HISTORY, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, COMPUTE, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo fogInfo{};
    fogInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    fogInfo.bindingCount = static_cast<std::uint32_t>(fogBindings.size());
    fogInfo.pBindings = fogBindings.data();
    UTA_CHECK(created(vkCreateDescriptorSetLayout(device, &fogInfo, nullptr, &p->fogSetLayout_), p->fogSetLayout_,
                    "vkCreateDescriptorSetLayout (fog)"));
    const std::array<VkDescriptorSetLayout, 2> fogSets{p->sceneSetLayout_, p->fogSetLayout_};
    const VkPushConstantRange fogRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(gpu::FogConstants)};
    VkPipelineLayoutCreateInfo fogLayoutInfo{};
    fogLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    fogLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(fogSets.size());
    fogLayoutInfo.pSetLayouts = fogSets.data();
    fogLayoutInfo.pushConstantRangeCount = 1;
    fogLayoutInfo.pPushConstantRanges = &fogRange;
    UTA_CHECK(created(vkCreatePipelineLayout(device, &fogLayoutInfo, nullptr, &p->fogLayout_), p->fogLayout_,
                    "vkCreatePipelineLayout (fog)"));
    Module scatterCompute{device}, integrateCompute{device}, reduceCompute{device};
    UTA_TRY(scatterCompute.handle, shaderModule(device, fog_scatter_comp_spv));
    UTA_TRY(integrateCompute.handle, shaderModule(device, fog_integrate_comp_spv));
    UTA_TRY(reduceCompute.handle, shaderModule(device, shadow_reduce_comp_spv));
    VkComputePipelineCreateInfo compute{};
    compute.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    compute.flags = making.flags;
    compute.layout = p->fogLayout_;
    compute.stage = stage(VK_SHADER_STAGE_COMPUTE_BIT, scatterCompute.handle);
    UTA_CHECK(created(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &compute, nullptr, &p->fogScatter_), p->fogScatter_,
                    "vkCreateComputePipelines (fog scatter)"));
    compute.stage = stage(VK_SHADER_STAGE_COMPUTE_BIT, integrateCompute.handle);
    UTA_CHECK(created(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &compute, nullptr, &p->fogIntegrate_), p->fogIntegrate_,
                    "vkCreateComputePipelines (fog integrate)"));
    compute.stage = stage(VK_SHADER_STAGE_COMPUTE_BIT, reduceCompute.handle);
    UTA_CHECK(created(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &compute, nullptr, &p->hazeShadow_), p->hazeShadow_,
                    "vkCreateComputePipelines (haze shadow)"));

    // -- The shadow tiles (SS 4.8) ---------------------------------------------
    // The scene set, with the tile pass's own push constants.
    const VkPushConstantRange shadowRange{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                          sizeof(gpu::ShadowConstants)};
    VkPipelineLayoutCreateInfo shadowLayoutInfo{};
    shadowLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    shadowLayoutInfo.setLayoutCount = 1;
    shadowLayoutInfo.pSetLayouts = &p->sceneSetLayout_;
    shadowLayoutInfo.pushConstantRangeCount = 1;
    shadowLayoutInfo.pPushConstantRanges = &shadowRange;
    UTA_CHECK(created(vkCreatePipelineLayout(device, &shadowLayoutInfo, nullptr, &p->shadowLayout_), p->shadowLayout_,
                    "vkCreatePipelineLayout (shadow)"));
    Module shadowVertex{device}, shadowFragment{device};
    UTA_TRY(shadowVertex.handle, shaderModule(device, shadow_vert_spv));
    UTA_TRY(shadowFragment.handle, shaderModule(device, shadow_frag_spv));
    UTA_TRY(p->shadow_, shadowPipeline(making, p->shadowLayout_, formats.depth, shadowVertex.handle,
                                       shadowFragment.handle));
    return p;
}

Pipelines::~Pipelines() {
    for (const auto& row : scene_)
        for (VkPipeline pipeline : row)
            if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline, nullptr);
    for (VkPipeline pipeline : {depth_[0], depth_[1], flame_, mote_, post_, upscaleInput_, easu_, rcas_, bloomDownsample_, bloomUpsample_})
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline, nullptr);
    if (bloomLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, bloomLayout_, nullptr);
    if (bloomSetLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, bloomSetLayout_, nullptr);
    if (clusters_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, clusters_, nullptr);
    for (VkPipeline pipeline : {fogScatter_, fogIntegrate_, hazeShadow_})
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline, nullptr);
    if (fogLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, fogLayout_, nullptr);
    if (fogSetLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, fogSetLayout_, nullptr);
    if (shadow_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, shadow_, nullptr);
    if (shadowLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, shadowLayout_, nullptr);
    if (sceneLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, sceneLayout_, nullptr);
    if (postLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, postLayout_, nullptr);
    if (sceneSetLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, sceneSetLayout_, nullptr);
    if (postSetLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, postSetLayout_, nullptr);
}

VkPipeline Pipelines::sceneFor(std::uint32_t polyFlags) const noexcept {
    // UTA-0271: PF_Translucent wins over PF_Modulated, as in UT99's renderers.
    const Blending blending = (polyFlags & gpu::PF_TRANSLUCENT) != 0  ? Blending::Translucent
                              : (polyFlags & gpu::PF_MODULATED) != 0 ? Blending::Modulated
                                                                      : Blending::Opaque;
    const bool twoSided = (polyFlags & gpu::PF_TWO_SIDED) != 0;
    return scene_[std::to_underlying(blending)][twoSided ? 1 : 0];
}

VkPipeline Pipelines::depthFor(std::uint32_t polyFlags) const noexcept {
    return depth_[(polyFlags & gpu::PF_TWO_SIDED) != 0 ? 1 : 0];
}

std::vector<std::pair<std::string, VkPipeline>> Pipelines::named() const {
    std::vector<std::pair<std::string, VkPipeline>> out;
    constexpr std::array<std::string_view, 3> BLENDING_NAMES{"opaque", "translucent", "modulated"};
    for (std::size_t blending = 0; blending < scene_.size(); ++blending)
        for (std::size_t twoSided = 0; twoSided < 2; ++twoSided)
            out.emplace_back(std::format("scene {}{}", BLENDING_NAMES[blending], twoSided == 1 ? " two-sided" : ""),
                             scene_[blending][twoSided]);
    out.emplace_back("depth", depth_[0]);
    out.emplace_back("depth two-sided", depth_[1]);
    for (const auto& [name, pipeline] :
         std::initializer_list<std::pair<std::string_view, VkPipeline>>{
             {"flame", flame_}, {"mote", mote_}, {"post", post_}, {"upscale input", upscaleInput_}, {"easu", easu_},
             {"rcas", rcas_}, {"bloom downsample", bloomDownsample_}, {"bloom upsample", bloomUpsample_},
             {"clusters", clusters_}, {"fog scatter", fogScatter_}, {"fog integrate", fogIntegrate_},
             {"haze shadow", hazeShadow_}, {"shadow", shadow_}})
        out.emplace_back(std::string(name), pipeline);
    return out;
}

std::vector<ShaderStatistics> Pipelines::statistics() const {
    if (!captured_) return {};
    // Extension commands: the loader exports only the core ones.
    const auto executables = reinterpret_cast<PFN_vkGetPipelineExecutablePropertiesKHR>(
        vkGetDeviceProcAddr(device_, "vkGetPipelineExecutablePropertiesKHR"));
    const auto statisticsOf = reinterpret_cast<PFN_vkGetPipelineExecutableStatisticsKHR>(
        vkGetDeviceProcAddr(device_, "vkGetPipelineExecutableStatisticsKHR"));
    if (executables == nullptr || statisticsOf == nullptr) return {};
    std::vector<ShaderStatistics> out;
    for (const auto& [name, pipeline] : named()) {
        VkPipelineInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR;
        info.pipeline = pipeline;
        std::uint32_t count = 0;
        // A query that fails is reported as such rather than dropped, so a
        // missing pipeline is seen.
        if (executables(device_, &info, &count, nullptr) != VK_SUCCESS) {
            out.push_back({name, "(not reported)", {}});
            continue;
        }
        std::vector<VkPipelineExecutablePropertiesKHR> stages(count);
        for (VkPipelineExecutablePropertiesKHR& stage : stages)
            stage.sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR;
        if (executables(device_, &info, &count, stages.data()) != VK_SUCCESS) {
            out.push_back({name, "(not reported)", {}});
            continue;
        }
        for (std::uint32_t index = 0; index < count; ++index) {
            ShaderStatistics stats{name, stages[index].name, {}};
            VkPipelineExecutableInfoKHR executable{};
            executable.sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR;
            executable.pipeline = pipeline;
            executable.executableIndex = index;
            std::uint32_t valueCount = 0;
            if (statisticsOf(device_, &executable, &valueCount, nullptr) == VK_SUCCESS) {
                std::vector<VkPipelineExecutableStatisticKHR> values(valueCount);
                for (VkPipelineExecutableStatisticKHR& value : values)
                    value.sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR;
                if (statisticsOf(device_, &executable, &valueCount, values.data()) == VK_SUCCESS) {
                    for (const VkPipelineExecutableStatisticKHR& value : values) {
                        std::string number;
                        switch (value.format) {
                        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:
                            number = value.value.b32 != VK_FALSE ? "1" : "0";
                            break;
                        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:
                            number = std::format("{}", value.value.i64);
                            break;
                        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:
                            number = std::format("{}", value.value.u64);
                            break;
                        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR:
                            // A value JSON cannot hold is left out with the
                            // formats this does not know.
                            if (!std::isfinite(value.value.f64)) continue;
                            number = std::format("{}", value.value.f64);
                            break;
                        default: continue; // a format this does not know is left out
                        }
                        stats.values.emplace_back(value.name, std::move(number));
                    }
                }
            }
            out.push_back(std::move(stats));
        }
    }
    return out;
}

} // namespace uta::urender
