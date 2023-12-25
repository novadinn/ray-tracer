#pragma once

#include "vk_mem_alloc.h"
#include <vulkan/vulkan.h>

struct VulkanBuffer {
  VkBuffer handle;
  VmaAllocation allocation;
  uint64_t size;
};

bool createBuffer(uint64_t size, VkBufferUsageFlags usage_flags,
                  VkMemoryPropertyFlags memory_flags, VmaMemoryUsage vma_usage);
void destroyBuffer();

void lockBuffer();
void unlockBuffer();

bool loadBufferData();