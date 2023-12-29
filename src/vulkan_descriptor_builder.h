#pragma once

#include "vulkan_device.h"

#include <vulkan/vulkan.h>
#include <vector>

struct VulkanDescriptorBuilder {
    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
};

bool beginDescriptorBuilder(VulkanDescriptorBuilder *out_descriptor_builder);
void bindDescriptorBuilderBuffer(uint32_t binding,
                                VkDescriptorBufferInfo *buffer_info,
                                VkDescriptorType type,
                                VkShaderStageFlags stage_flags, 
                                VulkanDescriptorBuilder *out_descriptor_builder);
void bindDescriptorBuilderImage(uint32_t binding,
                                VkDescriptorImageInfo *image_info,
                                VkDescriptorType type,
                                VkShaderStageFlags stage_flags, 
                                VulkanDescriptorBuilder *out_descriptor_builder);
bool endDescriptorBuilder(VulkanDescriptorBuilder *descriptor_builder, VulkanDevice *device, VkDescriptorSet *out_set, VkDescriptorSetLayout *out_layout);
bool endDescriptorBuilder(VulkanDescriptorBuilder *descriptor_builder, VulkanDevice *device, VkDescriptorSet *out_set);
