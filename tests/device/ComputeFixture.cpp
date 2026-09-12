// Running one compute shader on a device -- ComputeFixture.h.

#include "device/ComputeFixture.h"

#include "urender/Device.h"
#include "urender/Pipelines.h"
#include "urender/Resources.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>

namespace uta::test::render {

namespace {

void requireOk(const Result<void>& result) {
    if (!result.has_value()) FAIL(result.error().message());
}

} // namespace

std::vector<std::byte> runCompute(std::span<const std::uint32_t> spirv,
                                  const std::vector<std::span<const std::byte>>& inputs, std::size_t outputBytes,
                                  std::uint32_t invocations) {
    auto created = urender::Gpu::create(false);
    if (!created.has_value()) FAIL("no device, so this compute test cannot pass: " << created.error().message());
    urender::Gpu& gpu = **created;
    const VkDevice device = gpu.device();

    const auto bindingCount = static_cast<std::uint32_t>(inputs.size() + 1);
    std::vector<urender::Buffer> buffers;
    for (std::uint32_t i = 0; i < bindingCount; ++i) {
        const std::size_t size = i < inputs.size() ? inputs[i].size() : outputBytes;
        auto buffer = urender::Buffer::create(gpu, std::max<std::size_t>(size, 16),
                                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        if (!buffer.has_value()) FAIL(buffer.error().message());
        std::memset(buffer->mapped(), 0, buffer->size());
        if (i < inputs.size()) std::memcpy(buffer->mapped(), inputs[i].data(), inputs[i].size());
        buffers.push_back(std::move(*buffer));
    }

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (std::uint32_t i = 0; i < bindingCount; ++i)
        bindings.push_back({i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = bindingCount;
    layoutInfo.pBindings = bindings.data();
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    requireOk(urender::check(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &setLayout),
                             "vkCreateDescriptorSetLayout"));

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &setLayout;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    requireOk(urender::check(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout),
                             "vkCreatePipelineLayout"));

    auto module = urender::shaderModule(device, spirv);
    if (!module.has_value()) FAIL(module.error().message());
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = *module;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    requireOk(urender::check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline),
                             "vkCreateComputePipelines"));

    const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, bindingCount};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    requireOk(urender::check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool), "vkCreateDescriptorPool"));
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = pool;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &setLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    requireOk(urender::check(vkAllocateDescriptorSets(device, &allocate, &set), "vkAllocateDescriptorSets"));

    std::vector<VkDescriptorBufferInfo> infos;
    for (const urender::Buffer& buffer : buffers) infos.push_back({buffer.handle(), 0, VK_WHOLE_SIZE});
    std::vector<VkWriteDescriptorSet> writes;
    for (std::uint32_t i = 0; i < bindingCount; ++i) {
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = i;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &infos[i];
        writes.push_back(write);
    }
    vkUpdateDescriptorSets(device, bindingCount, writes.data(), 0, nullptr);

    requireOk(gpu.run([&](VkCommandBuffer commands) {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 0, nullptr);
        vkCmdDispatch(commands, (invocations + 15) / 16, 1, 1);
    }));

    const urender::Buffer& output = buffers.back();
    std::vector<std::byte> result(output.mapped(), output.mapped() + outputBytes);

    vkDestroyDescriptorPool(device, pool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyShaderModule(device, *module, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
    buffers.clear();
    return result;
}

} // namespace uta::test::render
