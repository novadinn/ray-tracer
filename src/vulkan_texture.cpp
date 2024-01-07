#include "vulkan_texture.h"

#include "logger.h"
#include "vulkan_common.h"
#include "vulkan_resources.h"

bool createTexture(VulkanDevice *device, VmaAllocator vma_allocator,
                   VkFormat format, uint32_t width, uint32_t height,
                   VkImageUsageFlags usage_flags, VulkanTexture *out_texture) {
  if (usage_flags & VK_IMAGE_USAGE_STORAGE_BIT) {
    VkFormatProperties format_properties;
    vkGetPhysicalDeviceFormatProperties(device->physical_device, format,
                                        &format_properties);

    if (!format_properties.optimalTilingFeatures &
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) {
      ERROR("Device does not support VK_IMAGE_USAGE_STORAGE_BIT!");
      return false;
    }
  }

  out_texture->format = format;
  out_texture->width = width;
  out_texture->height = height;

  VkImageCreateInfo image_create_info = {};
  image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_create_info.pNext = 0;
  image_create_info.flags = 0;
  image_create_info.imageType = VK_IMAGE_TYPE_2D;
  image_create_info.format = format;
  image_create_info.extent.width = width;
  image_create_info.extent.height = height;
  image_create_info.extent.depth = 1;
  image_create_info.mipLevels = 1;
  image_create_info.arrayLayers = 1;
  image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_create_info.usage = usage_flags;
  image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_create_info.queueFamilyIndexCount = 0;
  image_create_info.pQueueFamilyIndices = 0;
  image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo vma_allocation_create_info = {};
  /* vma_allocation_create_info.flags; */
  vma_allocation_create_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  vma_allocation_create_info.requiredFlags =
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  /* vma_allocation_create_info.preferredFlags;
  vma_allocation_create_info.memoryTypeBits;
  vma_allocation_create_info.pool;
  vma_allocation_create_info.pUserData;
  vma_allocation_create_info.priority; */

  VK_CHECK(vmaCreateImage(vma_allocator, &image_create_info,
                          &vma_allocation_create_info, &out_texture->handle,
                          &out_texture->memory, 0));

  VkImageViewCreateInfo view_create_info = {};
  view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_create_info.pNext = 0;
  view_create_info.flags = 0;
  view_create_info.image = out_texture->handle;
  view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_create_info.format = format;
  /* view_create_info.components; */
  view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view_create_info.subresourceRange.baseMipLevel = 0;
  view_create_info.subresourceRange.levelCount = 1;
  view_create_info.subresourceRange.baseArrayLayer = 0;
  view_create_info.subresourceRange.layerCount = 1;

  VK_CHECK(vkCreateImageView(device->logical_device, &view_create_info, 0,
                             &out_texture->view));

  VkSamplerCreateInfo sampler_create_info = {};
  sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sampler_create_info.pNext = 0;
  sampler_create_info.flags = 0;
  sampler_create_info.magFilter = VK_FILTER_LINEAR;
  sampler_create_info.minFilter = VK_FILTER_LINEAR;
  sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_create_info.mipLodBias = 0.0f;
  sampler_create_info.anisotropyEnable = VK_FALSE;
  sampler_create_info.maxAnisotropy = 1.0f;
  sampler_create_info.compareEnable = VK_FALSE;
  sampler_create_info.compareOp = VK_COMPARE_OP_NEVER;
  sampler_create_info.minLod = 0.0f;
  sampler_create_info.maxLod = (float)1;
  sampler_create_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  sampler_create_info.unnormalizedCoordinates = VK_FALSE;

  VK_CHECK(vkCreateSampler(device->logical_device, &sampler_create_info, 0,
                           &out_texture->sampler));

  return true;
}

void destroyTexture(VulkanTexture *texture, VulkanDevice *device,
                    VmaAllocator vma_allocator) {
  vkDestroySampler(device->logical_device, texture->sampler, 0);
  vkDestroyImageView(device->logical_device, texture->view, 0);
  vmaDestroyImage(vma_allocator, texture->handle, texture->memory);
}

