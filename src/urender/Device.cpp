// Bringing up a Vulkan device -- docs/specs/UTA-0014-vulkan-draw-path.md SS 4.4.

#include "urender/Device.h"

#include "core/Log.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <vector>

namespace uta::urender {

LogCategory logRender("urender");

namespace {

struct Requirement {
    std::string_view name;
    bool (*met)(const DeviceCandidate&);
};

/// SS 4.4's table, in its order. The first a device fails is the one its
/// refusal names.
const std::array<Requirement, 10> REQUIREMENTS = {{
    {"Vulkan 1.3", [](const DeviceCandidate& c) { return c.apiVersion >= VK_API_VERSION_1_3; }},
    {"a graphics queue", [](const DeviceCandidate& c) { return c.graphicsQueue; }},
    {"present support on its graphics queue",
     [](const DeviceCandidate& c) { return !c.presentSupport.has_value() || *c.presentSupport; }},
    {"dynamicRendering", [](const DeviceCandidate& c) { return c.v13.dynamicRendering == VK_TRUE; }},
    {"synchronization2", [](const DeviceCandidate& c) { return c.v13.synchronization2 == VK_TRUE; }},
    {"runtimeDescriptorArray",
     [](const DeviceCandidate& c) { return c.v12.runtimeDescriptorArray == VK_TRUE; }},
    {"shaderSampledImageArrayNonUniformIndexing",
     [](const DeviceCandidate& c) { return c.v12.shaderSampledImageArrayNonUniformIndexing == VK_TRUE; }},
    {"descriptorBindingPartiallyBound",
     [](const DeviceCandidate& c) { return c.v12.descriptorBindingPartiallyBound == VK_TRUE; }},
    {"descriptorBindingVariableDescriptorCount",
     [](const DeviceCandidate& c) { return c.v12.descriptorBindingVariableDescriptorCount == VK_TRUE; }},
    {"textureCompressionBC",
     [](const DeviceCandidate& c) { return c.core.textureCompressionBC == VK_TRUE; }},
}};

int preference(VkPhysicalDeviceType type) noexcept {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 0;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 1;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 2;
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return 3;
    default: return 4;
    }
}

constexpr const char* VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";

bool layerInstalled(const char* wanted) {
    std::uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS) return false;
    std::vector<VkLayerProperties> layers(count);
    if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS) return false;
    return std::ranges::any_of(layers, [wanted](const VkLayerProperties& layer) {
        return std::strcmp(layer.layerName, wanted) == 0;
    });
}

DeviceCandidate describe(VkPhysicalDevice device) {
    DeviceCandidate candidate;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    candidate.name = properties.deviceName;
    candidate.apiVersion = properties.apiVersion;
    candidate.type = properties.deviceType;

    candidate.v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    candidate.v13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    candidate.v12.pNext = &candidate.v13;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &candidate.v12;
    // A 1.0 device cannot be asked for the 1.2 and 1.3 structs; it fails the
    // first requirement regardless, so its features are left false.
    if (properties.apiVersion >= VK_API_VERSION_1_3) {
        vkGetPhysicalDeviceFeatures2(device, &features);
        candidate.core = features.features;
    }
    candidate.v12.pNext = nullptr;
    candidate.v13.pNext = nullptr;

    std::uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());
    candidate.graphicsQueue = std::ranges::any_of(families, [](const VkQueueFamilyProperties& f) {
        return (f.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
    });
    return candidate;
}

std::uint32_t graphicsFamilyOf(VkPhysicalDevice device) {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
    for (std::uint32_t i = 0; i < count; ++i)
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) return i;
    return 0; // unreachable for a selected device: "a graphics queue" is a requirement
}

} // namespace

std::string_view firstMissingRequirement(const DeviceCandidate& candidate) noexcept {
    for (const Requirement& requirement : REQUIREMENTS)
        if (!requirement.met(candidate)) return requirement.name;
    return {};
}

Result<std::size_t> selectDevice(std::span<const DeviceCandidate> candidates) {
    if (candidates.empty())
        return fail(ErrorCode::NotFound, "the Vulkan instance enumerated no physical device");

    std::optional<std::size_t> best;
    std::string refusals;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const std::string_view missing = firstMissingRequirement(candidates[i]);
        if (missing.empty()) {
            if (!best || preference(candidates[i].type) < preference(candidates[*best].type)) best = i;
        } else {
            refusals += std::format("{}{} lacks {}", refusals.empty() ? "" : "; ", candidates[i].name,
                                    missing);
        }
    }
    if (best) return *best;
    return fail(ErrorCode::NotFound,
                std::format("no Vulkan device meets docs/specs/UTA-0014-vulkan-draw-path.md SS 4.4: {}",
                            refusals));
}

