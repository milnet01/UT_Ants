// The descriptor layouts and the pipelines -- Pipelines.h.

#include "urender/Pipelines.h"

#include "ubundle/Bundle.h"
#include "urender/ShaderTypes.h"

#include "cluster.comp.spv.h"
#include "post.frag.spv.h"
#include "post.vert.spv.h"
#include "scene.frag.spv.h"
#include "scene.vert.spv.h"

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
                                 VkShaderModule vertex, VkShaderModule fragment, SceneVariant variant) {
    const std::array stages = {stage(VK_SHADER_STAGE_VERTEX_BIT, vertex),
                               stage(VK_SHADER_STAGE_FRAGMENT_BIT, fragment)};

    // ubundle::GeometryVertex, uploaded as it is laid out in memory.
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
    raster.cullMode = variant.twoSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
    // UTA-0109 SS 4.3 winds each triangle so its corners turn about the stored
    // normal; the projection's +Y-down flip makes that clockwise on screen.
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

    // The opaque pass writes colour and velocity; the translucent pass binds
    // the colour attachment alone, which is how it writes no velocity (SS 4.11).
    const std::array opaqueAttachments = {opaque, opaque};
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    if (variant.translucent) {
        blend.attachmentCount = 1;
        blend.pAttachments = &blended;
    } else {
        blend.attachmentCount = 2;
        blend.pAttachments = opaqueAttachments.data();
    }

    const std::array dynamics = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamics.size());
    dynamic.pDynamicStates = dynamics.data();

    const std::array colourFormats = {formats.hdr, formats.velocity};
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = variant.translucent ? 1 : 2;
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

Result<VkPipeline> postPipeline(VkDevice device, VkPipelineLayout layout, VkFormat output, VkShaderModule vertex,
                                VkShaderModule fragment) {
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

Result<std::unique_ptr<Pipelines>> Pipelines::create(const Gpu& gpu, const TargetFormats& formats) {
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
    for (std::uint32_t i = gpu::FRAME; i <= gpu::SHADOW_FACES; ++i)
        bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, everyStage, nullptr};
    bindings[gpu::SHADOW_ATLAS] = {gpu::SHADOW_ATLAS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
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
    const VkDescriptorSetLayoutBinding hdrBinding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                                  VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo postInfo{};
    postInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    postInfo.bindingCount = 1;
    postInfo.pBindings = &hdrBinding;
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
                                  {translucent == 1, twoSided == 1}));
        }
    }
    UTA_TRY(p->post_, postPipeline(device, p->postLayout_, formats.output, postVertex.handle, postFragment.handle));

    Module clusterCompute{device};
    UTA_TRY(clusterCompute.handle, shaderModule(device, cluster_comp_spv));
    VkComputePipelineCreateInfo compute{};
    compute.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    compute.stage = stage(VK_SHADER_STAGE_COMPUTE_BIT, clusterCompute.handle);
    compute.layout = p->sceneLayout_;
    UTA_CHECK(check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &compute, nullptr, &p->clusters_),
                    "vkCreateComputePipelines (clusters)"));
    return p;
}

Pipelines::~Pipelines() {
    for (auto& row : scene_)
        for (VkPipeline pipeline : row)
            if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline, nullptr);
    if (post_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, post_, nullptr);
    if (clusters_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, clusters_, nullptr);
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
