// The swapchain over the caller's surface -- docs/specs/UTA-0014-vulkan-draw-path.md
// SS 4.3's presenting path and SS 4.10's output format.
//
// INTERNAL (includes Vulkan).
//
// NO CI LEG GRADES THIS (SS 4.12's third tier): there is no display there. It is
// run by hand, and under UTA-0016.
//
// ACQUIRE BEFORE THE FRAME IS PLANNED. A frame skipped because the surface is
// out of date must leave nothing believing it was drawn -- SS 4.8's cached
// shadow tiles above all.

#pragma once

#include "core/Error.h"
#include "urender/Device.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace uta::urender {

class Image;

class Swapchain {
public:
    /// A swapchain over `gpu`'s surface. SS 4.3: its extent is the surface's
    /// where the surface defines one, else `width` x `height`, clamped to the
    /// surface's range. A zero extent -- a minimised window -- gives a swapchain
    /// with no images, which presents nothing. `old`, when given, is retired.
    [[nodiscard]] static Result<std::unique_ptr<Swapchain>> create(const Gpu& gpu, std::uint32_t width,
                                                                   std::uint32_t height,
                                                                   VkSwapchainKHR old = VK_NULL_HANDLE);

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    ~Swapchain();

    [[nodiscard]] VkSwapchainKHR handle() const noexcept { return handle_; }
    [[nodiscard]] VkExtent2D extent() const noexcept { return extent_; }
    [[nodiscard]] bool empty() const noexcept { return handle_ == VK_NULL_HANDLE; }
    /// Set once acquire or present has said this no longer matches its surface.
    [[nodiscard]] bool stale() const noexcept { return stale_; }

    /// The image the next frame goes to, or none when the surface is out of
    /// date -- the frame is then skipped and the swapchain rebuilt first.
    [[nodiscard]] Result<std::optional<std::uint32_t>> acquire();

    /// Record copying `output` to image `index` and readying it to present.
    void recordBlit(VkCommandBuffer commands, Image& output, std::uint32_t index);

    /// Present image `index`, whose commands have finished.
    [[nodiscard]] Result<void> present(VkQueue queue, std::uint32_t index);

private:
    Swapchain() = default;

    VkDevice device_ = VK_NULL_HANDLE;
    VkSwapchainKHR handle_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    bool stale_ = false;
};

} // namespace uta::urender