bool writeTextureData(VulkanTexture *texture, VulkanDevice *device,
                      void *pixels, VmaAllocator vma_allocator, VkQueue queue,
                      VkCommandPool command_pool, uint32_t queue_family_index) {
  /* TODO: instead of 4, determine the size of the texture via texture->format
   */
  uint32_t size = texture->width * texture->height * 4;

  VulkanBuffer staging_buffer;
  if (!createBuffer(vma_allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    VMA_MEMORY_USAGE_CPU_ONLY, &staging_buffer)) {
    ERROR("Failed to create a staging buffer!");
    return false;
  }

  if (!loadBufferData(&staging_buffer, vma_allocator, pixels)) {
    ERROR("Failed to load buffer data!");
    return false;
  }

  VkCommandBuffer temp_command_buffer;
  if (!allocateAndBeginSingleUseCommandBuffer(device, command_pool,
                                              &temp_command_buffer)) {
    ERROR("Failed to allocate a temp command buffer!");
    return false;
  }

  if (!transitionTextureLayout(
          texture, temp_command_buffer, VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, queue_family_index)) {
    ERROR("Failed to transition image layout!");
    return false;
  }
  if (!copyFromBufferToTexture(texture, &staging_buffer, temp_command_buffer)) {
    ERROR("Failed to copy buffer data to texture!");
    return false;
  }
  if (!transitionTextureLayout(
          texture, temp_command_buffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, queue_family_index)) {
    ERROR("Failed to transition image layout!");
    return false;
  }

  endAndFreeSingleUseCommandBuffer(temp_command_buffer, device, command_pool,
                                   queue);
  destroyBuffer(&staging_buffer, vma_allocator);

  return true;
}

bool transitionTextureLayout(VulkanTexture *texture,
                             VkCommandBuffer command_buffer,
                             VkImageLayout old_layout, VkImageLayout new_layout,
                             uint32_t queue_family_index) {
  VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = queue_family_index;
  barrier.dstQueueFamilyIndex = queue_family_index;
  barrier.image = texture->handle;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  VkPipelineStageFlags source_stage;
  VkPipelineStageFlags dest_stage;

  if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
      new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    dest_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    dest_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
             new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    dest_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
             new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    dest_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
             new_layout == VK_IMAGE_LAYOUT_GENERAL) {
    barrier.srcAccessMask = 0;
    source_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    dest_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  } else if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             new_layout == VK_IMAGE_LAYOUT_GENERAL) {
    barrier.srcAccessMask = 0;
    source_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    dest_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  } else {
    ERROR("Unsupported layout transition!");
    return false;
  }

  vkCmdPipelineBarrier(command_buffer, source_stage, dest_stage, 0, 0, 0, 0, 0,
                       1, &barrier);

  return true;
}

bool copyFromBufferToTexture(VulkanTexture *texture, VulkanBuffer *buffer,
                             VkCommandBuffer command_buffer) {
  /* TODO: instead of 4, determine the size of the texture via texture->format
   */
  uint32_t size = texture->width * texture->height * 4;

  VkBufferImageCopy buffer_image_copy = {};
  buffer_image_copy.bufferOffset = 0;
  buffer_image_copy.bufferRowLength = 0;
  buffer_image_copy.bufferImageHeight = 0;
  buffer_image_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  buffer_image_copy.imageSubresource.mipLevel = 0;
  buffer_image_copy.imageSubresource.baseArrayLayer = 0;
  buffer_image_copy.imageSubresource.layerCount = 1;
  buffer_image_copy.imageExtent.width = texture->width;
  buffer_image_copy.imageExtent.height = texture->height;
  buffer_image_copy.imageExtent.depth = 1;

  vkCmdCopyBufferToImage(command_buffer, buffer->handle, texture->handle,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                         &buffer_image_copy);

  return true;
}