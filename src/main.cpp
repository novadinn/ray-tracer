#include "logger.h"
#include "platform.h"

#include "glm/glm.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <assert.h>
#include <set>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <vulkan/vulkan.h>

struct VulkanSwapchain {
  VkSwapchainKHR handle;
  uint32_t max_frames_in_flight;
  std::vector<VkImage> images;
  std::vector<VkImageView> image_views;
  VkSurfaceFormatKHR surface_format;
};

struct VulkanDevice {
  VkPhysicalDevice physical_device;
  VkDevice logical_device;

  VkPhysicalDeviceProperties properties;
  VkPhysicalDeviceFeatures features;
  VkPhysicalDeviceMemoryProperties memory;

  uint32_t graphics_family_index = 0;
  uint32_t present_family_index = 0;
  uint32_t compute_family_index = 0;
  uint32_t transfer_family_index = 0;
};

#define VK_CHECK(result)                                                       \
  { assert(result == VK_SUCCESS); }

#if PLATFORM_APPLE == 1
#define VK_ENABLE_BETA_EXTENSIONS
#endif

VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_types,
    const VkDebugUtilsMessengerCallbackDataEXT *callback_data, void *user_data);
bool requiredLayersAvailable(std::vector<const char *> required_layers);
bool requiredExtensionsAvailable(std::vector<const char *> required_extensions);
bool createDebugMessanger(VkInstance instance,
                          VkDebugUtilsMessengerEXT *out_debug_messenger);

bool createInstance(VkApplicationInfo application_info, SDL_Window *window,
                    VkInstance *out_instance);
bool createSurface(SDL_Window *window, VkInstance instance,
                   VkSurfaceKHR *out_surface);
bool deviceExtensionsAvailable(VkPhysicalDevice physical_device,
                               std::vector<const char *> required_extensions);
bool createDevice(VkInstance instance, VkSurfaceKHR surface,
                  VulkanDevice *out_device);
void destroyDevice(VulkanDevice *device);
bool createSwapchain(VulkanDevice *device, VkSurfaceKHR surface, uint32_t width,
                     uint32_t height, VulkanSwapchain *out_swapchain);
void destroySwapchain(VulkanSwapchain *swapchain, VulkanDevice *device);

int main(int argc, char **argv) {
  SDL_Window *window;
  if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
    FATAL("Failed to initialize SDL!");
    exit(1);
  }

  window = SDL_CreateWindow("Ray tracer", SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED, 800, 600,
                            SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  if (!window) {
    FATAL("Failed to create a window!");
    exit(1);
  }

  uint8_t image_index = 0;
  uint8_t current_frame = 0;

  VkApplicationInfo application_info = {};
  application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  application_info.pNext = nullptr;
  application_info.pApplicationName = "Ray tracer";
  application_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  application_info.pEngineName = "Ray tracer";
  application_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  application_info.apiVersion = VK_API_VERSION_1_3;

#ifndef PLATFORM_APPLE
  setenv("MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS", "0", 1);
#endif

  VkInstance instance;
  if (!createInstance(application_info, window, &instance)) {
    ERROR("Failed to create vulkan instance!");
    exit(1);
  }

#ifndef NDEBUG
  VkDebugUtilsMessengerEXT debug_messenger;
  if (!createDebugMessanger(instance, &debug_messenger)) {
    ERROR("Failed to create vulkan debug messenger!");
  }
#endif

  VkSurfaceKHR surface;
  if (!createSurface(window, instance, &surface)) {
    FATAL("Failed to create vulkan surface!");
    exit(1);
  }

  VulkanDevice device;
  if (!createDevice(instance, surface, &device)) {
    FATAL("Failed to create vulkan device!");
    exit(1);
  }

  VkQueue graphics_queue;
  vkGetDeviceQueue(device.logical_device, device.graphics_family_index, 0,
                   &graphics_queue);
  VkQueue present_queue;
  vkGetDeviceQueue(device.logical_device, device.present_family_index, 0,
                   &present_queue);
  VkQueue compute_queue;
  vkGetDeviceQueue(device.logical_device, device.compute_family_index, 0,
                   &compute_queue);
  VkQueue transfer_queue;
  vkGetDeviceQueue(device.logical_device, device.transfer_family_index, 0,
                   &transfer_queue);

  VulkanSwapchain swapchain;
  if (!createSwapchain(&device, surface, 800, 600, &swapchain)) {
    FATAL("Failed to create a swapchain!");
    return false;
  }

  bool running = true;
  while (running) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_WINDOWEVENT: {
        if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
          running = false;
        } else if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
          int width, height;
          SDL_GetWindowSize(window, &width, &height);
        }
      } break;
      case SDL_QUIT: {
        running = false;
      } break;
      }
    }
  }

  destroySwapchain(&swapchain, &device);
  destroyDevice(&device);
  vkDestroySurfaceKHR(instance, surface, 0);
