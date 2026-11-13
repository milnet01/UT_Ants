// The descriptor layouts and the pipelines the frame draws with.
//
// INTERNAL (includes Vulkan).
//
// PIPELINE IDENTITY IS A BATCH'S polyFlags, NEVER ITS MATERIAL -- SS 4.5. The
// material is a bindless index pushed as a constant, so two batches differing
// only in material share a pipeline; two sharing a material and differing in
// flags may not, because the flags select cull mode, blending and depth write.
// UTA-0273's refracting water is the one exception: its pipeline is chosen by
// its material too, through refractFor.
// Masking, unlit and sky are tested bit by bit in the shaders instead, so six
// scene pipelines -- three blends, each one- or two-sided -- and two depth
// pipelines (UTA-0260) cover every flag word.

#pragma once

#include "core/Error.h"
#include "urender/Device.h"
#include "urender/Renderer.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace uta::urender {

struct TargetFormats {
    VkFormat hdr = VK_FORMAT_UNDEFINED;
    VkFormat velocity = VK_FORMAT_UNDEFINED;
    VkFormat depth = VK_FORMAT_UNDEFINED;
    VkFormat output = VK_FORMAT_UNDEFINED;
    VkFormat emission = VK_FORMAT_UNDEFINED; ///< UTA-0053: the forward pass's third target and the bloom chain
};

/// A shader module over embedded SPIR-V words.
[[nodiscard]] Result<VkShaderModule> shaderModule(VkDevice device, std::span<const std::uint32_t> words);

class Pipelines {
public:
    /// `tier` sets the scene shader's parallax step counts (UTA-0040 SS 4.4).
    /// `lightTerms` is Config::lightTerms (UTA-0292); `specular`,
    /// Config::specular (UTA-0298).
    [[nodiscard]] static Result<std::unique_ptr<Pipelines>> create(const Gpu& gpu, const TargetFormats& formats,
                                                                    Tier tier, bool lightTerms = false,
                                                                    bool specular = true);

    Pipelines(const Pipelines&) = delete;
    Pipelines& operator=(const Pipelines&) = delete;
    ~Pipelines();

    [[nodiscard]] VkDescriptorSetLayout sceneSetLayout() const noexcept { return sceneSetLayout_; }
    [[nodiscard]] VkDescriptorSetLayout postSetLayout() const noexcept { return postSetLayout_; }
    /// UTA-0273 SS 4.3: set 1 of sceneLayout, the scene copy then the depth.
    [[nodiscard]] VkDescriptorSetLayout refractSetLayout() const noexcept { return refractSetLayout_; }
    [[nodiscard]] VkPipelineLayout sceneLayout() const noexcept { return sceneLayout_; }
    [[nodiscard]] VkPipelineLayout postLayout() const noexcept { return postLayout_; }

    /// The forward pipeline a batch carrying `polyFlags` is drawn with.
    /// UTA-0451: `glossy` (MaterialSet::isGlossy) picks the one with
    /// highlights, where they are on; the rest draw without and pay nothing.
    [[nodiscard]] VkPipeline sceneFor(std::uint32_t polyFlags, bool glossy) const noexcept;
    /// UTA-0273 SS 4.4: the pipeline a refracting batch carrying `polyFlags`
    /// is drawn with, reading set 1; null below Ultra, where none refracts.
    [[nodiscard]] VkPipeline refractFor(std::uint32_t polyFlags) const noexcept;
    /// UTA-0260: the pipeline that draws such a batch's depth alone, ahead of
    /// the forward pass. The scene's own vertex stage and no fragment stage.
    [[nodiscard]] VkPipeline depthFor(std::uint32_t polyFlags) const noexcept;
    /// UTA-0263 SS 4.4: the camera-facing flames, instanced over FLAMES.
    [[nodiscard]] VkPipeline flame() const noexcept { return flame_; }
    /// UTA-0215: the specks in the water about the eye, drawn as the flames are.
    [[nodiscard]] VkPipeline mote() const noexcept { return mote_; }
    /// UTA-0293 SS 4.4: the blades of grass, instanced over GRASS. Made only
    /// at a tier that draws grass; VK_NULL_HANDLE below it.
    [[nodiscard]] VkPipeline grass() const noexcept { return grass_; }
    /// UTA-0019 SS 4.8's stand-in shapes, over shapeLayout: instance-rate
    /// ShapeInstance records, ShapeConstants pushed, colour and depth only.
    /// `blended` tests depth without writing it, for a shape with alpha below 1.
    [[nodiscard]] VkPipeline shape(bool blended) const noexcept { return blended ? shapeBlended_ : shapeOpaque_; }
    [[nodiscard]] VkPipelineLayout shapeLayout() const noexcept { return shapeLayout_; }
    [[nodiscard]] VkPipeline post() const noexcept { return post_; }
    /// UTA-0154's FSR 1 stages, over the post layout: post.frag into the
    /// HDR-format upscale input, EASU into another, and RCAS into the output.
    [[nodiscard]] VkPipeline upscaleInput() const noexcept { return upscaleInput_; }
    [[nodiscard]] VkPipeline easu() const noexcept { return easu_; }
    [[nodiscard]] VkPipeline rcas() const noexcept { return rcas_; }
    /// UTA-0053's bloom chain: one sampled source per set, BloomConstants
    /// pushed, a downsample that replaces and an upsample that adds.
    [[nodiscard]] VkDescriptorSetLayout bloomSetLayout() const noexcept { return bloomSetLayout_; }
    [[nodiscard]] VkPipelineLayout bloomLayout() const noexcept { return bloomLayout_; }
    [[nodiscard]] VkPipeline bloomDownsample() const noexcept { return bloomDownsample_; }
    [[nodiscard]] VkPipeline bloomUpsample() const noexcept { return bloomUpsample_; }
    /// SS 4.6's culling pass, over the scene set.
    [[nodiscard]] VkPipeline clusters() const noexcept { return clusters_; }
    /// UTA-0015 SS 4.4's two fog stages: the scene set, then the fog set, with
    /// FogConstants pushed.
    [[nodiscard]] VkDescriptorSetLayout fogSetLayout() const noexcept { return fogSetLayout_; }
    [[nodiscard]] VkPipelineLayout fogLayout() const noexcept { return fogLayout_; }
    [[nodiscard]] VkPipeline fogScatter() const noexcept { return fogScatter_; }
    [[nodiscard]] VkPipeline fogIntegrate() const noexcept { return fogIntegrate_; }
    /// UTA-0333: the haze's copy of one drawn shadow tile, on the fog layout with
    /// the tile pushed in place of FogConstants.
    [[nodiscard]] VkPipeline hazeShadow() const noexcept { return hazeShadow_; }
    /// SS 4.8's tile pass: depth only, over the scene set with its own push constants.
    [[nodiscard]] VkPipeline shadow() const noexcept { return shadow_; }
    [[nodiscard]] VkPipelineLayout shadowLayout() const noexcept { return shadowLayout_; }

