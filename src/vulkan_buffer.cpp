#include "vulkan_buffer.h"

#include "vulkan_common.h"
#include "logger.h"
#include "vulkan_resources.h"

bool createBuffer(VmaAllocator vma_allocator, uint64_t size, VkBufferUsageFlags usage_flags,
                  VkMemoryPropertyFlags memory_flags, VmaMemoryUsage vma_usage, VulkanBuffer *out_buffer) {
  out_buffer->size = size;

    VkBufferCreateInfo buffer_create_info = {};
  buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_create_info.pNext = 0;
  buffer_create_info.flags = 0;
  buffer_create_info.size = size;
  buffer_create_info.usage = usage_flags;
  buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  buffer_create_info.queueFamilyIndexCount = 0;
  buffer_create_info.pQueueFamilyIndices = 0;

  VmaAllocationCreateInfo vma_allocation_create_info = {};
  /* vma_allocation_create_info.flags; */
  vma_allocation_create_info.usage = vma_usage;
  /*vma_allocation_create_info.requiredFlags = memory_flags;
  vma_allocation_create_info.preferredFlags;
  vma_allocation_create_info.memoryTypeBits;
  vma_allocation_create_info.pool;
  vma_allocation_create_info.pUserData;
  vma_allocation_create_info.priority; */

  VK_CHECK(vmaCreateBuffer(vma_allocator, &buffer_create_info,
                           &vma_allocation_create_info, &out_buffer->handle, &out_buffer->memory, 0));


  return true;
}

void destroyBuffer(VulkanBuffer *buffer, VmaAllocator vma_allocator) {
  vmaDestroyBuffer(vma_allocator, buffer->handle, buffer->memory);
}

void *lockBuffer(VulkanBuffer *buffer, VmaAllocator vma_allocator) {
  void *data;
  VK_CHECK(vmaMapMemory(vma_allocator, buffer->memory, &data));

  return data;
}

void unlockBuffer(VulkanBuffer *buffer, VmaAllocator vma_allocator) {
  vmaUnmapMemory(vma_allocator, buffer->memory);
}

bool loadBufferData(VulkanBuffer *buffer, VmaAllocator vma_allocator, void *data) {
  void *data_ptr = lockBuffer(buffer, vma_allocator);
  memcpy(data_ptr, data, buffer->size);
  unlockBuffer(buffer, vma_allocator);

  return true;
}

bool loadBufferDataStaging(VulkanBuffer *buffer, VulkanDevice *device, VmaAllocator vma_allocator, void *data, VkQueue queue, VkCommandPool command_pool) {
  VulkanBuffer staging_buffer;
  if(!createBuffer(vma_allocator, buffer->size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             VMA_MEMORY_USAGE_CPU_ONLY, &staging_buffer)) {
    ERROR("Failed to create a staging buffer!");
    return false;
  }

  if(!loadBufferData(&staging_buffer, vma_allocator, data)) {
    ERROR("Failed to load buffer data!");
    return false;
  }

  copyBufferTo(device, &staging_buffer, buffer, queue, command_pool);

  destroyBuffer(&staging_buffer, vma_allocator);

  return true;
}

bool copyBufferTo(VulkanDevice *device, VulkanBuffer *source, VulkanBuffer *dest, VkQueue queue, VkCommandPool command_pool) {
  vkQueueWaitIdle(queue);

  VkCommandBuffer temp_command_buffer;
  allocateAndBeginSingleUseCommandBuffer(device, command_pool, &temp_command_buffer);

  VkBufferCopy copy_region;
  copy_region.srcOffset = 0;
  copy_region.dstOffset = 0;
  copy_region.size = source->size;

  vkCmdCopyBuffer(temp_command_buffer, source->handle, dest->handle, 1,
                  &copy_region);

  endAndFreeSingleUseCommandBuffer(temp_command_buffer, device, command_pool, queue);

  return true;
}