std::string resultName(VkResult result) {
    const char* name = "an unnamed VkResult";
    switch (result) {
    case VK_SUCCESS: name = "VK_SUCCESS"; break;
    case VK_NOT_READY: name = "VK_NOT_READY"; break;
    case VK_TIMEOUT: name = "VK_TIMEOUT"; break;
    case VK_INCOMPLETE: name = "VK_INCOMPLETE"; break;
    case VK_SUBOPTIMAL_KHR: name = "VK_SUBOPTIMAL_KHR"; break;
    case VK_ERROR_OUT_OF_HOST_MEMORY: name = "VK_ERROR_OUT_OF_HOST_MEMORY"; break;
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: name = "VK_ERROR_OUT_OF_DEVICE_MEMORY"; break;
    case VK_ERROR_INITIALIZATION_FAILED: name = "VK_ERROR_INITIALIZATION_FAILED"; break;
    case VK_ERROR_DEVICE_LOST: name = "VK_ERROR_DEVICE_LOST"; break;
    case VK_ERROR_MEMORY_MAP_FAILED: name = "VK_ERROR_MEMORY_MAP_FAILED"; break;
    case VK_ERROR_LAYER_NOT_PRESENT: name = "VK_ERROR_LAYER_NOT_PRESENT"; break;
    case VK_ERROR_EXTENSION_NOT_PRESENT: name = "VK_ERROR_EXTENSION_NOT_PRESENT"; break;
    case VK_ERROR_FEATURE_NOT_PRESENT: name = "VK_ERROR_FEATURE_NOT_PRESENT"; break;
    case VK_ERROR_INCOMPATIBLE_DRIVER: name = "VK_ERROR_INCOMPATIBLE_DRIVER"; break;
    case VK_ERROR_TOO_MANY_OBJECTS: name = "VK_ERROR_TOO_MANY_OBJECTS"; break;
    case VK_ERROR_FORMAT_NOT_SUPPORTED: name = "VK_ERROR_FORMAT_NOT_SUPPORTED"; break;
    case VK_ERROR_FRAGMENTED_POOL: name = "VK_ERROR_FRAGMENTED_POOL"; break;
    case VK_ERROR_OUT_OF_POOL_MEMORY: name = "VK_ERROR_OUT_OF_POOL_MEMORY"; break;
    case VK_ERROR_SURFACE_LOST_KHR: name = "VK_ERROR_SURFACE_LOST_KHR"; break;
    case VK_ERROR_OUT_OF_DATE_KHR: name = "VK_ERROR_OUT_OF_DATE_KHR"; break;
    default: break;
    }
    return std::format("{} ({})", name, static_cast<int>(result));
}

Result<void> check(VkResult result, std::string_view call) {
    if (result == VK_SUCCESS) return {};
    const ErrorCode code = result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY
                               ? ErrorCode::OutOfMemory
                               : ErrorCode::Unknown;
    return fail(code, std::format("{} returned {}", call, resultName(result)));
}

