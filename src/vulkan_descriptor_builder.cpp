#include "vulkan_descriptor_builder.h"

#include "vulkan_descriptor_allocator.h"
#include "vulkan_descriptor_layout_cache.h"
#include "logger.h"

bool beginDescriptorBuilder(VulkanDescriptorBuilder *out_descriptor_builder) {
    return true;
}

void bindDescriptorBuilderBuffer(uint32_t binding,
                                VkDescriptorBufferInfo *buffer_info,
                                VkDescriptorType type,
                                VkShaderStageFlags stage_flags, 
                                VulkanDescriptorBuilder *out_descriptor_builder) {
  VkDescriptorSetLayoutBinding layout_binding = {};
  layout_binding.binding = binding;
  layout_binding.descriptorType = type;
  layout_binding.descriptorCount = 1;
  layout_binding.stageFlags = stage_flags;
  layout_binding.pImmutableSamplers = nullptr;

  out_descriptor_builder->bindings.emplace_back(layout_binding);

  VkWriteDescriptorSet write_descriptor_set = {};
  write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write_descriptor_set.pNext = nullptr;
  /* write_descriptor_set.dstSet; set later */
  write_descriptor_set.dstBinding = binding;
  write_descriptor_set.dstArrayElement = 0;
  write_descriptor_set.descriptorCount = 1;
  write_descriptor_set.descriptorType = type;
  write_descriptor_set.pImageInfo = 0;
  write_descriptor_set.pBufferInfo = buffer_info;
  write_descriptor_set.pTexelBufferView = 0;

  out_descriptor_builder->writes.emplace_back(write_descriptor_set);
}

void bindDescriptorBuilderImage(uint32_t binding,
                                VkDescriptorImageInfo *image_info,
                                VkDescriptorType type,
                                VkShaderStageFlags stage_flags, 
                                VulkanDescriptorBuilder *out_descriptor_builder) {
  VkDescriptorSetLayoutBinding layout_binding = {};
  layout_binding.binding = binding;
  layout_binding.descriptorType = type;
  layout_binding.descriptorCount = 1;
  layout_binding.stageFlags = stage_flags;
  layout_binding.pImmutableSamplers = nullptr;

  out_descriptor_builder->bindings.emplace_back(layout_binding);

  VkWriteDescriptorSet write_descriptor_set = {};
  write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write_descriptor_set.pNext = nullptr;
  /* write_descriptor_set.dstSet; set later */
  write_descriptor_set.dstBinding = binding;
  write_descriptor_set.dstArrayElement = 0;
  write_descriptor_set.descriptorCount = 1;
  write_descriptor_set.descriptorType = type;
  write_descriptor_set.pImageInfo = image_info;
  write_descriptor_set.pBufferInfo = 0;
  write_descriptor_set.pTexelBufferView = 0;

  out_descriptor_builder->writes.emplace_back(write_descriptor_set);
}

bool endDescriptorBuilder(VulkanDescriptorBuilder *descriptor_builder, VulkanDevice *device, VkDescriptorSet *out_set, VkDescriptorSetLayout *out_layout) {
  VkDescriptorSetLayoutCreateInfo layout_info = {};
  layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_info.pNext = 0;
  layout_info.flags = 0;
  layout_info.pBindings = descriptor_builder->bindings.data();
  layout_info.bindingCount = descriptor_builder->bindings.size();

  *out_layout = createDescriptorLayoutFromCache(device, &layout_info);

  if(!allocateDescriptorSetFromDescriptorAllocator(device, *out_layout, out_set)) {
    ERROR("Failed to allocate a descriptor set!");
    return false;
  }

  for (VkWriteDescriptorSet &w : descriptor_builder->writes) {
    w.dstSet = *out_set;
  }

  vkUpdateDescriptorSets(device->logical_device, descriptor_builder->writes.size(),
                         descriptor_builder->writes.data(), 0, 0);

  return true;
}

bool endDescriptorBuilder(VulkanDescriptorBuilder *descriptor_builder, VulkanDevice *device, VkDescriptorSet *out_set) {
  VkDescriptorSetLayout layout;
  return endDescriptorBuilder(descriptor_builder, device, out_set, &layout);
}