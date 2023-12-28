#pragma once

#include "vulkan_device.h"

#include <vulkan/vulkan.h>
#include <vector>

struct VulkanDescriptorAllocator {
    VkDescriptorPool current_pool;
    std::vector<VkDescriptorPool> used_pools;
    std::vector<VkDescriptorPool> free_pools;
};

bool initializeDescriptorAllocator();
void shutdownDescriptorAllocator(VulkanDevice *device);

void resetDescriptorAllocator(VulkanDevice *device);

bool allocateDescriptorSetFromDescriptorAllocator(VulkanDevice *device, VkDescriptorSetLayout layout, VkDescriptorSet *out_descriptor_set);