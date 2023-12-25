#include "vulkan_resources.h"

#include "vulkan_common.h"
#include "logger.h"

bool createCommandPool(VulkanDevice *device, uint32_t queue_family_index,
                       VkCommandPool *out_command_pool) {
  VkCommandPoolCreateInfo command_pool_create_info = {};
  command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  command_pool_create_info.pNext = 0;
  command_pool_create_info.flags =
      VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  command_pool_create_info.queueFamilyIndex = queue_family_index;

  VK_CHECK(vkCreateCommandPool(device->logical_device,
                               &command_pool_create_info, 0, out_command_pool));

  return true;
}

bool allocateCommandBuffer(VulkanDevice *device, VkCommandPool command_pool,
                           VkCommandBuffer *out_command_buffer) {
  VkCommandBufferAllocateInfo command_buffer_allocate_info = {};
  command_buffer_allocate_info.sType =
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  command_buffer_allocate_info.pNext = 0;
  command_buffer_allocate_info.commandPool = command_pool;
  command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_buffer_allocate_info.commandBufferCount = 1;

  VK_CHECK(vkAllocateCommandBuffers(device->logical_device,
                                    &command_buffer_allocate_info,
                                    out_command_buffer));

  return true;
}

void beginCommandBuffer(VkCommandBuffer command_buffer, VkCommandBufferUsageFlags usage_flags) {
  VkCommandBufferBeginInfo begin_info = {};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.pNext = 0;
  begin_info.flags = usage_flags;
  begin_info.pInheritanceInfo = 0;

  VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));
}

bool allocateAndBeginSingleUseCommandBuffer(VulkanDevice *device, VkCommandPool command_pool,
                                            VkCommandBuffer *out_command_buffer) {
  if(!allocateCommandBuffer(device, command_pool, out_command_buffer)) {
    ERROR("Failed to allocate a command buffer!");
    return false;
  }
  beginCommandBuffer(*out_command_buffer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

  return true;
}

void endAndFreeSingleUseCommandBuffer(VkCommandBuffer command_buffer, VulkanDevice *device, VkCommandPool command_pool, VkQueue queue) {
  VK_CHECK(vkEndCommandBuffer(command_buffer));

  VkSubmitInfo submit_info;
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.pNext = 0;
  submit_info.waitSemaphoreCount = 0;
  submit_info.pWaitSemaphores = 0;
  submit_info.pWaitDstStageMask = 0;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer;
  submit_info.signalSemaphoreCount = 0;
  submit_info.pSignalSemaphores = 0;

  VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, 0));
  VK_CHECK(vkQueueWaitIdle(queue));

  vkFreeCommandBuffers(device->logical_device, command_pool, 1,
                       &command_buffer);
}

bool createSemaphore(VulkanDevice *device, VkSemaphore *out_semaphore) {
  VkSemaphoreCreateInfo semaphore_create_info = {};
  semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  semaphore_create_info.pNext = 0;
  semaphore_create_info.flags = 0;

  VK_CHECK(vkCreateSemaphore(device->logical_device, &semaphore_create_info, 0,
                             out_semaphore));

  return true;
}

bool createFence(VulkanDevice *device, VkFence *out_fence) {
  VkFenceCreateInfo fence_create_info = {};
  fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_create_info.pNext = 0;
  fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  VK_CHECK(
      vkCreateFence(device->logical_device, &fence_create_info, 0, out_fence));

  return true;
}