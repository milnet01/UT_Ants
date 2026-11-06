// Putting a frame on the caller's surface -- docs/specs/UTA-0014-vulkan-draw-path.md
// SS 4.3. Swapchain.h says what grades it.
//
// THE FRAME IS SUBMITTED AND WAITED FOR, as on the surfaceless path, so present
// waits on no semaphore: by the time it is called the image is finished.

#include "urender/Resources.h"
#include "urender/Swapchain.h"

#include <cstdint>
#include <vector>

namespace uta::urender {

namespace {

VkBufferImageCopy overlayCopy(const OverlayRect& rect, VkDeviceSize bufferOffset) {
    VkBufferImageCopy copy{};
    copy.bufferOffset = bufferOffset; // rows tightly packed: the run is one colour
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageOffset = {static_cast<std::int32_t>(rect.x), static_cast<std::int32_t>(rect.y), 0};
    copy.imageExtent = {rect.width, rect.height, 1};
    return copy;
}

/// UTA-0452: the black box, then the text over it. Two copies with a barrier
/// between, since the text's rectangles overlap the box's and one copy's
/// regions may not overlap each other. Every write is a transfer write, so
/// the blit before is ordered by the same barrier shape.
void recordOverlay(VkCommandBuffer commands, VkImage target, const OverlayLayout& overlay, VkBuffer pixels) {
    const auto afterLastWrite = [&] {
        transitionImage(commands, target, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    };
    afterLastWrite(); // the blit
    const VkBufferImageCopy box = overlayCopy(overlay.backing, 0);
    vkCmdCopyBufferToImage(commands, pixels, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &box);
    if (overlay.lit.empty()) return;

    afterLastWrite(); // the box
    const VkDeviceSize white = overlay.pixelCount() * 4;
    std::vector<VkBufferImageCopy> text;
    text.reserve(overlay.lit.size());
    for (const OverlayRect& rect : overlay.lit) text.push_back(overlayCopy(rect, white));
    vkCmdCopyBufferToImage(commands, pixels, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<std::uint32_t>(text.size()), text.data());
}

} // namespace

Result<std::optional<std::uint32_t>> Swapchain::acquire() {
    if (empty()) return std::optional<std::uint32_t>{};

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    UTA_CHECK(check(vkCreateFence(device_, &fenceInfo, nullptr, &fence), "vkCreateFence"));

    std::uint32_t index = 0;
    const VkResult acquired = vkAcquireNextImageKHR(device_, handle_, UINT64_MAX, VK_NULL_HANDLE, fence, &index);
    // The fence says the image is ready for a submission that waits on nothing.
    Result<void> ready;
    if (acquired == VK_SUCCESS || acquired == VK_SUBOPTIMAL_KHR)
        ready = check(vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    vkDestroyFence(device_, fence, nullptr);

    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        stale_ = true;
        return std::optional<std::uint32_t>{};
    }
    if (acquired == VK_SUBOPTIMAL_KHR) stale_ = true; // still presentable; rebuilt next frame
    else UTA_CHECK(check(acquired, "vkAcquireNextImageKHR"));
    UTA_CHECK(ready);
    return std::optional<std::uint32_t>{index};
}

void Swapchain::recordBlit(VkCommandBuffer commands, Image& output, std::uint32_t index,
                           const OverlayLayout& overlay, VkBuffer overlayPixels) {
    const VkImage target = images_[index];
    output.transition(commands, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    // Every pixel is overwritten, so the image's last contents are not kept.
    transitionImage(commands, target, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {static_cast<std::int32_t>(output.desc().width),
                          static_cast<std::int32_t>(output.desc().height), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[1] = {static_cast<std::int32_t>(extent_.width), static_cast<std::int32_t>(extent_.height), 1};
    vkCmdBlitImage(commands, output.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
    if (!overlay.empty() && overlayPixels != VK_NULL_HANDLE) recordOverlay(commands, target, overlay, overlayPixels);
    transitionImage(commands, target, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}

Result<void> Swapchain::present(VkQueue queue, std::uint32_t index) {
    VkPresentInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.swapchainCount = 1;
    info.pSwapchains = &handle_;
    info.pImageIndices = &index;
    const VkResult presented = vkQueuePresentKHR(queue, &info);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        stale_ = true;
        return {};
    }
    return check(presented, "vkQueuePresentKHR");
}

} // namespace uta::urender
