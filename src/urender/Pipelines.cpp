// The descriptor layouts and the pipelines -- Pipelines.h.

#include "urender/Pipelines.h"

#include "ubundle/Bundle.h"
#include "urender/ShaderTypes.h"
#include "urender/Tiers.h"

#include "bloom.frag.spv.h"
#include "cluster.comp.spv.h"
#include "fog_integrate.comp.spv.h"
#include "fog_scatter.comp.spv.h"
#include "fsr_easu.frag.spv.h"
#include "fsr_rcas.frag.spv.h"
#include "post.frag.spv.h"
#include "post.vert.spv.h"
#include "scene.frag.spv.h"
#include "scene.vert.spv.h"
#include "shadow.frag.spv.h"
#include "shadow.vert.spv.h"

#include <algorithm>
#include <cstddef>
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

struct SceneVariant {
    bool translucent;
    bool twoSided;
};

Result<VkPipeline> scenePipeline(VkDevice device, VkPipelineLayout layout, const TargetFormats& formats,
                                 VkShaderModule vertex, VkShaderModule fragment, SceneVariant variant,
                                 ParallaxSteps parallax) {
    // UTA-0040 SS 4.4: scene.frag's constant_id 0 and 1, so a tier with no
    // steps compiles the march out.
    const std::array entries = {
        VkSpecializationMapEntry{0, offsetof(ParallaxSteps, minimum), sizeof(ParallaxSteps::minimum)},
        VkSpecializationMapEntry{1, offsetof(ParallaxSteps, maximum), sizeof(ParallaxSteps::maximum)},
    };
    const VkSpecializationInfo specialization{static_cast<std::uint32_t>(entries.size()), entries.data(),
                                              sizeof(parallax), &parallax};
    std::array stages = {stage(VK_SHADER_STAGE_VERTEX_BIT, vertex), stage(VK_SHADER_STAGE_FRAGMENT_BIT, fragment)};
    stages[1].pSpecializationInfo = &specialization;

    // ubundle::GeometryVertex, uploaded as it is laid out in memory; then
    // UTA-0164 SS 4.5's occlusion uvs, a stream of their own.
    const std::array bindings = {
        VkVertexInputBindingDescription{0, sizeof(ubundle::GeometryVertex), VK_VERTEX_INPUT_RATE_VERTEX},
        VkVertexInputBindingDescription{1, sizeof(std::array<float, 2>), VK_VERTEX_INPUT_RATE_VERTEX},
    };
    const std::array attributes = {
        VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                          offsetof(ubundle::GeometryVertex, position)},
        VkVertexInputAttributeDescription{1, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                          offsetof(ubundle::GeometryVertex, normal)},
        VkVertexInputAttributeDescription{2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ubundle::GeometryVertex, u)},
        // UTA-0156 SS 4.4: the vertex's zone, for its ambient light.
        VkVertexInputAttributeDescription{3, 0, VK_FORMAT_R8_UINT, offsetof(ubundle::GeometryVertex, zone)},
        VkVertexInputAttributeDescription{4, 1, VK_FORMAT_R32G32_SFLOAT, 0},
    };
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = variant.twoSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
    // UTA-0109 SS 4.3 winds each triangle so its corners turn about the stored
    // normal; the projection's +Y-down flip makes that clockwise on screen. It is
    // dynamic state, set per draw: a mirrored mover reverses it (Frame.cpp).
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    // SS 4.5: translucent batches are depth-tested and not depth-written.
    depth.depthWriteEnable = variant.translucent ? VK_FALSE : VK_TRUE;
    // Or-equal, so the sky's depth at exactly the far plane draws where
    // nothing nearer did.
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

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
    blended.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blended.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blended.alphaBlendOp = VK_BLEND_OP_ADD;

    // The opaque pass writes colour, velocity and UTA-0053's emission; the
    // translucent pass binds the colour attachment alone, which is how it writes
    // no velocity (SS 4.11).
    const std::array opaqueAttachments = {opaque, opaque, opaque};
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    if (variant.translucent) {
        blend.attachmentCount = 1;
        blend.pAttachments = &blended;
    } else {
        blend.attachmentCount = 3;
        blend.pAttachments = opaqueAttachments.data();
    }

    // Front face is core dynamic state from Vulkan 1.3 (VK_EXT_extended_dynamic_state
    // was promoted), so it needs no feature beyond SS 4.4's.
    const std::array dynamics = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_FRONT_FACE};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamics.size());
    dynamic.pDynamicStates = dynamics.data();

    // UTA-0053: the opaque pass also writes emission, for the bloom chain.
    const std::array colourFormats = {formats.hdr, formats.velocity, formats.emission};
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = variant.translucent ? 1 : 3;
    rendering.pColorAttachmentFormats = colourFormats.data();
    rendering.depthAttachmentFormat = formats.depth;

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering; // dynamic rendering: no VkRenderPass
    info.stageCount = static_cast<std::uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    UTA_CHECK(check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
                    "vkCreateGraphicsPipelines (scene)"));
    return pipeline;
}

