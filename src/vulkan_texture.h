#pragma once

#include "vulkan_buffer.h"
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
                   VkImageUsageFlags usage_flags, VulkanTexture *out_texture);
void destroyTexture(VulkanTexture *texture, VulkanDevice *device,
                    VmaAllocator vma_allocator);

bool writeTextureData(VulkanTexture *texture, VulkanDevice *device,
                      void *pixels, VmaAllocator vma_allocator, VkQueue queue,
                      VkCommandPool command_pool, uint32_t queue_family_index);
bool transitionTextureLayout(VulkanTexture *texture,
                             VkCommandBuffer command_buffer,
                             VkImageLayout old_layout, VkImageLayout new_layout,
                             uint32_t queue_family_index);
bool copyFromBufferToTexture(VulkanTexture *texture, VulkanBuffer *buffer,
                             VkCommandBuffer command_buffer);