Result<std::unique_ptr<Gpu>> Gpu::create(bool validation) {
    std::unique_ptr<Gpu> gpu(new Gpu());

    std::vector<const char*> layers;
    if (validation) {
        if (layerInstalled(VALIDATION_LAYER)) {
            layers.push_back(VALIDATION_LAYER);
        } else {
            // SS 6: logged, because a silent absence turns a validation-clean
            // claim into an unfalsifiable one.
            UTA_LOG(logRender, LogLevel::Warning,
                    "validation was requested and {} is not installed, so this run is NOT validation-checked",
                    VALIDATION_LAYER);
        }
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "UT_Ants";
    app.pEngineName = "UT_Ants";
    app.apiVersion = VK_API_VERSION_1_3;

    // No instance extension: the surfaceless path asks for none (SS 4.3).
    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &app;
    instanceInfo.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    instanceInfo.ppEnabledLayerNames = layers.data();
    if (const VkResult r = vkCreateInstance(&instanceInfo, nullptr, &gpu->instance_); r != VK_SUCCESS) {
        gpu->instance_ = VK_NULL_HANDLE;
        return fail(ErrorCode::NotFound,
                    std::format("there is no Vulkan instance: vkCreateInstance returned {} -- no loader, "
                                "or no driver behind it",
                                resultName(r)));
    }

    std::uint32_t deviceCount = 0;
    UTA_CHECK(check(vkEnumeratePhysicalDevices(gpu->instance_, &deviceCount, nullptr),
                    "vkEnumeratePhysicalDevices"));
    std::vector<VkPhysicalDevice> devices(deviceCount);
    UTA_CHECK(check(vkEnumeratePhysicalDevices(gpu->instance_, &deviceCount, devices.data()),
                    "vkEnumeratePhysicalDevices"));

    std::vector<DeviceCandidate> candidates;
    candidates.reserve(devices.size());
    for (VkPhysicalDevice device : devices) candidates.push_back(describe(device));
    UTA_TRY(const std::size_t chosen, selectDevice(candidates));

    gpu->physical_ = devices[chosen];
    gpu->name_ = candidates[chosen].name;
    gpu->family_ = graphicsFamilyOf(gpu->physical_);
    vkGetPhysicalDeviceMemoryProperties(gpu->physical_, &gpu->memory_);

    // Enable exactly what SS 4.4 required, and nothing else.
    VkPhysicalDeviceVulkan13Features enable13{};
    enable13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    enable13.dynamicRendering = VK_TRUE;
    enable13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features enable12{};
    enable12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    enable12.pNext = &enable13;
    enable12.runtimeDescriptorArray = VK_TRUE;
    enable12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    enable12.descriptorBindingPartiallyBound = VK_TRUE;
    enable12.descriptorBindingVariableDescriptorCount = VK_TRUE;
    VkPhysicalDeviceFeatures2 enable{};
    enable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    enable.pNext = &enable12;
    enable.features.textureCompressionBC = VK_TRUE;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = gpu->family_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    // No device extension: no VK_KHR_swapchain on the surfaceless path (INV-4).
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &enable;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    UTA_CHECK(check(vkCreateDevice(gpu->physical_, &deviceInfo, nullptr, &gpu->device_), "vkCreateDevice"));
    vkGetDeviceQueue(gpu->device_, gpu->family_, 0, &gpu->queue_);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = gpu->family_;
    UTA_CHECK(check(vkCreateCommandPool(gpu->device_, &poolInfo, nullptr, &gpu->pool_), "vkCreateCommandPool"));

    UTA_LOG(logRender, LogLevel::Info, "Vulkan device: {}", gpu->name_);
    return gpu;
}

Gpu::~Gpu() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        if (pool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, pool_, nullptr);
        vkDestroyDevice(device_, nullptr);
    }
    if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
}

Result<std::uint32_t> Gpu::memoryType(std::uint32_t allowed, VkMemoryPropertyFlags wanted) const {
    for (std::uint32_t i = 0; i < memory_.memoryTypeCount; ++i)
        if ((allowed & (1u << i)) != 0 && (memory_.memoryTypes[i].propertyFlags & wanted) == wanted) return i;
    return fail(ErrorCode::NotFound,
                std::format("{} has no memory type with properties {:#x} among {:#x}", name_, wanted, allowed));
}

Result<void> Gpu::run(const std::function<void(VkCommandBuffer)>& record) {
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = pool_;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    UTA_CHECK(check(vkAllocateCommandBuffers(device_, &allocate, &commands), "vkAllocateCommandBuffers"));

    VkFence fence = VK_NULL_HANDLE;
    const auto release = [&] {
        if (fence != VK_NULL_HANDLE) vkDestroyFence(device_, fence, nullptr);
        vkFreeCommandBuffers(device_, pool_, 1, &commands);
    };

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (auto r = check(vkBeginCommandBuffer(commands, &begin), "vkBeginCommandBuffer"); !r) {
        release();
        return r;
    }
    record(commands);
    if (auto r = check(vkEndCommandBuffer(commands), "vkEndCommandBuffer"); !r) {
        release();
        return r;
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (auto r = check(vkCreateFence(device_, &fenceInfo, nullptr, &fence), "vkCreateFence"); !r) {
        release();
        return r;
    }

    VkCommandBufferSubmitInfo buffer{};
    buffer.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    buffer.commandBuffer = commands;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &buffer;
    Result<void> outcome = check(vkQueueSubmit2(queue_, 1, &submit, fence), "vkQueueSubmit2");
    if (outcome) outcome = check(vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    release();
    return outcome;
}

} // namespace uta::urender
