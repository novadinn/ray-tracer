#include "logger.h"
#include "platform.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <assert.h>
#include <set>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <vulkan/vulkan.h>

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

  vkDestroyDevice(device.logical_device, 0);
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