/// SS 4.8's tile pass. Both faces cast -- a shadow must not leak through a
/// wall seen edge-on from its back -- and a slope-scaled rasterisation depth
/// bias is what keeps a lit surface from shadowing itself. shadows.glsl applies
/// no normal offset.
Result<VkPipeline> shadowPipeline(VkDevice device, VkPipelineLayout layout, VkFormat depthFormat,
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
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.depthBiasEnable = VK_TRUE;
    raster.depthBiasConstantFactor = 1.25f;
    raster.depthBiasSlopeFactor = 1.75f;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    const std::array dynamics = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamics.size());
    dynamic.pDynamicStates = dynamics.data();
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.depthAttachmentFormat = depthFormat;

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = static_cast<std::uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    UTA_CHECK(check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
                    "vkCreateGraphicsPipelines (shadow)"));
    return pipeline;
}

/// A full-target pass. `additive` adds into what the target holds -- UTA-0053's
/// bloom upsample -- where every other use replaces it.
Result<VkPipeline> postPipeline(VkDevice device, VkPipelineLayout layout, VkFormat output, VkShaderModule vertex,
                                VkShaderModule fragment, bool additive = false) {
    const std::array stages = {stage(VK_SHADER_STAGE_VERTEX_BIT, vertex),
                               stage(VK_SHADER_STAGE_FRAGMENT_BIT, fragment)};
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                                | VK_COLOR_COMPONENT_A_BIT;
    if (additive) {
        attachment.blendEnable = VK_TRUE;
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.colorBlendOp = VK_BLEND_OP_ADD;
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    const std::array dynamics = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamics.size());
    dynamic.pDynamicStates = dynamics.data();
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &output;

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = static_cast<std::uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    UTA_CHECK(check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
                    "vkCreateGraphicsPipelines (post)"));
    return pipeline;
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

