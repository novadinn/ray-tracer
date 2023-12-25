#pragma once

#include "vulkan_device.h"

#include <vulkan/vulkan.h>

bool createCommandPool(VulkanDevice *device, uint32_t queue_family_index,
                       VkCommandPool *out_command_pool);

bool allocateCommandBuffer(VulkanDevice *device, VkCommandPool command_pool,
                           VkCommandBuffer *out_command_buffer);
void beginCommandBuffer(VkCommandBuffer command_buffer, VkCommandBufferUsageFlags usage_flags);
bool allocateAndBeginSingleUseCommandBuffer(VulkanDevice *device, VkCommandPool command_pool,
                                            VkCommandBuffer *out_command_buffer);
void endAndFreeSingleUseCommandBuffer(VkCommandBuffer command_buffer, VulkanDevice *device, VkCommandPool command_pool, VkQueue queue);

bool createSemaphore(VulkanDevice *device, VkSemaphore *out_semaphore);

bool createFence(VulkanDevice *device, VkFence *out_fence);