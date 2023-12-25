#include "vulkan_device.h"

#include "logger.h"
#include "vulkan_common.h"
#include "platform.h"

#include <set>
#include <string.h>

bool deviceExtensionsAvailable(VkPhysicalDevice physical_device,
                               std::vector<const char *> required_extensions);

bool createDevice(VkInstance instance, VkSurfaceKHR surface,
                  VulkanDevice *out_device) {
  std::vector<VkPhysicalDevice> physical_devices;
  uint32_t physical_device_count = 0;
  VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, 0));
  if (physical_device_count == 0) {
    ERROR("Failed to find GPU with Vulkan support!");
    return false;
  }
  physical_devices.resize(physical_device_count);
  VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count,
                                      physical_devices.data()));

  for (uint32_t i = 0; i < physical_devices.size(); ++i) {
    VkPhysicalDevice current_physical_device = physical_devices[i];

    std::vector<const char *> device_extension_names;
    device_extension_names.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
#ifdef PLATFORM_APPLE
    device_extension_names.emplace_back("VK_KHR_portability_subset");
#endif

    if (!deviceExtensionsAvailable(current_physical_device,
                                   device_extension_names)) {
      return false;
    }

    std::vector<VkQueueFamilyProperties> queue_family_properties;
    uint32_t queue_family_count;
    vkGetPhysicalDeviceQueueFamilyProperties(current_physical_device,
                                             &queue_family_count, 0);
    queue_family_properties.resize(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(current_physical_device,
                                             &queue_family_count,
                                             &queue_family_properties[0]);

    int32_t graphics_family_index = -1;
    int32_t present_family_index = -1;
    int32_t compute_family_index = -1;
    int32_t transfer_family_index = -1;
    for (uint32_t j = 0; j < queue_family_count; ++j) {
      VkQueueFamilyProperties queue_properties = queue_family_properties[j];

      if (queue_properties.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        graphics_family_index = j;

        VkBool32 supports_present = VK_FALSE;
        VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(
            current_physical_device, j, surface, &supports_present));
        if (supports_present) {
          present_family_index = j;
        }
      }

      if (queue_properties.queueFlags & VK_QUEUE_TRANSFER_BIT) {
        transfer_family_index = j;
      }

      if (queue_properties.queueFlags & VK_QUEUE_COMPUTE_BIT) {
        compute_family_index = j;
      }

      /* attempting to find a transfer-only queue (can be used for multithreaded
       * transfer operations) */
      for (uint32_t k = 0; k < queue_family_count; ++k) {
        VkQueueFamilyProperties queue_properties = queue_family_properties[k];

        if ((queue_properties.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            !(queue_properties.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            !(queue_properties.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            !(queue_properties.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            !(queue_properties.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) &&
            !(queue_properties.queueFlags & VK_QUEUE_PROTECTED_BIT) &&
            !(queue_properties.queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) &&
            !(queue_properties.queueFlags & VK_QUEUE_OPTICAL_FLOW_BIT_NV)) {
          transfer_family_index = k;
        }
      }
    }

    if (graphics_family_index == -1 || present_family_index == -1 ||
        transfer_family_index == -1 || compute_family_index == -1) {
      return false;
    }

    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties(current_physical_device, &device_properties);
    VkPhysicalDeviceFeatures device_features;
    vkGetPhysicalDeviceFeatures(current_physical_device, &device_features);
    VkPhysicalDeviceMemoryProperties device_memory;
    vkGetPhysicalDeviceMemoryProperties(current_physical_device,
                                        &device_memory);

    out_device->physical_device = current_physical_device;
    out_device->properties = device_properties;
    out_device->features = device_features;
    out_device->memory = device_memory;
    out_device->queue_family_indices.emplace(VULKAN_DEVICE_QUEUE_TYPE_GRAPHICS,
                                             graphics_family_index);
    out_device->queue_family_indices.emplace(VULKAN_DEVICE_QUEUE_TYPE_PRESENT,
                                             present_family_index);
    out_device->queue_family_indices.emplace(VULKAN_DEVICE_QUEUE_TYPE_COMPUTE,
                                             compute_family_index);
    out_device->queue_family_indices.emplace(VULKAN_DEVICE_QUEUE_TYPE_TRANSFER,
                                             transfer_family_index);

    break;
  }

  std::vector<uint32_t> queue_indices;
  std::set<uint32_t> unique_queue_indices;
  if (!unique_queue_indices.contains(
          out_device
              ->queue_family_indices[VULKAN_DEVICE_QUEUE_TYPE_GRAPHICS])) {
    queue_indices.emplace_back(
        out_device->queue_family_indices[VULKAN_DEVICE_QUEUE_TYPE_GRAPHICS]);
  }
  unique_queue_indices.emplace(
      out_device->queue_family_indices[VULKAN_DEVICE_QUEUE_TYPE_GRAPHICS]);

  if (!unique_queue_indices.contains(
          out_device->queue_family_indices[VULKAN_DEVICE_QUEUE_TYPE_PRESENT])) {
    queue_indices.emplace_back(
        out_device->queue_family_indices[VULKAN_DEVICE_QUEUE_TYPE_PRESENT]);
  }
  unique_queue_indices.emplace(
      out_device->queue_family_indices[VULKAN_DEVICE_QUEUE_TYPE_PRESENT]);

  if (!unique_queue_indices.contains(
          out_device->queue_family_indices[VULKAN_DEVICE_QUEUE_TYPE_COMPUTE])) {
    queue_indices.emplace_back(
        out_device->queue_family_indices[VULKAN_DEVICE_QUEUE_TYPE_COMPUTE]);
  }
  unique_queue_indices.emplace(
      out_device->queue_family_indices[VULKAN_DEVICE_QUEUE_TYPE_COMPUTE]);

  if (!unique_queue_indices.contains(
          out_device
              ->queue_family_indices[VULKAN_DEVICE_QUEUE_TYPE_TRANSFER])) {
    queue_indices.emplace_back(
        out_device->queue_family_indices[VULKAN_DEVICE_QUEUE_TYPE_TRANSFER]);
  }
  unique_queue_indices.emplace(
      out_device->queue_family_indices[VULKAN_DEVICE_QUEUE_TYPE_TRANSFER]);

  std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
  for (uint32_t i = 0; i < queue_indices.size(); ++i) {
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info = {};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.pNext = 0;
    queue_create_info.flags = 0;
    queue_create_info.queueFamilyIndex = queue_indices[i];
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;

    queue_create_infos.emplace_back(queue_create_info);
  }

  std::vector<const char *> required_extension_names;
  required_extension_names.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
#ifdef PLATFORM_APPLE
  required_extension_names.emplace_back("VK_KHR_portability_subset");
#endif

  VkPhysicalDeviceFeatures device_features = {};

  VkDeviceCreateInfo device_create_info = {};
  device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_create_info.pNext = 0;
  device_create_info.flags = 0;
  device_create_info.queueCreateInfoCount = queue_create_infos.size();
  device_create_info.pQueueCreateInfos = queue_create_infos.data();
  device_create_info.enabledLayerCount = 0;   /* deprecated */
  device_create_info.ppEnabledLayerNames = 0; /* deprecated */
  device_create_info.enabledExtensionCount = required_extension_names.size();
  device_create_info.ppEnabledExtensionNames = required_extension_names.data();
  device_create_info.pEnabledFeatures = &device_features;

  VK_CHECK(vkCreateDevice(out_device->physical_device, &device_create_info, 0,
                          &out_device->logical_device));

  return true;
}

void destroyDevice(VulkanDevice *device) {
  vkDestroyDevice(device->logical_device, 0);
}

bool deviceExtensionsAvailable(VkPhysicalDevice physical_device,
                               std::vector<const char *> required_extensions) {
  uint32_t available_extension_count = 0;
  std::vector<VkExtensionProperties> available_extensions;

  VK_CHECK(vkEnumerateDeviceExtensionProperties(physical_device, 0,
                                                &available_extension_count, 0));
  if (available_extension_count != 0) {
    available_extensions.resize(available_extension_count);
    VK_CHECK(vkEnumerateDeviceExtensionProperties(physical_device, 0,
                                                  &available_extension_count,
                                                  &available_extensions[0]));

    for (uint32_t i = 0; i < required_extensions.size(); ++i) {
      bool found = false;
      for (uint32_t j = 0; j < available_extension_count; ++j) {
        if (strcmp(required_extensions[i],
                   available_extensions[j].extensionName)) {
          found = true;
          break;
        }
      }

      if (!found) {
        DEBUG("Required device extension not found: '%s', skipping device.",
              required_extensions[i]);
        return false;
      }
    }
  }

  return true;
}