    /// How many textures one scene set can bind: the device's limit, capped.
    [[nodiscard]] std::uint32_t textureCapacity() const noexcept { return textureCapacity_; }

    /// UTA-0352: each pipeline's statistics, stage by stage, named as
    /// `named` names it; none where the Gpu captures none.
    [[nodiscard]] std::vector<ShaderStatistics> statistics() const;

private:
    Pipelines() = default;

    // create's steps, in its order. Each fills the members its section names.
    [[nodiscard]] VkPipelineCreateFlags createFlags() const noexcept;
    [[nodiscard]] Result<void> createSceneSet();
    [[nodiscard]] Result<void> createPostSet();
    [[nodiscard]] Result<void> createScenePipelines(const TargetFormats& formats, Tier tier, bool lightTerms,
                                                    bool specular);
    [[nodiscard]] Result<void> createOutputPipelines(const TargetFormats& formats, VkShaderModule postVertex);
    [[nodiscard]] Result<void> createBloomChain(const TargetFormats& formats, VkShaderModule postVertex);
    [[nodiscard]] Result<void> createClusters();
    [[nodiscard]] Result<void> createFog();
    [[nodiscard]] Result<void> createShadow(const TargetFormats& formats);
    [[nodiscard]] Result<void> createShapes(const TargetFormats& formats);

    /// Every pipeline, under a name a reader can find it by.
    [[nodiscard]] std::vector<std::pair<std::string, VkPipeline>> named() const;

    VkDevice device_ = VK_NULL_HANDLE;
    bool captured_ = false; ///< made with VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR
    std::uint32_t textureCapacity_ = 0;
    VkDescriptorSetLayout sceneSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout postSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout refractSetLayout_ = VK_NULL_HANDLE; ///< UTA-0273
    VkPipelineLayout sceneLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout postLayout_ = VK_NULL_HANDLE;
    /// Indexed [Blending][twoSided]: Blending is Pipelines.cpp's, opaque, translucent, modulated.
    /// Drawn without highlights.
    std::array<std::array<VkPipeline, 2>, 3> scene_{};
    /// UTA-0451: the same with highlights, for glossy materials; null where
    /// highlights are off.
    std::array<std::array<VkPipeline, 2>, 3> glossy_{};
    /// UTA-0273: indexed [twoSided]; null below Ultra.
    std::array<VkPipeline, 2> refract_{};
    /// UTA-0260: indexed [twoSided].
    std::array<VkPipeline, 2> depth_{};
    VkPipeline flame_ = VK_NULL_HANDLE;
    VkPipeline mote_ = VK_NULL_HANDLE;
    VkPipeline grass_ = VK_NULL_HANDLE;
    VkPipelineLayout shapeLayout_ = VK_NULL_HANDLE;
    VkPipeline shapeOpaque_ = VK_NULL_HANDLE, shapeBlended_ = VK_NULL_HANDLE;
    VkPipeline post_ = VK_NULL_HANDLE;
    VkPipeline upscaleInput_ = VK_NULL_HANDLE, easu_ = VK_NULL_HANDLE, rcas_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout bloomSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout bloomLayout_ = VK_NULL_HANDLE;
    VkPipeline bloomDownsample_ = VK_NULL_HANDLE, bloomUpsample_ = VK_NULL_HANDLE;
    VkPipeline clusters_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout fogSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout fogLayout_ = VK_NULL_HANDLE;
    VkPipeline fogScatter_ = VK_NULL_HANDLE, fogIntegrate_ = VK_NULL_HANDLE, hazeShadow_ = VK_NULL_HANDLE;
    VkPipelineLayout shadowLayout_ = VK_NULL_HANDLE;
    VkPipeline shadow_ = VK_NULL_HANDLE;
};

} // namespace uta::urender
