#pragma once

#include "vulkan_device.h"

#include "vk_mem_alloc.h"
#include <vulkan/vulkan.h>

struct VulkanBuffer {
  VkBuffer handle;
  VmaAllocation memory;

  uint64_t size;
};

bool createBuffer(VmaAllocator vma_allocator, uint64_t size, VkBufferUsageFlags usage_flags,
                  VkMemoryPropertyFlags memory_flags, VmaMemoryUsage vma_usage, VulkanBuffer *out_buffer);
void destroyBuffer(VulkanBuffer *buffer, VmaAllocator vma_allocator);

void *lockBuffer(VulkanBuffer *buffer, VmaAllocator vma_allocator);
void unlockBuffer(VulkanBuffer *buffer, VmaAllocator vma_allocator);

bool loadBufferData(VulkanBuffer *buffer, VmaAllocator vma_allocator, void *data);
bool loadBufferDataStaging(VulkanBuffer *buffer, VulkanDevice *device, VmaAllocator vma_allocator, void *data, VkQueue queue, VkCommandPool command_pool);
bool copyBufferTo(VulkanDevice *device, VulkanBuffer *source, VulkanBuffer *dest, VkQueue queue, VkCommandPool command_pool);