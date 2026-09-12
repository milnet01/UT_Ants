// Bringing up a Vulkan device -- docs/specs/UTA-0014-vulkan-draw-path.md SS 4.4.
//
// INTERNAL: this header includes Vulkan, so nothing outside urender and its
// tests may include it (INV-2).
//
// READ THE FEATURE BIT, NEVER THE EXTENSION STRING. A driver can advertise an
// extension whose feature is not usable, and a device selected on the string
// makes every draw undefined (INV-3's breaking case).

#pragma once

#include "core/Error.h"
#include "core/Log.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace uta::urender {

/// urender's log category.
extern LogCategory logRender;

/// What SS 4.4 reads about one physical device. Plain data, so INV-3 can
/// build a table of these without a device.
struct DeviceCandidate {
    std::string name;
    std::uint32_t apiVersion = 0;
    VkPhysicalDeviceType type = VK_PHYSICAL_DEVICE_TYPE_OTHER;
    bool graphicsQueue = false;
    /// Engaged on the presenting path only; the surfaceless path never asks.
    std::optional<bool> presentSupport;
    VkPhysicalDeviceFeatures core{};
    VkPhysicalDeviceVulkan12Features v12{};
    VkPhysicalDeviceVulkan13Features v13{};
};

/// The first SS 4.4 requirement `candidate` fails, in that table's order, or
/// an empty view when it meets every one.
[[nodiscard]] std::string_view firstMissingRequirement(const DeviceCandidate& candidate) noexcept;

/// The index of the device to use: the first that meets every requirement,
/// preferring a discrete GPU, then an integrated one, then anything else. When
/// none qualifies, NotFound naming each device and the first requirement it
/// failed -- SS 4.4's third refusal. An empty span is the second.
[[nodiscard]] Result<std::size_t> selectDevice(std::span<const DeviceCandidate> candidates);

/// "VK_ERROR_INCOMPATIBLE_DRIVER (-9)".
[[nodiscard]] std::string resultName(VkResult result);

/// NotFound or Unknown carrying `call` and the VkResult, or success.
[[nodiscard]] Result<void> check(VkResult result, std::string_view call);

/// The instance, the chosen device and its one graphics queue.
///
/// Destroying it waits for the device to go idle and destroys the device, then
/// the instance. Every resource made from it must be gone first, which is the
/// Renderer's job: it declares its Gpu first so it is destroyed last.
class Gpu {
public:
    /// SS 4.4's three refusals: no instance, no physical device, or no device
    /// meeting every requirement.
    [[nodiscard]] static Result<std::unique_ptr<Gpu>> create(bool validation);

    Gpu(const Gpu&) = delete;
    Gpu& operator=(const Gpu&) = delete;
    ~Gpu();

    [[nodiscard]] VkInstance instance() const noexcept { return instance_; }
    [[nodiscard]] VkPhysicalDevice physical() const noexcept { return physical_; }
    [[nodiscard]] VkDevice device() const noexcept { return device_; }
    [[nodiscard]] VkQueue queue() const noexcept { return queue_; }
    [[nodiscard]] std::uint32_t queueFamily() const noexcept { return family_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    /// A memory type allowed by `allowed` with every one of `wanted`.
    [[nodiscard]] Result<std::uint32_t> memoryType(std::uint32_t allowed,
                                                   VkMemoryPropertyFlags wanted) const;

    /// Record with `record` into a fresh command buffer, submit it, and wait
    /// for it to finish.
    [[nodiscard]] Result<void> run(const std::function<void(VkCommandBuffer)>& record);

private:
    Gpu() = default;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t family_ = 0;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory_{};
    std::string name_;
};

} // namespace uta::urender
