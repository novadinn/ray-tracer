#pragma once

#include "vulkan_device.h"

#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

struct VulkanDescriptorLayoutCache {
    struct DescriptorLayoutInfo {
        std::vector<VkDescriptorSetLayoutBinding> bindings;

        bool operator==(const DescriptorLayoutInfo &other) const;
        size_t hash() const;
    };

    struct DescriptorLayoutHash {
        std::size_t operator()(const DescriptorLayoutInfo &info) const {
            return info.hash();
        }
    };

    std::unordered_map<DescriptorLayoutInfo, VkDescriptorSetLayout, DescriptorLayoutHash> layout_cache;
};

bool initializeDescriptorLayoutCache();
void shutdownDescriptorLayoutCache(VulkanDevice *device);

VkDescriptorSetLayout createDescriptorLayoutFromCache(VulkanDevice *device, VkDescriptorSetLayoutCreateInfo *layout_create_info);