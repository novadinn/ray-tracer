#include "vulkan_descriptor_allocator.h"

#include "vulkan_common.h"
#include "logger.h"

static VulkanDescriptorAllocator *descriptor_allocator;

VkDescriptorPool grabDescriptorAllocatorPool(VulkanDevice *device);

bool initializeDescriptorAllocator() {
    descriptor_allocator = new VulkanDescriptorAllocator();
    descriptor_allocator->current_pool = VK_NULL_HANDLE;

    return true;
}

void shutdownDescriptorAllocator(VulkanDevice *device) {
    for(uint32_t i = 0; i < descriptor_allocator->free_pools.size(); ++i) {
        vkDestroyDescriptorPool(device->logical_device, descriptor_allocator->free_pools[i], 0);
    }
    for(uint32_t i = 0; i < descriptor_allocator->used_pools.size(); ++i) {
        vkDestroyDescriptorPool(device->logical_device, descriptor_allocator->used_pools[i], 0);
    }

    delete descriptor_allocator;
}

void resetDescriptorAllocator(VulkanDevice *device) {
    for(uint32_t i = 0; i < descriptor_allocator->used_pools.size(); ++i) {
        vkResetDescriptorPool(device->logical_device, descriptor_allocator->used_pools[i], 0);
    }

    descriptor_allocator->used_pools.clear();
    descriptor_allocator->current_pool = VK_NULL_HANDLE;
}

bool allocateDescriptorSetFromDescriptorAllocator(VulkanDevice *device, VkDescriptorSetLayout layout, VkDescriptorSet *out_descriptor_set) {
  if (descriptor_allocator->current_pool == VK_NULL_HANDLE) {
    descriptor_allocator->current_pool = grabDescriptorAllocatorPool(device);
    descriptor_allocator->used_pools.emplace_back(descriptor_allocator->current_pool);
  }

  VkDescriptorSetAllocateInfo set_allocate_info = {};
  set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  set_allocate_info.pNext = 0;
  set_allocate_info.descriptorPool = descriptor_allocator->current_pool;
  set_allocate_info.descriptorSetCount = 1;
  set_allocate_info.pSetLayouts = &layout;

  VkResult result = vkAllocateDescriptorSets(
      device->logical_device, &set_allocate_info, out_descriptor_set);
  switch (result) {
  case VK_SUCCESS: {
    return true;
  } break;
  case VK_ERROR_FRAGMENTED_POOL:
  case VK_ERROR_OUT_OF_POOL_MEMORY: {
    /* reallocate */
    descriptor_allocator->current_pool = grabDescriptorAllocatorPool(device);
    descriptor_allocator->used_pools.emplace_back(descriptor_allocator->current_pool);
    VK_CHECK(vkAllocateDescriptorSets(device->logical_device,
                                      &set_allocate_info, out_descriptor_set));
    return true;
  } break;
  default: {
    FATAL("Unrecoverable error encountered while allocating descriptor set!");
    return false;
  } break;
  }

  return false;
}

VkDescriptorPool grabDescriptorAllocatorPool(VulkanDevice *device) {
  if (descriptor_allocator->free_pools.size() > 0) {
    VkDescriptorPool pool = descriptor_allocator->free_pools.back();
    descriptor_allocator->free_pools.pop_back();
    return pool;
  }

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

  VkDescriptorPool pool;
  VK_CHECK(vkCreateDescriptorPool(device->logical_device,
                                  &descriptor_pool_create_info,
                                  0, &pool));

  return pool;
}