#ifndef NDEBUG
  PFN_vkDestroyDebugUtilsMessengerEXT func =
      (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
          instance, "vkDestroyDebugUtilsMessengerEXT");
  func(instance, debug_messenger, 0);
#endif
  vkDestroyInstance(instance, 0);

  return 0;
}

bool createInstance(VkApplicationInfo application_info, SDL_Window *window,
                    VkInstance *out_instance) {
  std::vector<const char *> required_layers;
#ifndef NDEBUG
  required_layers.emplace_back("VK_LAYER_KHRONOS_validation");
#endif
  if (!requiredLayersAvailable(required_layers)) {
    return false;
  }

  uint32_t required_extensions_count = 0;
  std::vector<const char *> required_extensions;
  SDL_Vulkan_GetInstanceExtensions(window, &required_extensions_count,
                                   required_extensions.data());
  required_extensions.resize(required_extensions_count);
  if (!SDL_Vulkan_GetInstanceExtensions(window, &required_extensions_count,
                                        required_extensions.data())) {
    ERROR("Failed to get SDL Vulkan extensions!");
    return false;
  }
#ifndef NDEBUG
  required_extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
#ifdef PLATFORM_APPLE
  required_extensions.emplace_back(
      VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

  if (!requiredExtensionsAvailable(required_extensions)) {
    return false;
  }

  VkInstanceCreateInfo instance_create_info = {};
  instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_create_info.pNext = 0;
#if PLATFORM_APPLE == 1
  instance_create_info.flags |=
      VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
  instance_create_info.pApplicationInfo = &application_info;
  instance_create_info.enabledLayerCount = required_layers.size();
  instance_create_info.ppEnabledLayerNames = required_layers.data();
  instance_create_info.enabledExtensionCount = required_extensions.size();
  instance_create_info.ppEnabledExtensionNames = required_extensions.data();

  VK_CHECK(vkCreateInstance(&instance_create_info, 0, out_instance));

  return true;
}

bool requiredLayersAvailable(std::vector<const char *> required_layers) {
  uint32_t available_layer_count = 0;
  std::vector<VkLayerProperties> available_layers;

  VK_CHECK(vkEnumerateInstanceLayerProperties(&available_layer_count, 0));
  available_layers.resize(available_layer_count);
  VK_CHECK(vkEnumerateInstanceLayerProperties(&available_layer_count,
                                              &available_layers[0]));

  for (uint32_t i = 0; i < required_layers.size(); ++i) {
    bool found = false;
    for (uint32_t j = 0; j < available_layer_count; ++j) {
      if (strcmp(required_layers[i], available_layers[j].layerName) == 0) {
        found = true;
        break;
      }
    }

    if (!found) {
      ERROR("Required validation layer is missing: %s.", required_layers[i]);
      return false;
    }
  }

  return true;
}

bool requiredExtensionsAvailable(
    std::vector<const char *> required_extensions) {
  uint32_t available_extension_count = 0;
  std::vector<VkExtensionProperties> available_extensions;

  VK_CHECK(
      vkEnumerateInstanceExtensionProperties(0, &available_extension_count, 0));
  available_extensions.resize(available_extension_count);
  VK_CHECK(vkEnumerateInstanceExtensionProperties(0, &available_extension_count,
                                                  &available_extensions[0]));

  for (uint32_t i = 0; i < required_extensions.size(); ++i) {
    bool found = false;
    for (uint32_t j = 0; j < available_extension_count; ++j) {
      if (strcmp(required_extensions[i],
                 available_extensions[j].extensionName) == 0) {
        found = true;
        break;
      }
    }

    if (!found) {
      DEBUG("Required extension is missing: %s.", required_extensions[i]);
      return false;
    }
  }

  return true;
}

VKAPI_ATTR VkBool32 VKAPI_CALL
vulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                    VkDebugUtilsMessageTypeFlagsEXT message_types,
                    const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
                    void *user_data) {
  switch (message_severity) {
  default:
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: {
    ERROR(callback_data->pMessage);
  } break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: {
    WARN(callback_data->pMessage);
  } break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: {
    INFO(callback_data->pMessage);
  } break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: {
    TRACE(callback_data->pMessage);
  } break;
  }

  return VK_FALSE;
}

