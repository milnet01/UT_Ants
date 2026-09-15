// The descriptor layouts and the pipelines the frame draws with.
//
// INTERNAL (includes Vulkan).
//
// PIPELINE IDENTITY IS A BATCH'S polyFlags, NEVER ITS MATERIAL -- SS 4.5. The
// material is a bindless index pushed as a constant, so two batches differing
// only in material share a pipeline; two sharing a material and differing in
// flags may not, because the flags select cull mode, blending and depth write.
// Masking, unlit and sky are tested bit by bit in the shaders instead, so four
// pipelines cover every flag word.

#pragma once

#include "core/Error.h"
#include "urender/Device.h"
#include "urender/Renderer.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>

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
    [[nodiscard]] static Result<std::unique_ptr<Pipelines>> create(const Gpu& gpu, const TargetFormats& formats,
                                                                    Tier tier);

    Pipelines(const Pipelines&) = delete;
    Pipelines& operator=(const Pipelines&) = delete;
    ~Pipelines();

    [[nodiscard]] VkDescriptorSetLayout sceneSetLayout() const noexcept { return sceneSetLayout_; }
    [[nodiscard]] VkDescriptorSetLayout postSetLayout() const noexcept { return postSetLayout_; }
    [[nodiscard]] VkPipelineLayout sceneLayout() const noexcept { return sceneLayout_; }
    [[nodiscard]] VkPipelineLayout postLayout() const noexcept { return postLayout_; }

    /// The forward pipeline a batch carrying `polyFlags` is drawn with.
    [[nodiscard]] VkPipeline sceneFor(std::uint32_t polyFlags) const noexcept;
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
    /// SS 4.8's tile pass: depth only, over the scene set with its own push constants.
    [[nodiscard]] VkPipeline shadow() const noexcept { return shadow_; }
    [[nodiscard]] VkPipelineLayout shadowLayout() const noexcept { return shadowLayout_; }

    /// How many textures one scene set can bind: the device's limit, capped.
    [[nodiscard]] std::uint32_t textureCapacity() const noexcept { return textureCapacity_; }

private:
    Pipelines() = default;

    VkDevice device_ = VK_NULL_HANDLE;
    std::uint32_t textureCapacity_ = 0;
    VkDescriptorSetLayout sceneSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout postSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout sceneLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout postLayout_ = VK_NULL_HANDLE;
    /// Indexed [translucent][twoSided].
    std::array<std::array<VkPipeline, 2>, 2> scene_{};
    VkPipeline post_ = VK_NULL_HANDLE;
    VkPipeline upscaleInput_ = VK_NULL_HANDLE, easu_ = VK_NULL_HANDLE, rcas_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout bloomSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout bloomLayout_ = VK_NULL_HANDLE;
    VkPipeline bloomDownsample_ = VK_NULL_HANDLE, bloomUpsample_ = VK_NULL_HANDLE;
    VkPipeline clusters_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout fogSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout fogLayout_ = VK_NULL_HANDLE;
    VkPipeline fogScatter_ = VK_NULL_HANDLE, fogIntegrate_ = VK_NULL_HANDLE;
    VkPipelineLayout shadowLayout_ = VK_NULL_HANDLE;
    VkPipeline shadow_ = VK_NULL_HANDLE;
};

} // namespace uta::urender
