#pragma once

#include "vulkan_device.h"

#include <vulkan/vulkan.h>

bool createCommandPool(VulkanDevice *device, uint32_t queue_family_index,
                       VkCommandPool *out_command_pool);

bool allocateCommandBuffer(VulkanDevice *device, VkCommandPool command_pool,
                           VkCommandBuffer *out_command_buffer);
void beginCommandBuffer(VkCommandBuffer command_buffer,
                        VkCommandBufferUsageFlags usage_flags);
bool allocateAndBeginSingleUseCommandBuffer(
    VulkanDevice *device, VkCommandPool command_pool,
    VkCommandBuffer *out_command_buffer);
void endAndFreeSingleUseCommandBuffer(VkCommandBuffer command_buffer,
                                      VulkanDevice *device,
                                      VkCommandPool command_pool,
                                      VkQueue queue);

bool createSemaphore(VulkanDevice *device, VkSemaphore *out_semaphore);

bool createFence(VulkanDevice *device, VkFence *out_fence);

bool createShaderModule(VulkanDevice *device, const char *path,
                        VkShaderModule *out_shader_module);

bool createDescriptorSetLayout(
    VulkanDevice *device,
    std::vector<VkDescriptorSetLayoutBinding> descriptor_set_layout_bindings,
    VkDescriptorSetLayout *out_descriptor_set_layout);

bool createDescriptorPool(VulkanDevice *device, VkDescriptorPool *out_descriptor_pool);
bool allocateDescriptorSet(VulkanDevice *device, VkDescriptorPool descriptor_pool, VkDescriptorSetLayout layout, VkDescriptorSet *out_descriptor_set);