bool createDebugMessanger(VkInstance instance,
                          VkDebugUtilsMessengerEXT *out_debug_messenger) {
  uint32_t log_severity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;

  VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {};
  debug_create_info.sType =
      VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  debug_create_info.pNext = 0;
  debug_create_info.flags = 0;
  debug_create_info.messageSeverity = log_severity;
  debug_create_info.messageType =
      VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
  debug_create_info.pfnUserCallback = vulkanDebugCallback;
  debug_create_info.pUserData = 0;

  PFN_vkCreateDebugUtilsMessengerEXT func =
      (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
          instance, "vkCreateDebugUtilsMessengerEXT");
  VK_CHECK(func(instance, &debug_create_info, 0, out_debug_messenger));

  return true;
}

bool createSurface(SDL_Window *window, VkInstance instance,
                   VkSurfaceKHR *out_surface) {
  if (!SDL_Vulkan_CreateSurface(window, instance, out_surface)) {
    return false;
  }

  return true;
}

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
    out_device->graphics_family_index = graphics_family_index;
    out_device->present_family_index = present_family_index;
    out_device->compute_family_index = compute_family_index;
    out_device->transfer_family_index = transfer_family_index;

    break;
  }

  std::vector<uint32_t> queue_indices;
  std::set<uint32_t> unique_queue_indices;
  if (!unique_queue_indices.contains(out_device->graphics_family_index)) {
    queue_indices.emplace_back(out_device->graphics_family_index);
  }
  unique_queue_indices.emplace(out_device->graphics_family_index);

  if (!unique_queue_indices.contains(out_device->present_family_index)) {
    queue_indices.emplace_back(out_device->present_family_index);
  }
  unique_queue_indices.emplace(out_device->present_family_index);

  if (!unique_queue_indices.contains(out_device->compute_family_index)) {
    queue_indices.emplace_back(out_device->compute_family_index);
  }
  unique_queue_indices.emplace(out_device->compute_family_index);

  if (!unique_queue_indices.contains(out_device->transfer_family_index)) {
    queue_indices.emplace_back(out_device->transfer_family_index);
  }
  unique_queue_indices.emplace(out_device->transfer_family_index);

  std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
  for (int i = 0; i < queue_indices.size(); ++i) {
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
  if (device->graphics_family_index != device->present_family_index) {
    uint32_t indices[] = {device->graphics_family_index,
                          device->present_family_index};
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
  for (int i = 0; i < swapchain->image_views.size(); ++i) {
    vkDestroyImageView(device->logical_device, swapchain->image_views[i], 0);
  }

  vkDestroySwapchainKHR(device->logical_device, swapchain->handle, 0);
}