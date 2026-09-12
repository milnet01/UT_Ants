// Creating the presenting path's swapchain -- docs/specs/UTA-0014-vulkan-draw-path.md
// SS 4.3 and SS 4.10. Swapchain.h says what grades it.

#include "urender/Swapchain.h"

#include <algorithm>
#include <limits>

namespace uta::urender {

Result<std::unique_ptr<Swapchain>> Swapchain::create(const Gpu& gpu, std::uint32_t width, std::uint32_t height,
                                                     VkSwapchainKHR old) {
    std::unique_ptr<Swapchain> swapchain(new Swapchain());
    swapchain->device_ = gpu.device();

    VkSurfaceCapabilitiesKHR caps{};
    UTA_CHECK(check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu.physical(), gpu.surface(), &caps),
                    "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));

    // SS 4.3: the surface's extent where it defines one. 0xFFFFFFFF leaves it to
    // the swapchain, and then the caller's size decides.
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == std::numeric_limits<std::uint32_t>::max()) {
        extent.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    swapchain->extent_ = extent;
    if (extent.width == 0 || extent.height == 0) return swapchain; // a minimised window

    std::uint32_t formatCount = 0;
    UTA_CHECK(check(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu.physical(), gpu.surface(), &formatCount, nullptr),
                    "vkGetPhysicalDeviceSurfaceFormatsKHR"));
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    UTA_CHECK(check(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu.physical(), gpu.surface(), &formatCount, formats.data()),
                    "vkGetPhysicalDeviceSurfaceFormatsKHR"));
    // SS 4.10: an _SRGB format, so the store encodes. Either channel order --
    // the blit from the R8G8B8A8_SRGB output target reorders.
    const auto format = std::ranges::find_if(formats, [](const VkSurfaceFormatKHR& f) {
        return (f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB)
               && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    if (format == formats.end())
        return fail(ErrorCode::NotFound, "the surface offers neither B8G8R8A8_SRGB nor R8G8B8A8_SRGB, and "
                                         "docs/specs/UTA-0014-vulkan-draw-path.md SS 4.10 encodes through one");
    if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0)
        return fail(ErrorCode::NotFound, "the surface's images cannot be a transfer destination, which the blit needs");

    std::uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount != 0) imageCount = std::min(imageCount, caps.maxImageCount);

    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (const VkCompositeAlphaFlagBitsKHR candidate :
         {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
          VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
        if ((caps.supportedCompositeAlpha & candidate) != 0) {
            alpha = candidate;
            break;
        }
    }

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = gpu.surface();
    info.minImageCount = imageCount;
    info.imageFormat = format->format;
    info.imageColorSpace = format->colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = alpha;
    // FIFO is the one present mode every driver must offer.
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    info.oldSwapchain = old;
    UTA_CHECK(check(vkCreateSwapchainKHR(swapchain->device_, &info, nullptr, &swapchain->handle_),
                    "vkCreateSwapchainKHR"));

    std::uint32_t count = 0;
    UTA_CHECK(check(vkGetSwapchainImagesKHR(swapchain->device_, swapchain->handle_, &count, nullptr),
                    "vkGetSwapchainImagesKHR"));
    swapchain->images_.resize(count);
    UTA_CHECK(check(vkGetSwapchainImagesKHR(swapchain->device_, swapchain->handle_, &count, swapchain->images_.data()),
                    "vkGetSwapchainImagesKHR"));
    return swapchain;
}

Swapchain::~Swapchain() {
    if (handle_ == VK_NULL_HANDLE) return;
    // An image of it may still be queued to present.
    vkDeviceWaitIdle(device_);
    vkDestroySwapchainKHR(device_, handle_, nullptr);
}

} // namespace uta::urender
