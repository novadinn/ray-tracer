#pragma once

#include "vulkan_device.h"

#include "vk_mem_alloc.h"
#include <vulkan/vulkan.h>

struct VulkanTexture {
  VkImage handle;
  VkImageView view;
  VmaAllocation memory;
  VkSampler sampler;

  VkFormat format;
  uint32_t width, height;
};

bool createTexture(VulkanDevice *device, VmaAllocator vma_allocator,
                   VkFormat format, uint32_t width, uint32_t height,
                   VulkanTexture *out_texture);
void destroyTexture(VulkanTexture *texture, VulkanDevice *device,
                    VmaAllocator vma_allocator);