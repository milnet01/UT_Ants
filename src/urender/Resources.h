// Buffers and images, and the one allocation rule this item uses.
//
// INTERNAL (includes Vulkan).
//
// NO VULKAN MEMORY ALLOCATOR, AND NONE IS CLAIMED (SS 8). A buffer or image
// made here owns its own allocation. The one class that would exhaust a
// driver's allocation count -- a bundle's textures, five maps a material --
// is bound into one shared block per memory type instead (Materials.cpp).

#pragma once

#include "core/Error.h"
#include "urender/Device.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace uta::urender {

class Buffer {
public:
    Buffer() = default;
    Buffer(Buffer&& other) noexcept { *this = std::move(other); }
    Buffer& operator=(Buffer&& other) noexcept;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    ~Buffer() { reset(); }

    /// `hostVisible` maps it for the buffer's whole life; otherwise it is
    /// device-local and filled through `upload`.
    [[nodiscard]] static Result<Buffer> create(const Gpu& gpu, VkDeviceSize size,
                                               VkBufferUsageFlags usage, bool hostVisible);

    /// A device-local buffer holding `bytes`, copied through a staging buffer.
    /// An empty span makes a minimal buffer: Vulkan has no zero-sized one.
    [[nodiscard]] static Result<Buffer> upload(Gpu& gpu, std::span<const std::byte> bytes,
                                               VkBufferUsageFlags usage);

    [[nodiscard]] VkBuffer handle() const noexcept { return handle_; }
    [[nodiscard]] VkDeviceSize size() const noexcept { return size_; }
    /// Null unless host-visible.
    [[nodiscard]] std::byte* mapped() const noexcept { return mapped_; }

    void reset() noexcept;

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkBuffer handle_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    std::byte* mapped_ = nullptr;
};

struct ImageDesc {
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::uint32_t width = 1, height = 1, mipLevels = 1;
    VkImageUsageFlags usage = 0;
};

class Image {
public:
    Image() = default;
    Image(Image&& other) noexcept { *this = std::move(other); }
    Image& operator=(Image&& other) noexcept;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    ~Image() { reset(); }

    /// An image with its own device-local allocation, and a view of every level.
    [[nodiscard]] static Result<Image> create(const Gpu& gpu, const ImageDesc& desc);

    /// An image with no memory yet; `bind` gives it some and makes its view.
    [[nodiscard]] static Result<Image> createUnbound(const Gpu& gpu, const ImageDesc& desc);
    [[nodiscard]] Result<void> bind(VkDeviceMemory memory, VkDeviceSize offset);

    [[nodiscard]] VkImage handle() const noexcept { return handle_; }
    [[nodiscard]] VkImageView view() const noexcept { return view_; }
    [[nodiscard]] const ImageDesc& desc() const noexcept { return desc_; }
    [[nodiscard]] VkImageLayout layout() const noexcept { return layout_; }

    /// Record a barrier moving every level to `target`, from whatever layout
    /// this image was last moved to. A no-op when it is already there.
    void transition(VkCommandBuffer commands, VkImageLayout target);

    void reset() noexcept;

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkImage handle_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE; ///< null when the memory is shared
    ImageDesc desc_;
    VkImageLayout layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
};

/// An allocation several images are bound into, freed with it.
class MemoryBlock {
public:
    MemoryBlock() = default;
    MemoryBlock(MemoryBlock&& other) noexcept { *this = std::move(other); }
    MemoryBlock& operator=(MemoryBlock&& other) noexcept;
    MemoryBlock(const MemoryBlock&) = delete;
    MemoryBlock& operator=(const MemoryBlock&) = delete;
    ~MemoryBlock() { reset(); }

    [[nodiscard]] static Result<MemoryBlock> allocate(const Gpu& gpu, VkDeviceSize size,
                                                      std::uint32_t memoryType);
    [[nodiscard]] VkDeviceMemory handle() const noexcept { return memory_; }
    void reset() noexcept;

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
};

/// The aspect an image of `format` has: depth for a depth format, else colour.
[[nodiscard]] VkImageAspectFlags aspectOf(VkFormat format) noexcept;

} // namespace uta::urender
