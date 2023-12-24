#pragma once

#include <stdint.h>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

enum VulkanDeviceQueueType {
  VULKAN_DEVICE_QUEUE_TYPE_GRAPHICS,
  VULKAN_DEVICE_QUEUE_TYPE_PRESENT,
  VULKAN_DEVICE_QUEUE_TYPE_COMPUTE,
  VULKAN_DEVICE_QUEUE_TYPE_TRANSFER,
};

struct VulkanDevice {
  VkPhysicalDevice physical_device;
  VkDevice logical_device;

  VkPhysicalDeviceProperties properties;
  VkPhysicalDeviceFeatures features;
  VkPhysicalDeviceMemoryProperties memory;

  std::unordered_map<VulkanDeviceQueueType, uint32_t> queue_family_indices;
};

bool createDevice(VkInstance instance, VkSurfaceKHR surface,
                  VulkanDevice *out_device);
void destroyDevice(VulkanDevice *device);
