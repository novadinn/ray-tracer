#include "vulkan_swapchain.h"

#include "logger.h"
#include "vulkan_common.h"

#include "glm/glm.hpp"

bool createSwapchain(VulkanDevice *device, VkSurfaceKHR surface, uint32_t width,
                     uint32_t height, VulkanSwapchain *out_swapchain) {
  uint32_t format_count = 0;
  VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(device->physical_device,
                                                surface, &format_count, 0));
  std::vector<VkSurfaceFormatKHR> surface_formats;
  if (format_count != 0) {
    surface_formats.resize(format_count);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(device->physical_device,
                                                  surface, &format_count,
                                                  surface_formats.data()));
  } else {
    ERROR("Failed to get any supported swapchain image formats!");
    return false;
  }

  VkSurfaceFormatKHR image_format = surface_formats[0];
  for (uint32_t i = 0; i < surface_formats.size(); ++i) {
    VkSurfaceFormatKHR format = surface_formats[i];
    if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
        format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      image_format = format;
      break;
    }
  }

  uint32_t present_mode_count = 0;
  VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(
      device->physical_device, surface, &present_mode_count, 0));
  std::vector<VkPresentModeKHR> present_modes;
  if (present_mode_count != 0) {
    present_modes.resize(present_mode_count);
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(
        device->physical_device, surface, &present_mode_count,
        present_modes.data()));
  } else {
    ERROR("Failed to get any supported swapchain present modes!");
    return false;
  }

  VkPresentModeKHR present_mode = present_modes[0];
  for (uint32_t i = 0; i < present_modes.size(); ++i) {
    VkPresentModeKHR mode = present_modes[i];
    if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
      present_mode = mode;
      break;
    }
  }

  VkSurfaceCapabilitiesKHR surface_capabilities;
  VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
      device->physical_device, surface, &surface_capabilities));

  VkExtent2D extent = {width, height};
  if (surface_capabilities.currentExtent.width != UINT32_MAX) {
    extent = surface_capabilities.currentExtent;
  }
  extent.width =
      glm::clamp(extent.width, surface_capabilities.minImageExtent.width,
                 surface_capabilities.maxImageExtent.width);
  extent.height =
      glm::clamp(extent.height, surface_capabilities.minImageExtent.height,
                 surface_capabilities.maxImageExtent.height);

  uint32_t image_count = surface_capabilities.minImageCount + 1;
  if (surface_capabilities.maxImageCount > 0 &&
      image_count > surface_capabilities.maxImageCount) {
    image_count = surface_capabilities.maxImageCount;
  }

  uint32_t max_frames_in_flight = image_count - 1;

  VkSwapchainCreateInfoKHR swapchain_create_info = {};
  swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchain_create_info.pNext = 0;
  swapchain_create_info.flags = 0;
  swapchain_create_info.surface = surface;
  swapchain_create_info.minImageCount = image_count;
  swapchain_create_info.imageFormat = image_format.format;
  swapchain_create_info.imageColorSpace = image_format.colorSpace;
  swapchain_create_info.imageExtent = extent;
  swapchain_create_info.imageArrayLayers = 1;
  swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  if (device->queue_family_indices[VULKAN_DEVICE_QUEUE_TYPE_GRAPHICS] !=
      device->queue_family_indices[VULKAN_DEVICE_QUEUE_TYPE_PRESENT]) {
    uint32_t indices[] = {
        device->queue_family_indices[VULKAN_DEVICE_QUEUE_TYPE_GRAPHICS],
        device->queue_family_indices[VULKAN_DEVICE_QUEUE_TYPE_PRESENT]};
    swapchain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    swapchain_create_info.queueFamilyIndexCount = 2;
    swapchain_create_info.pQueueFamilyIndices = indices;
  } else {
    swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_create_info.queueFamilyIndexCount = 0;
    swapchain_create_info.pQueueFamilyIndices = 0;
  }
  swapchain_create_info.preTransform = surface_capabilities.currentTransform;
  swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swapchain_create_info.presentMode = present_mode;
  swapchain_create_info.clipped = VK_TRUE;
  swapchain_create_info.oldSwapchain = 0;

  VK_CHECK(vkCreateSwapchainKHR(device->logical_device, &swapchain_create_info,
                                0, &out_swapchain->handle));

  std::vector<VkImage> images;
  std::vector<VkImageView> image_views;

  uint32_t swapchain_image_count = 0;
  VK_CHECK(vkGetSwapchainImagesKHR(device->logical_device,
                                   out_swapchain->handle,
                                   &swapchain_image_count, 0));
  images.resize(swapchain_image_count);
  image_views.resize(swapchain_image_count);
  VK_CHECK(vkGetSwapchainImagesKHR(device->logical_device,
                                   out_swapchain->handle,
                                   &swapchain_image_count, images.data()));

  for (uint32_t i = 0; i < swapchain_image_count; ++i) {
    VkImage image = images[i];

    VkImageViewCreateInfo view_info = {
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = image_format.format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(device->logical_device, &view_info, 0,
                               &image_views[i]));
  }

  /* TODO: depth attachment? */

  out_swapchain->images = images;
  out_swapchain->image_views = image_views;
  out_swapchain->max_frames_in_flight = max_frames_in_flight;
  out_swapchain->surface_format = image_format;

  return true;
}

void destroySwapchain(VulkanSwapchain *swapchain, VulkanDevice *device) {
  for (uint32_t i = 0; i < swapchain->image_views.size(); ++i) {
    vkDestroyImageView(device->logical_device, swapchain->image_views[i], 0);
  }

  vkDestroySwapchainKHR(device->logical_device, swapchain->handle, 0);
}
