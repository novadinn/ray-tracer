#pragma once

#include "vulkan_device.h"

#include <stdint.h>
#include <vector>
#include <vulkan/vulkan.h>

struct VulkanSwapchain {
  VkSwapchainKHR handle;
  uint32_t max_frames_in_flight;
  std::vector<VkImage> images;
  std::vector<VkImageView> image_views;
  VkSurfaceFormatKHR surface_format;
};

bool createSwapchain(VulkanDevice *device, VkSurfaceKHR surface, uint32_t width,
                     uint32_t height, VulkanSwapchain *out_swapchain);
void destroySwapchain(VulkanSwapchain *swapchain, VulkanDevice *device);