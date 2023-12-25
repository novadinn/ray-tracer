#include "logger.h"
#include "platform.h"
#include "vulkan_common.h"
#include "vulkan_device.h"
#include "vulkan_swapchain.h"

#include "glm/glm.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <assert.h>
#include <set>
#include <stdint.h>
#include <string.h>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

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
bool createRenderPass(VulkanDevice *device, VulkanSwapchain *swapchain,
                      VkRenderPass *out_render_pass);
bool createFramebuffer(VulkanDevice *device, VkRenderPass render_pass,
                       std::vector<VkImageView> attachments, uint32_t width,
                       uint32_t height, VkFramebuffer *out_framebuffer);
bool createCommandPool(VulkanDevice *device, uint32_t queue_family_index,
                       VkCommandPool *out_command_pool);
bool allocateCommandBuffer(VulkanDevice *device, VkCommandPool command_pool,
                           VkCommandBuffer *out_command_buffer);
bool createSemaphore(VulkanDevice *device, VkSemaphore *out_semaphore);
bool createFence(VulkanDevice *device, VkFence *out_fence);

int main(int argc, char **argv) {
  SDL_Window *window;
  if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
    FATAL("Failed to initialize SDL!");
    exit(1);
  }

  const uint32_t window_width = 800;
  const uint32_t window_height = 600;

  window = SDL_CreateWindow(
      "Ray tracer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      window_width, window_height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  if (!window) {
    FATAL("Failed to create a window!");
    exit(1);
  }

  uint32_t image_index = 0;
  uint32_t current_frame = 0;

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

  VulkanSwapchain swapchain;
  if (!createSwapchain(&device, surface, window_width, window_height,
                       &swapchain)) {
    FATAL("Failed to create a swapchain!");
    exit(1);
  }

  VkRenderPass render_pass;
  if (!createRenderPass(&device, &swapchain, &render_pass)) {
    FATAL("Failed to create a render pass!");
    exit(1);
  }

  std::vector<VkFramebuffer> framebuffers;
  framebuffers.resize(swapchain.images.size());
  for (uint32_t i = 0; i < framebuffers.size(); ++i) {
    if (!createFramebuffer(&device, render_pass,
                           std::vector<VkImageView>{swapchain.image_views[i]},
                           window_width, window_height, &framebuffers[i])) {
      FATAL("Failed to create a framebuffer!");
      exit(1);
    }
  }

  std::unordered_map<VulkanDeviceQueueType, VkQueue> queues;
  std::unordered_map<VulkanDeviceQueueType, VkCommandPool> command_pools;
  std::unordered_map<VulkanDeviceQueueType, std::vector<VkCommandBuffer>>
      command_buffers;
  /* TODO: we dont need multiple command pools and command buffers, if family
   * indices are not unique */
  for (auto it = device.queue_family_indices.begin();
       it != device.queue_family_indices.end(); it++) {
    VkQueue queue;
    vkGetDeviceQueue(device.logical_device, it->second, 0, &queue);
    queues.emplace(it->first, queue);

    VkCommandPool command_pool;
    if (!createCommandPool(&device, it->second, &command_pool)) {
      FATAL("Failed to create a command pool!");
      exit(1);
    }
    command_pools.emplace(it->first, command_pool);

    command_buffers.emplace(it->first, std::vector<VkCommandBuffer>{});
    command_buffers[it->first].resize(swapchain.images.size());
    for (uint32_t i = 0; i < command_buffers[it->first].size(); ++i) {
      if (!allocateCommandBuffer(&device, command_pool,
                                 &command_buffers[it->first][i])) {
        FATAL("Failed to allocate a command buffer!");
        exit(1);
      }
    }
  }

  std::vector<VkSemaphore> image_available_semaphores;
  image_available_semaphores.resize(swapchain.max_frames_in_flight);
  std::vector<VkSemaphore> queue_complete_semaphores;
  queue_complete_semaphores.resize(swapchain.max_frames_in_flight);
  std::vector<VkFence> in_flight_fences;
  in_flight_fences.resize(swapchain.max_frames_in_flight);
  for (uint32_t i = 0; i < swapchain.max_frames_in_flight; ++i) {
    if (!createSemaphore(&device, &image_available_semaphores[i])) {
      FATAL("Failed to create a semaphore!");
      exit(1);
    }
    if (!createSemaphore(&device, &queue_complete_semaphores[i])) {
      FATAL("Failed to create a semaphore!");
      exit(1);
    }

    if (!createFence(&device, &in_flight_fences[i])) {
      FATAL("Failed to create a fence!");
      exit(1);
    }
  }

  std::vector<VkFence *> images_in_flight;
  images_in_flight.resize(swapchain.images.size());
  for (uint32_t i = 0; i < images_in_flight.size(); ++i) {
    images_in_flight[i] = 0;
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

    vkDeviceWaitIdle(device.logical_device);
    vkWaitForFences(device.logical_device, 1, &in_flight_fences[current_frame],
                    true, UINT64_MAX);

    vkAcquireNextImageKHR(device.logical_device, swapchain.handle, UINT64_MAX,
                          image_available_semaphores[current_frame], 0,
                          &image_index);

    VkCommandBuffer graphics_command_buffer =
        command_buffers[VULKAN_DEVICE_QUEUE_TYPE_GRAPHICS][image_index];
    VkCommandBufferBeginInfo command_buffer_begin_info = {};
    command_buffer_begin_info.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin_info.pNext = 0;
    command_buffer_begin_info.flags = 0;
    command_buffer_begin_info.pInheritanceInfo = 0;

    VK_CHECK(vkBeginCommandBuffer(graphics_command_buffer,
                                  &command_buffer_begin_info));

    glm::vec4 clear_color = {1, 0, 0, 1};
    VkClearValue clear_value = {};
    clear_value.color.float32[0] = clear_color.r;
    clear_value.color.float32[1] = clear_color.g;
    clear_value.color.float32[2] = clear_color.b;
    clear_value.color.float32[3] = clear_color.a;

    glm::vec4 render_area = {0, 0, window_width, window_height};
    VkRenderPassBeginInfo render_pass_begin_info = {};
    render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin_info.pNext = 0;
    render_pass_begin_info.renderPass = render_pass;
    render_pass_begin_info.framebuffer = framebuffers[image_index];
    render_pass_begin_info.renderArea.offset.x = render_area.x;
    render_pass_begin_info.renderArea.offset.y = render_area.y;
    render_pass_begin_info.renderArea.extent.width = render_area.z;
    render_pass_begin_info.renderArea.extent.height = render_area.w;
    render_pass_begin_info.clearValueCount = 1;
    render_pass_begin_info.pClearValues = &clear_value;

    vkCmdBeginRenderPass(graphics_command_buffer, &render_pass_begin_info,
                         VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport;
    viewport.x = 0.0f;
    viewport.y = render_area.w;
    viewport.width = render_area.z;
    viewport.height = -render_area.w;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vkCmdSetViewport(graphics_command_buffer, 0, 1, &viewport);

    VkRect2D scissor;
    scissor.offset.x = scissor.offset.y = 0;
    scissor.extent.width = render_area.z;
    scissor.extent.height = render_area.w;

    vkCmdSetScissor(graphics_command_buffer, 0, 1, &scissor);

    vkCmdEndRenderPass(graphics_command_buffer);

    VK_CHECK(vkEndCommandBuffer(graphics_command_buffer));
    /* make sure the previous frame is not using this image (its fence is
     * being waited on) */
    if (images_in_flight[image_index] != 0) {
      vkWaitForFences(device.logical_device, 1, images_in_flight[image_index],
                      true, UINT64_MAX);
    }

    /* mark the image fence as in-use by this frame */
    images_in_flight[image_index] = &in_flight_fences[current_frame];
    VK_CHECK(vkResetFences(device.logical_device, 1,
                           &in_flight_fences[current_frame]));

    VkPipelineStageFlags flags[1] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = 0;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &image_available_semaphores[current_frame];
    submit_info.pWaitDstStageMask = 0;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &graphics_command_buffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &queue_complete_semaphores[current_frame];
    submit_info.pWaitDstStageMask = flags;

    VkQueue graphics_queue = queues[VULKAN_DEVICE_QUEUE_TYPE_GRAPHICS];

    VkResult result = vkQueueSubmit(graphics_queue, 1, &submit_info,
                                    in_flight_fences[current_frame]);
    if (result != VK_SUCCESS) {
      ERROR("Vulkan queue submit failed.");
    }

    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pNext = 0;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &queue_complete_semaphores[current_frame];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain.handle;
    present_info.pImageIndices = &image_index;
    present_info.pResults = 0;

    VkQueue present_queue = queues[VULKAN_DEVICE_QUEUE_TYPE_PRESENT];

    result = vkQueuePresentKHR(present_queue, &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
      /* TODO: recreate a swapchain */
    } else if (result != VK_SUCCESS) {
      ERROR("Failed to present a swapchain image!");
    }

    current_frame = (current_frame + 1) % swapchain.max_frames_in_flight;
  }

  vkDeviceWaitIdle(device.logical_device);

  for (auto it = command_buffers.begin(); it != command_buffers.end(); it++) {
    std::vector<VkCommandBuffer> &command_buffers = it->second;
    vkFreeCommandBuffers(device.logical_device, command_pools[it->first],
                         command_buffers.size(), command_buffers.data());
  }
  for (auto it = command_pools.begin(); it != command_pools.end(); it++) {
    vkDestroyCommandPool(device.logical_device, it->second, 0);
  }
  for (uint32_t i = 0; i < swapchain.max_frames_in_flight; ++i) {
    vkDestroySemaphore(device.logical_device, image_available_semaphores[i], 0);
    vkDestroySemaphore(device.logical_device, queue_complete_semaphores[i], 0);
    vkDestroyFence(device.logical_device, in_flight_fences[i], 0);
  }
  for (uint32_t i = 0; i < framebuffers.size(); ++i) {
    vkDestroyFramebuffer(device.logical_device, framebuffers[i], 0);
  }
  vkDestroyRenderPass(device.logical_device, render_pass, 0);
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

bool createRenderPass(VulkanDevice *device, VulkanSwapchain *swapchain,
                      VkRenderPass *out_render_pass) {
  VkAttachmentDescription attachment_description = {};
  attachment_description.flags = 0;
  attachment_description.format = swapchain->surface_format.format;
  attachment_description.samples = VK_SAMPLE_COUNT_1_BIT;
  attachment_description.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachment_description.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachment_description.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment_description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment_description.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachment_description.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference color_attachment_reference = {};
  color_attachment_reference.attachment = 0;
  color_attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass_description = {};
  subpass_description.flags = 0;
  subpass_description.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass_description.inputAttachmentCount = 0;
  subpass_description.pInputAttachments = 0;
  subpass_description.colorAttachmentCount = 1;
  subpass_description.pColorAttachments = &color_attachment_reference;
  subpass_description.pResolveAttachments = 0;
  subpass_description.pDepthStencilAttachment = 0;
  subpass_description.preserveAttachmentCount = 0;
  subpass_description.pPreserveAttachments = 0;

  VkSubpassDependency subpass_dependency = {};
  subpass_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  subpass_dependency.dstSubpass = 0;
  subpass_dependency.srcStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  subpass_dependency.srcAccessMask = 0;
  subpass_dependency.dstStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  subpass_dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  subpass_dependency.dependencyFlags = 0;

  VkRenderPassCreateInfo render_pass_create_info = {};
  render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_create_info.pNext = 0;
  render_pass_create_info.flags = 0;
  render_pass_create_info.attachmentCount = 1;
  render_pass_create_info.pAttachments = &attachment_description;
  render_pass_create_info.subpassCount = 1;
  render_pass_create_info.pSubpasses = &subpass_description;
  render_pass_create_info.dependencyCount = 1;
  render_pass_create_info.pDependencies = &subpass_dependency;

  VK_CHECK(vkCreateRenderPass(device->logical_device, &render_pass_create_info,
                              0, out_render_pass));

  return true;
}

bool createFramebuffer(VulkanDevice *device, VkRenderPass render_pass,
                       std::vector<VkImageView> attachments, uint32_t width,
                       uint32_t height, VkFramebuffer *out_framebuffer) {
  VkFramebufferCreateInfo framebuffer_create_info = {};
  framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebuffer_create_info.pNext = 0;
  framebuffer_create_info.flags = 0;
  framebuffer_create_info.renderPass = render_pass;
  framebuffer_create_info.attachmentCount = attachments.size();
  framebuffer_create_info.pAttachments = attachments.data();
  framebuffer_create_info.width = width;
  framebuffer_create_info.height = height;
  framebuffer_create_info.layers = 1;

  VK_CHECK(vkCreateFramebuffer(device->logical_device, &framebuffer_create_info,
                               0, out_framebuffer));

  return true;
}

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