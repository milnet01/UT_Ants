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
};

/// A shader module over embedded SPIR-V words.
[[nodiscard]] Result<VkShaderModule> shaderModule(VkDevice device, std::span<const std::uint32_t> words);

class Pipelines {
public:
    [[nodiscard]] static Result<std::unique_ptr<Pipelines>> create(const Gpu& gpu, const TargetFormats& formats);

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
    /// SS 4.6's culling pass, over the scene set.
    [[nodiscard]] VkPipeline clusters() const noexcept { return clusters_; }

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
    VkPipeline clusters_ = VK_NULL_HANDLE;
};

} // namespace uta::urender
