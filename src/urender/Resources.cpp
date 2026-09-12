// Buffers and images -- Resources.h.

#include "urender/Resources.h"

#include <algorithm>
#include <cstring>

namespace uta::urender {

namespace {

/// The stage and access a layout implies, for a synchronization2 barrier.
struct Use {
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 access;
};

Use useOf(VkImageLayout layout) noexcept {
    switch (layout) {
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
    case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
        return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
        return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
                    | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT};
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT};
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        return {VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0};
    default:
        return {VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0};
    }
}

} // namespace

VkImageAspectFlags aspectOf(VkFormat format) noexcept {
    switch (format) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_X8_D24_UNORM_PACK32: return VK_IMAGE_ASPECT_DEPTH_BIT;
    default: return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

// -- Buffer ------------------------------------------------------------------

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        reset();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        handle_ = std::exchange(other.handle_, VK_NULL_HANDLE);
        memory_ = std::exchange(other.memory_, VK_NULL_HANDLE);
        size_ = std::exchange(other.size_, 0);
        mapped_ = std::exchange(other.mapped_, nullptr);
    }
    return *this;
}

void Buffer::reset() noexcept {
    if (device_ == VK_NULL_HANDLE) return;
    if (handle_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, handle_, nullptr);
    if (memory_ != VK_NULL_HANDLE) vkFreeMemory(device_, memory_, nullptr); // unmaps too
    device_ = VK_NULL_HANDLE;
    handle_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
    size_ = 0;
    mapped_ = nullptr;
}

