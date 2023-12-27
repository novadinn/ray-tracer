#include "vulkan_resources.h"

#include "logger.h"
#include "vulkan_common.h"

#include <stdio.h>

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

void beginCommandBuffer(VkCommandBuffer command_buffer,
                        VkCommandBufferUsageFlags usage_flags) {
  VkCommandBufferBeginInfo begin_info = {};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.pNext = 0;
  begin_info.flags = usage_flags;
  begin_info.pInheritanceInfo = 0;

  VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));
}

bool allocateAndBeginSingleUseCommandBuffer(
    VulkanDevice *device, VkCommandPool command_pool,
    VkCommandBuffer *out_command_buffer) {
  if (!allocateCommandBuffer(device, command_pool, out_command_buffer)) {
    ERROR("Failed to allocate a command buffer!");
    return false;
  }
  beginCommandBuffer(*out_command_buffer,
                     VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

  return true;
}

void endAndFreeSingleUseCommandBuffer(VkCommandBuffer command_buffer,
                                      VulkanDevice *device,
                                      VkCommandPool command_pool,
                                      VkQueue queue) {
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

bool createShaderModule(VulkanDevice *device, const char *path,
                        VkShaderModule *out_shader_module) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    ERROR("Failed to open file %s", path);
    return false;
  }

  fseek(file, 0, SEEK_END);
  int64_t file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  std::vector<uint32_t> file_data;
  file_data.resize(file_size);
  fread(&file_data[0], file_size, 1, file);
  fclose(file);

  VkShaderModuleCreateInfo create_info = {};
  create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  create_info.pNext = 0;
  create_info.flags = 0;
  create_info.codeSize = file_size;
  create_info.pCode = &file_data[0];

  VK_CHECK(vkCreateShaderModule(device->logical_device, &create_info, 0,
                                out_shader_module));

  return true;
}

bool createDescriptorSetLayout(
    VulkanDevice *device,
    std::vector<VkDescriptorSetLayoutBinding> descriptor_set_layout_bindings,
    VkDescriptorSetLayout *out_descriptor_set_layout) {
  VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {};
  descriptor_set_layout_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptor_set_layout_create_info.pNext = 0;
  descriptor_set_layout_create_info.flags = 0;
  descriptor_set_layout_create_info.bindingCount =
      descriptor_set_layout_bindings.size();
  descriptor_set_layout_create_info.pBindings =
      descriptor_set_layout_bindings.data();

  VK_CHECK(vkCreateDescriptorSetLayout(device->logical_device,
                                       &descriptor_set_layout_create_info, 0,
                                       out_descriptor_set_layout));

  return true;
}

bool createDescriptorPool(VulkanDevice *device,
                          VkDescriptorPool *out_descriptor_pool) {
  const uint32_t size_count = 1000;
  const std::vector<std::pair<VkDescriptorType, float>> pool_sizes = {
      {VK_DESCRIPTOR_TYPE_SAMPLER, 0.5f},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4.f},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4.f},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.f},
      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1.f},
      {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1.f},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2.f},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2.f},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1.f},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1.f},
      {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 0.5f}};

  std::vector<VkDescriptorPoolSize> sizes;
  sizes.reserve(pool_sizes.size());
  for (auto size : pool_sizes) {
    sizes.push_back({size.first, uint32_t(size.second * size_count)});
  }

  VkDescriptorPoolCreateInfo descriptor_pool_create_info = {};
  descriptor_pool_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptor_pool_create_info.flags = 0;
  descriptor_pool_create_info.maxSets = size_count;
  descriptor_pool_create_info.poolSizeCount = sizes.size();
  descriptor_pool_create_info.pPoolSizes = sizes.data();

  VK_CHECK(vkCreateDescriptorPool(device->logical_device,
                                  &descriptor_pool_create_info, 0,
                                  out_descriptor_pool));

  return true;
}

bool allocateDescriptorSet(VulkanDevice *device,
                           VkDescriptorPool descriptor_pool,
                           VkDescriptorSetLayout layout,
                           VkDescriptorSet *out_descriptor_set) {
  VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {};
  descriptor_set_allocate_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptor_set_allocate_info.pNext = 0;
  descriptor_set_allocate_info.descriptorPool = descriptor_pool;
  descriptor_set_allocate_info.descriptorSetCount = 1;
  descriptor_set_allocate_info.pSetLayouts = &layout;

  VK_CHECK(vkAllocateDescriptorSets(device->logical_device,
                                    &descriptor_set_allocate_info,
                                    out_descriptor_set));

  return true;
}