Result<std::unique_ptr<Pipelines>> Pipelines::create(const Gpu& gpu, const TargetFormats& formats, Tier tier) {
    std::unique_ptr<Pipelines> p(new Pipelines());
    p->device_ = gpu.device();
    const VkDevice device = p->device_;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(gpu.physical(), &properties);
    const VkPhysicalDeviceLimits& limits = properties.limits;
    // A combined image sampler counts against both the sampled-image and the
    // sampler limits; one of each is kept back for the shadow atlas.
    p->textureCapacity_ = std::min({limits.maxPerStageDescriptorSampledImages, limits.maxPerStageDescriptorSamplers,
                                    limits.maxDescriptorSetSampledImages, limits.maxDescriptorSetSamplers,
                                    TEXTURE_CEILING + 1})
                          - 1;

    // -- The scene set ------------------------------------------------------
    const VkShaderStageFlags everyStage =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    std::array<VkDescriptorSetLayoutBinding, gpu::TEXTURES + 1> bindings{};
    std::array<VkDescriptorBindingFlags, gpu::TEXTURES + 1> bindingFlags{};
    for (std::uint32_t i = gpu::FRAME; i <= gpu::ZONES; ++i)
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
    UTA_CHECK(check(vkCreateDescriptorSetLayout(device, &sceneInfo, nullptr, &p->sceneSetLayout_),
                    "vkCreateDescriptorSetLayout (scene)"));

    const VkPushConstantRange drawRange{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                        sizeof(gpu::DrawConstants)};
    VkPipelineLayoutCreateInfo sceneLayoutInfo{};
    sceneLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    sceneLayoutInfo.setLayoutCount = 1;
    sceneLayoutInfo.pSetLayouts = &p->sceneSetLayout_;
    sceneLayoutInfo.pushConstantRangeCount = 1;
    sceneLayoutInfo.pPushConstantRanges = &drawRange;
    UTA_CHECK(check(vkCreatePipelineLayout(device, &sceneLayoutInfo, nullptr, &p->sceneLayout_),
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
    UTA_CHECK(check(vkCreateDescriptorSetLayout(device, &postInfo, nullptr, &p->postSetLayout_),
                    "vkCreateDescriptorSetLayout (post)"));
    const VkPushConstantRange postRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(gpu::PostConstants)};
    VkPipelineLayoutCreateInfo postLayoutInfo{};
    postLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    postLayoutInfo.setLayoutCount = 1;
    postLayoutInfo.pSetLayouts = &p->postSetLayout_;
    postLayoutInfo.pushConstantRangeCount = 1;
    postLayoutInfo.pPushConstantRanges = &postRange;
    UTA_CHECK(check(vkCreatePipelineLayout(device, &postLayoutInfo, nullptr, &p->postLayout_),
                    "vkCreatePipelineLayout (post)"));

    // -- The pipelines ------------------------------------------------------
    Module sceneVertex{device}, sceneFragment{device}, postVertex{device}, postFragment{device};
    UTA_TRY(sceneVertex.handle, shaderModule(device, scene_vert_spv));
    UTA_TRY(sceneFragment.handle, shaderModule(device, scene_frag_spv));
    UTA_TRY(postVertex.handle, shaderModule(device, post_vert_spv));
    UTA_TRY(postFragment.handle, shaderModule(device, post_frag_spv));

    for (int translucent = 0; translucent < 2; ++translucent) {
        for (int twoSided = 0; twoSided < 2; ++twoSided) {
            UTA_TRY(p->scene_[translucent][twoSided],
                    scenePipeline(device, p->sceneLayout_, formats, sceneVertex.handle, sceneFragment.handle,
                                  {translucent == 1, twoSided == 1}, parallaxStepsOf(tier)));
        }
    }
    UTA_TRY(p->post_, postPipeline(device, p->postLayout_, formats.output, postVertex.handle, postFragment.handle));
    // UTA-0154: FSR 1's stages. The upscale input and EASU's output are
    // HDR-format images; RCAS writes the output.
    Module easuFragment{device}, rcasFragment{device};
    UTA_TRY(easuFragment.handle, shaderModule(device, fsr_easu_frag_spv));
    UTA_TRY(rcasFragment.handle, shaderModule(device, fsr_rcas_frag_spv));
    UTA_TRY(p->upscaleInput_,
            postPipeline(device, p->postLayout_, formats.hdr, postVertex.handle, postFragment.handle));
    UTA_TRY(p->easu_, postPipeline(device, p->postLayout_, formats.hdr, postVertex.handle, easuFragment.handle));
    UTA_TRY(p->rcas_, postPipeline(device, p->postLayout_, formats.output, postVertex.handle, rcasFragment.handle));

    // -- The bloom chain (UTA-0053) -------------------------------------------
    const VkDescriptorSetLayoutBinding sourceBinding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                                     VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo bloomInfo{};
    bloomInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    bloomInfo.bindingCount = 1;
    bloomInfo.pBindings = &sourceBinding;
    UTA_CHECK(check(vkCreateDescriptorSetLayout(device, &bloomInfo, nullptr, &p->bloomSetLayout_),
                    "vkCreateDescriptorSetLayout (bloom)"));
    const VkPushConstantRange bloomRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(gpu::BloomConstants)};
    VkPipelineLayoutCreateInfo bloomLayoutInfo{};
    bloomLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    bloomLayoutInfo.setLayoutCount = 1;
    bloomLayoutInfo.pSetLayouts = &p->bloomSetLayout_;
    bloomLayoutInfo.pushConstantRangeCount = 1;
    bloomLayoutInfo.pPushConstantRanges = &bloomRange;
    UTA_CHECK(check(vkCreatePipelineLayout(device, &bloomLayoutInfo, nullptr, &p->bloomLayout_),
                    "vkCreatePipelineLayout (bloom)"));
    Module bloomFragment{device};
    UTA_TRY(bloomFragment.handle, shaderModule(device, bloom_frag_spv));
    UTA_TRY(p->bloomDownsample_,
            postPipeline(device, p->bloomLayout_, formats.emission, postVertex.handle, bloomFragment.handle));
    UTA_TRY(p->bloomUpsample_,
            postPipeline(device, p->bloomLayout_, formats.emission, postVertex.handle, bloomFragment.handle, true));

    Module clusterCompute{device};
    UTA_TRY(clusterCompute.handle, shaderModule(device, cluster_comp_spv));
    VkComputePipelineCreateInfo compute{};
    compute.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    compute.stage = stage(VK_SHADER_STAGE_COMPUTE_BIT, clusterCompute.handle);
    compute.layout = p->sceneLayout_;
    UTA_CHECK(check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &compute, nullptr, &p->clusters_),
                    "vkCreateComputePipelines (clusters)"));

    // -- The fog volume (UTA-0015 SS 4.4) --------------------------------------
    // Set 1: the scattering image, the integrated image, and VOLUME_LIGHTS.
    const std::array fogBindings = {
        VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        VkDescriptorSetLayoutBinding{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo fogInfo{};
    fogInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    fogInfo.bindingCount = static_cast<std::uint32_t>(fogBindings.size());
    fogInfo.pBindings = fogBindings.data();
    UTA_CHECK(check(vkCreateDescriptorSetLayout(device, &fogInfo, nullptr, &p->fogSetLayout_),
                    "vkCreateDescriptorSetLayout (fog)"));
    const std::array<VkDescriptorSetLayout, 2> fogSets{p->sceneSetLayout_, p->fogSetLayout_};
    const VkPushConstantRange fogRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(gpu::FogConstants)};
    VkPipelineLayoutCreateInfo fogLayoutInfo{};
    fogLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    fogLayoutInfo.setLayoutCount = static_cast<std::uint32_t>(fogSets.size());
    fogLayoutInfo.pSetLayouts = fogSets.data();
    fogLayoutInfo.pushConstantRangeCount = 1;
    fogLayoutInfo.pPushConstantRanges = &fogRange;
    UTA_CHECK(check(vkCreatePipelineLayout(device, &fogLayoutInfo, nullptr, &p->fogLayout_),
                    "vkCreatePipelineLayout (fog)"));
    Module scatterCompute{device}, integrateCompute{device};
    UTA_TRY(scatterCompute.handle, shaderModule(device, fog_scatter_comp_spv));
    UTA_TRY(integrateCompute.handle, shaderModule(device, fog_integrate_comp_spv));
    compute.layout = p->fogLayout_;
    compute.stage = stage(VK_SHADER_STAGE_COMPUTE_BIT, scatterCompute.handle);
    UTA_CHECK(check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &compute, nullptr, &p->fogScatter_),
                    "vkCreateComputePipelines (fog scatter)"));
    compute.stage = stage(VK_SHADER_STAGE_COMPUTE_BIT, integrateCompute.handle);
    UTA_CHECK(check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &compute, nullptr, &p->fogIntegrate_),
                    "vkCreateComputePipelines (fog integrate)"));

    const VkPushConstantRange shadowRange{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                          sizeof(gpu::ShadowConstants)};
    VkPipelineLayoutCreateInfo shadowLayoutInfo = sceneLayoutInfo;
    shadowLayoutInfo.pPushConstantRanges = &shadowRange;
    UTA_CHECK(check(vkCreatePipelineLayout(device, &shadowLayoutInfo, nullptr, &p->shadowLayout_),
                    "vkCreatePipelineLayout (shadow)"));
    Module shadowVertex{device}, shadowFragment{device};
    UTA_TRY(shadowVertex.handle, shaderModule(device, shadow_vert_spv));
    UTA_TRY(shadowFragment.handle, shaderModule(device, shadow_frag_spv));
    UTA_TRY(p->shadow_, shadowPipeline(device, p->shadowLayout_, formats.depth, shadowVertex.handle,
                                       shadowFragment.handle));
    return p;
}

Pipelines::~Pipelines() {
    for (auto& row : scene_)
        for (VkPipeline pipeline : row)
            if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline, nullptr);
    for (VkPipeline pipeline : {post_, upscaleInput_, easu_, rcas_, bloomDownsample_, bloomUpsample_})
        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline, nullptr);
    if (bloomLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, bloomLayout_, nullptr);
    if (bloomSetLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, bloomSetLayout_, nullptr);
    if (clusters_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, clusters_, nullptr);
    for (VkPipeline pipeline : {fogScatter_, fogIntegrate_})
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
    const bool translucent = (polyFlags & gpu::PF_TRANSLUCENT) != 0;
    const bool twoSided = (polyFlags & gpu::PF_TWO_SIDED) != 0;
    return scene_[translucent ? 1 : 0][twoSided ? 1 : 0];
}

} // namespace uta::urender