Result<Buffer> Buffer::create(const Gpu& gpu, VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible) {
    Buffer buffer;
    buffer.device_ = gpu.device();
    buffer.size_ = size;

    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    UTA_CHECK(check(vkCreateBuffer(buffer.device_, &info, nullptr, &buffer.handle_), "vkCreateBuffer"));

    VkMemoryRequirements needs{};
    vkGetBufferMemoryRequirements(buffer.device_, buffer.handle_, &needs);
    const VkMemoryPropertyFlags wanted = hostVisible
                                             ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                             : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    UTA_TRY(const std::uint32_t type, gpu.memoryType(needs.memoryTypeBits, wanted));

    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = needs.size;
    allocate.memoryTypeIndex = type;
    UTA_CHECK(check(vkAllocateMemory(buffer.device_, &allocate, nullptr, &buffer.memory_), "vkAllocateMemory"));
    UTA_CHECK(check(vkBindBufferMemory(buffer.device_, buffer.handle_, buffer.memory_, 0), "vkBindBufferMemory"));

    if (hostVisible) {
        void* mapped = nullptr;
        UTA_CHECK(check(vkMapMemory(buffer.device_, buffer.memory_, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory"));
        buffer.mapped_ = static_cast<std::byte*>(mapped);
    }
    return buffer;
}

Result<Buffer> Buffer::upload(Gpu& gpu, std::span<const std::byte> bytes, VkBufferUsageFlags usage) {
    const VkDeviceSize size = std::max<VkDeviceSize>(bytes.size(), 16);
    UTA_TRY(Buffer staging, create(gpu, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true));
    if (!bytes.empty()) std::memcpy(staging.mapped(), bytes.data(), bytes.size());
    UTA_TRY(Buffer target, create(gpu, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false));
    UTA_CHECK(gpu.run([&](VkCommandBuffer commands) {
        VkBufferCopy region{0, 0, size};
        vkCmdCopyBuffer(commands, staging.handle(), target.handle(), 1, &region);
    }));
    return target;
}

// -- Image -------------------------------------------------------------------

Image& Image::operator=(Image&& other) noexcept {
    if (this != &other) {
        reset();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        handle_ = std::exchange(other.handle_, VK_NULL_HANDLE);
        view_ = std::exchange(other.view_, VK_NULL_HANDLE);
        memory_ = std::exchange(other.memory_, VK_NULL_HANDLE);
        desc_ = other.desc_;
        layout_ = std::exchange(other.layout_, VK_IMAGE_LAYOUT_UNDEFINED);
    }
    return *this;
}

void Image::reset() noexcept {
    if (device_ == VK_NULL_HANDLE) return;
    if (view_ != VK_NULL_HANDLE) vkDestroyImageView(device_, view_, nullptr);
    if (handle_ != VK_NULL_HANDLE) vkDestroyImage(device_, handle_, nullptr);
    if (memory_ != VK_NULL_HANDLE) vkFreeMemory(device_, memory_, nullptr);
    device_ = VK_NULL_HANDLE;
    handle_ = VK_NULL_HANDLE;
    view_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
    layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
}

Result<Image> Image::createUnbound(const Gpu& gpu, const ImageDesc& desc) {
    Image image;
    image.device_ = gpu.device();
    image.desc_ = desc;

    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = desc.format;
    info.extent = {desc.width, desc.height, 1};
    info.mipLevels = desc.mipLevels;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = desc.usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    UTA_CHECK(check(vkCreateImage(image.device_, &info, nullptr, &image.handle_), "vkCreateImage"));
    return image;
}

Result<void> Image::bind(VkDeviceMemory memory, VkDeviceSize offset) {
    UTA_CHECK(check(vkBindImageMemory(device_, handle_, memory, offset), "vkBindImageMemory"));
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = handle_;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = desc_.format;
    view.subresourceRange = {aspectOf(desc_.format), 0, desc_.mipLevels, 0, 1};
    return check(vkCreateImageView(device_, &view, nullptr, &view_), "vkCreateImageView");
}

Result<Image> Image::create(const Gpu& gpu, const ImageDesc& desc) {
    UTA_TRY(Image image, createUnbound(gpu, desc));
    VkMemoryRequirements needs{};
    vkGetImageMemoryRequirements(image.device_, image.handle_, &needs);
    UTA_TRY(const std::uint32_t type, gpu.memoryType(needs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = needs.size;
    allocate.memoryTypeIndex = type;
    UTA_CHECK(check(vkAllocateMemory(image.device_, &allocate, nullptr, &image.memory_), "vkAllocateMemory"));
    UTA_CHECK(image.bind(image.memory_, 0));
    return image;
}

void transitionImage(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect, std::uint32_t mipLevels,
                     VkImageLayout from, VkImageLayout to) {
    const Use before = useOf(from);
    const Use after = useOf(to);
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = before.stage;
    barrier.srcAccessMask = before.access;
    barrier.dstStageMask = after.stage;
    barrier.dstAccessMask = after.access;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, mipLevels, 0, 1};
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands, &dependency);
}

void Image::transition(VkCommandBuffer commands, VkImageLayout target) {
    if (layout_ == target) return;
    transitionImage(commands, handle_, aspectOf(desc_.format), desc_.mipLevels, layout_, target);
    layout_ = target;
}

// -- MemoryBlock -------------------------------------------------------------

MemoryBlock& MemoryBlock::operator=(MemoryBlock&& other) noexcept {
    if (this != &other) {
        reset();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        memory_ = std::exchange(other.memory_, VK_NULL_HANDLE);
    }
    return *this;
}

void MemoryBlock::reset() noexcept {
    if (device_ != VK_NULL_HANDLE && memory_ != VK_NULL_HANDLE) vkFreeMemory(device_, memory_, nullptr);
    device_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
}

Result<MemoryBlock> MemoryBlock::allocate(const Gpu& gpu, VkDeviceSize size, std::uint32_t memoryType) {
    MemoryBlock block;
    block.device_ = gpu.device();
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = size;
    allocate.memoryTypeIndex = memoryType;
    UTA_CHECK(check(vkAllocateMemory(block.device_, &allocate, nullptr, &block.memory_), "vkAllocateMemory"));
    return block;
}

} // namespace uta::urender
