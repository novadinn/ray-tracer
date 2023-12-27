#pragma once

#include "vulkan_device.h"

#include <vulkan/vulkan.h>

struct VulkanPipeline {
  VkPipeline handle;
  VkPipelineLayout layout;
};

bool createGraphicsPipeline(
    VulkanDevice *device, VkRenderPass render_pass,
    std::vector<VkDescriptorSetLayout> descriptor_set_layouts,
    std::vector<VkPipelineShaderStageCreateInfo> stages,
    VulkanPipeline *out_pipeline);
bool createComputePipeline(
    VulkanDevice *device,
    std::vector<VkDescriptorSetLayout> descriptor_set_layouts,
    VkPipelineShaderStageCreateInfo stage, VulkanPipeline *out_pipeline);
void destroyPipeline(VulkanPipeline *pipeline, VulkanDevice *device);