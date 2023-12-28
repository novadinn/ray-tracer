#include "logger.h"
#include "platform.h"
#include "vulkan_buffer.h"
#include "vulkan_common.h"
#include "vulkan_device.h"
#include "vulkan_pipeline.h"
#include "vulkan_resources.h"
#include "vulkan_swapchain.h"
#include "vulkan_texture.h"

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
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

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
bool createVmaAllocator(VulkanDevice *device, VkInstance instance,
                        uint32_t api_version, VmaAllocator *out_vma_allocator);
VkDescriptorSetLayoutBinding
descriptorSetLayoutBinding(uint32_t binding, VkDescriptorType descriptor_type,
                           VkShaderStageFlags shader_stage_flags);
VkPipelineShaderStageCreateInfo
pipelineShaderStageCreateInfo(VkShaderStageFlagBits stage_flag,
                              VkShaderModule shader_module);
void writeDescriptorSet(VulkanDevice *device, VkDescriptorSet descriptor_set,
                        uint32_t binding, VkDescriptorType descriptor_type,
                        VkDescriptorImageInfo *image_info,
                        VkDescriptorBufferInfo *buffer_info);
bool loadTexture(const char *path, VulkanDevice *device,
                 VmaAllocator vma_allocator, VkQueue queue,
                 VkCommandPool command_pool, uint32_t queue_family_index,
                 VulkanTexture *out_texture);

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

  VmaAllocator vma_allocator;
  if (!createVmaAllocator(&device, instance, application_info.apiVersion,
                          &vma_allocator)) {
    FATAL("Failed to create a vma allocator!");
    exit(1);
  }

  const uint32_t graphics_family_index =
      device.queue_family_indices[VULKAN_DEVICE_QUEUE_TYPE_GRAPHICS];
  const uint32_t compute_family_index =
      device.queue_family_indices[VULKAN_DEVICE_QUEUE_TYPE_COMPUTE];
  const uint32_t present_family_index =
      device.queue_family_indices[VULKAN_DEVICE_QUEUE_TYPE_PRESENT];

  VkQueue graphics_queue;
  vkGetDeviceQueue(device.logical_device, graphics_family_index, 0,
                   &graphics_queue);
  VkQueue compute_queue;
  vkGetDeviceQueue(device.logical_device, compute_family_index, 0,
                   &compute_queue);
  VkQueue present_queue;
  vkGetDeviceQueue(device.logical_device, present_family_index, 0,
                   &present_queue);

  VkCommandPool graphics_command_pool;
  if (!createCommandPool(&device, graphics_family_index,
                         &graphics_command_pool)) {
    FATAL("Failed to create a command pool!");
    exit(1);
  }
  VkCommandPool compute_command_pool;
  if (!createCommandPool(&device, compute_family_index,
                         &compute_command_pool)) {
    FATAL("Failed to create a command pool!");
    exit(1);
  }

  std::vector<VkCommandBuffer> graphics_command_buffers;
  graphics_command_buffers.resize(swapchain.images.size());
  for (uint32_t i = 0; i < graphics_command_buffers.size(); ++i) {
    if (!allocateCommandBuffer(&device, graphics_command_pool,
                               &graphics_command_buffers[i])) {
      FATAL("Failed to allocate a command buffer!");
      exit(1);
    }
  }
  std::vector<VkCommandBuffer> compute_command_buffers;
  compute_command_buffers.resize(swapchain.images.size());
  for (uint32_t i = 0; i < compute_command_buffers.size(); ++i) {
    if (!allocateCommandBuffer(&device, compute_command_pool,
                               &compute_command_buffers[i])) {
      FATAL("Failed to allocate a command buffer!");
      exit(1);
    }
  }

  std::vector<VkSemaphore> image_available_semaphores;
  image_available_semaphores.resize(swapchain.max_frames_in_flight);
  std::vector<VkSemaphore> render_finished_semaphores;
  render_finished_semaphores.resize(swapchain.max_frames_in_flight);
  std::vector<VkFence> in_flight_fences;
  in_flight_fences.resize(swapchain.max_frames_in_flight);
  std::vector<VkSemaphore> compute_finished_semaphores;
  compute_finished_semaphores.resize(swapchain.max_frames_in_flight);
  std::vector<VkFence> compute_in_flight_fences;
  compute_in_flight_fences.resize(swapchain.max_frames_in_flight);
  for (uint32_t i = 0; i < swapchain.max_frames_in_flight; ++i) {
    if (!createSemaphore(&device, &image_available_semaphores[i])) {
      FATAL("Failed to create a semaphore!");
      exit(1);
    }
    if (!createSemaphore(&device, &render_finished_semaphores[i])) {
      FATAL("Failed to create a semaphore!");
      exit(1);
    }
    if (!createFence(&device, &in_flight_fences[i])) {
      FATAL("Failed to create a fence!");
      exit(1);
    }

    if (!createSemaphore(&device, &compute_finished_semaphores[i])) {
      FATAL("Failed to create a semaphore!");
      exit(1);
    }
    if (!createFence(&device, &compute_in_flight_fences[i])) {
      FATAL("Failed to create a fence!");
      exit(1);
    }
  }

  VkDescriptorPool descriptor_pool;
  if (!createDescriptorPool(&device, &descriptor_pool)) {
    FATAL("Failed to create a descriptor pool!");
    exit(1);
  }

  VkShaderModule texture_vertex_shader_module;
  if (!createShaderModule(&device, "assets/shaders/texture.vert.spv",
                          &texture_vertex_shader_module)) {
    FATAL("Failed to load a shader!");
    exit(1);
  }
  VkShaderModule texture_fragment_shader_module;
  if (!createShaderModule(&device, "assets/shaders/texture.frag.spv",
                          &texture_fragment_shader_module)) {
    FATAL("Failed to load a shader!");
    exit(1);
  }

  VkDescriptorSetLayoutBinding descriptor_set_layout_binding =
      descriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                 VK_SHADER_STAGE_FRAGMENT_BIT);

  VkDescriptorSetLayout descriptor_set_layout;
  if (!createDescriptorSetLayout(&device,
                                 std::vector<VkDescriptorSetLayoutBinding>{
                                     descriptor_set_layout_binding},
                                 &descriptor_set_layout)) {
    FATAL("Failed to create a descriptor set layout!");
    exit(1);
  }

  std::vector<VkPipelineShaderStageCreateInfo> graphics_pipeline_stages;
  graphics_pipeline_stages.emplace_back(pipelineShaderStageCreateInfo(
      VK_SHADER_STAGE_VERTEX_BIT, texture_vertex_shader_module));
  graphics_pipeline_stages.emplace_back(pipelineShaderStageCreateInfo(
      VK_SHADER_STAGE_FRAGMENT_BIT, texture_fragment_shader_module));

  VulkanPipeline graphics_pipeline;
  if (!createGraphicsPipeline(
          &device, render_pass,
          std::vector<VkDescriptorSetLayout>{descriptor_set_layout},
          graphics_pipeline_stages, &graphics_pipeline)) {
    FATAL("Failed to create a graphics pipeline!");
    exit(1);
  }

  vkDestroyShaderModule(device.logical_device, texture_vertex_shader_module, 0);
  vkDestroyShaderModule(device.logical_device, texture_fragment_shader_module,
                        0);

  VkShaderModule compute_shader_module;
  if (!createShaderModule(&device, "assets/shaders/ray_tracing.comp.spv",
                          &compute_shader_module)) {
    FATAL("Failed to create a compute shader module!");
    exit(1);
  }

  VkDescriptorSetLayoutBinding compute_descriptor_set_layout_binding =
      descriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                 VK_SHADER_STAGE_COMPUTE_BIT);

  VkDescriptorSetLayout compute_descriptor_set_layout;
  if (!createDescriptorSetLayout(&device,
                                 std::vector<VkDescriptorSetLayoutBinding>{
                                     compute_descriptor_set_layout_binding},
                                 &compute_descriptor_set_layout)) {
    FATAL("Failed to create a descriptor set layout!");
    exit(1);
  }

  VkPipelineShaderStageCreateInfo compute_stage_create_info =
      pipelineShaderStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT,
                                    compute_shader_module);

  VulkanPipeline compute_pipeline;
  if (!createComputePipeline(
          &device,
          std::vector<VkDescriptorSetLayout>{compute_descriptor_set_layout},
          compute_stage_create_info, &compute_pipeline)) {
    FATAL("Failed to create a compute pipeline!");
    exit(1);
  }

  vkDestroyShaderModule(device.logical_device, compute_shader_module, 0);

  VkDescriptorSet texture_descriptor_set;
  if (!allocateDescriptorSet(&device, descriptor_pool, descriptor_set_layout,
                             &texture_descriptor_set)) {
    FATAL("Failed to create a descriptor set!");
    exit(1);
  }

  VulkanTexture texture;
  if (!createTexture(&device, vma_allocator, VK_FORMAT_R8G8B8A8_UNORM, 800, 600,
                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                     &texture)) {
    FATAL("Failed to create a texture!")
    exit(1);
  }
  VkCommandBuffer temp_command_buffer;
  if (!allocateAndBeginSingleUseCommandBuffer(&device, graphics_command_pool,
                                              &temp_command_buffer)) {
    ERROR("Failed to allocate a temp command buffer!");
    exit(1);
  }
  if (!transitionTextureLayout(
          &texture, temp_command_buffer, VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_GENERAL, graphics_family_index)) {
    ERROR("Failed to transition image layout!");
    exit(1);
  }
  endAndFreeSingleUseCommandBuffer(temp_command_buffer, &device,
                                   graphics_command_pool, graphics_queue);

  VkDescriptorImageInfo descriptor_image_info = {};
  descriptor_image_info.sampler = texture.sampler;
  descriptor_image_info.imageView = texture.view;
  descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  writeDescriptorSet(&device, texture_descriptor_set, 0,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                     &descriptor_image_info, 0);

  VkDescriptorSet compute_texture_descriptor_set;
  if (!allocateDescriptorSet(&device, descriptor_pool,
                             compute_descriptor_set_layout,
                             &compute_texture_descriptor_set)) {
    FATAL("Failed to create a descriptor set!");
    exit(1);
  }

  VkDescriptorImageInfo compute_descriptor_image_info = {};
  compute_descriptor_image_info.sampler = texture.sampler;
  compute_descriptor_image_info.imageView = texture.view;
  compute_descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  writeDescriptorSet(&device, compute_texture_descriptor_set, 0,
                     VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                     &compute_descriptor_image_info, 0);

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

    vkWaitForFences(device.logical_device, 1,
                    &compute_in_flight_fences[current_frame], VK_TRUE,
                    UINT64_MAX);
    vkResetFences(device.logical_device, 1,
                  &compute_in_flight_fences[current_frame]);

    VkCommandBuffer compute_command_buffer =
        compute_command_buffers[current_frame];
    beginCommandBuffer(compute_command_buffer, 0);

    vkCmdBindPipeline(compute_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                      compute_pipeline.handle);
    vkCmdBindDescriptorSets(
        compute_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        compute_pipeline.layout, 0, 1, &compute_texture_descriptor_set, 0, 0);
    vkCmdDispatch(compute_command_buffer, texture.width / 16,
                  texture.height / 16, 1);

    vkEndCommandBuffer(compute_command_buffer);

    VkPipelineStageFlags wait_dst_stage_mask =
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

    VkSubmitInfo compute_submit_info = {};
    compute_submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    compute_submit_info.pNext = 0;
    compute_submit_info.waitSemaphoreCount = 0;
    compute_submit_info.pWaitSemaphores = 0;
    compute_submit_info.pWaitDstStageMask = &wait_dst_stage_mask;
    compute_submit_info.commandBufferCount = 1;
    compute_submit_info.pCommandBuffers =
        &compute_command_buffers[current_frame];
    compute_submit_info.signalSemaphoreCount = 1;
    compute_submit_info.pSignalSemaphores =
        &compute_finished_semaphores[current_frame];

    VkResult result = vkQueueSubmit(compute_queue, 1, &compute_submit_info,
                                    compute_in_flight_fences[current_frame]);
    if (result != VK_SUCCESS) {
      ERROR("Vulkan queue submit failed.");
    }

    vkWaitForFences(device.logical_device, 1, &in_flight_fences[current_frame],
                    true, UINT64_MAX);
    VK_CHECK(vkResetFences(device.logical_device, 1,
                           &in_flight_fences[current_frame]));

    uint32_t image_index = 0;
    vkAcquireNextImageKHR(device.logical_device, swapchain.handle, UINT64_MAX,
                          image_available_semaphores[current_frame], 0,
                          &image_index);

    VkCommandBuffer graphics_command_buffer =
        graphics_command_buffers[current_frame];
    beginCommandBuffer(graphics_command_buffer, 0);

    VkImageMemoryBarrier image_memory_barrier = {};
    image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_memory_barrier.pNext = 0;
    image_memory_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    image_memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_memory_barrier.image = texture.handle;
    image_memory_barrier.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;
    image_memory_barrier.subresourceRange.baseMipLevel = 0;
    image_memory_barrier.subresourceRange.levelCount = 1;
    image_memory_barrier.subresourceRange.baseArrayLayer = 0;
    image_memory_barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(graphics_command_buffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &image_memory_barrier);

    glm::vec4 clear_color = {0, 0, 0, 1};
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

    vkCmdBindPipeline(graphics_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      graphics_pipeline.handle);
    vkCmdBindDescriptorSets(
        graphics_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        graphics_pipeline.layout, 0, 1, &texture_descriptor_set, 0, 0);

    vkCmdDraw(graphics_command_buffer, 4, 1, 0, 0);

    vkCmdEndRenderPass(graphics_command_buffer);

    VK_CHECK(vkEndCommandBuffer(graphics_command_buffer));

    VkPipelineStageFlags wait_dst_stage_masks[2] = {
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    std::vector<VkSemaphore> wait_semaphores = {
        compute_finished_semaphores[current_frame],
        image_available_semaphores[current_frame]};

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = 0;
    submit_info.waitSemaphoreCount = wait_semaphores.size();
    submit_info.pWaitSemaphores = wait_semaphores.data();
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &graphics_command_buffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &render_finished_semaphores[current_frame];
    submit_info.pWaitDstStageMask = wait_dst_stage_masks;

    result = vkQueueSubmit(graphics_queue, 1, &submit_info,
                           in_flight_fences[current_frame]);
    if (result != VK_SUCCESS) {
      ERROR("Vulkan queue submit failed.");
    }

    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pNext = 0;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_finished_semaphores[current_frame];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain.handle;
    present_info.pImageIndices = &image_index;
    present_info.pResults = 0;

    result = vkQueuePresentKHR(present_queue, &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
      /* TODO: recreate a swapchain */
    } else if (result != VK_SUCCESS) {
      ERROR("Failed to present a swapchain image!");
    }

    current_frame = (current_frame + 1) % swapchain.max_frames_in_flight;
  }

  vkDeviceWaitIdle(device.logical_device);

  vkDestroyDescriptorSetLayout(device.logical_device,
                               compute_descriptor_set_layout, 0);

  destroyPipeline(&compute_pipeline, &device);

  destroyTexture(&texture, &device, vma_allocator);

  vkDestroyDescriptorPool(device.logical_device, descriptor_pool, 0);

  destroyPipeline(&graphics_pipeline, &device);

  vkDestroyDescriptorSetLayout(device.logical_device, descriptor_set_layout, 0);

  vkFreeCommandBuffers(device.logical_device, graphics_command_pool,
                       graphics_command_buffers.size(),
                       graphics_command_buffers.data());
  vkFreeCommandBuffers(device.logical_device, compute_command_pool,
                       compute_command_buffers.size(),
                       compute_command_buffers.data());
  vkDestroyCommandPool(device.logical_device, graphics_command_pool, 0);
  vkDestroyCommandPool(device.logical_device, compute_command_pool, 0);

  for (uint32_t i = 0; i < swapchain.max_frames_in_flight; ++i) {
    vkDestroySemaphore(device.logical_device, image_available_semaphores[i], 0);
    vkDestroySemaphore(device.logical_device, render_finished_semaphores[i], 0);
    vkDestroyFence(device.logical_device, in_flight_fences[i], 0);
    vkDestroySemaphore(device.logical_device, compute_finished_semaphores[i],
                       0);
    vkDestroyFence(device.logical_device, compute_in_flight_fences[i], 0);
  }
  for (uint32_t i = 0; i < framebuffers.size(); ++i) {
    vkDestroyFramebuffer(device.logical_device, framebuffers[i], 0);
  }
  vkDestroyRenderPass(device.logical_device, render_pass, 0);
  destroySwapchain(&swapchain, &device);
  vmaDestroyAllocator(vma_allocator);
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

bool createVmaAllocator(VulkanDevice *device, VkInstance instance,
                        uint32_t api_version, VmaAllocator *out_vma_allocator) {
  VmaAllocatorCreateInfo vma_allocator_create_info = {};
  vma_allocator_create_info.flags = 0;
  vma_allocator_create_info.physicalDevice = device->physical_device;
  vma_allocator_create_info.device = device->logical_device;
  /* vma_allocator_create_info.preferredLargeHeapBlockSize; */
  vma_allocator_create_info.pAllocationCallbacks = 0;
  /* vma_allocator_create_info.pDeviceMemoryCallbacks; */
  /* vma_allocator_create_info.pHeapSizeLimit; */
  /* vma_allocator_create_info.pVulkanFunctions; */
  vma_allocator_create_info.instance = instance;
  vma_allocator_create_info.vulkanApiVersion = api_version;
  VK_CHECK(vmaCreateAllocator(&vma_allocator_create_info, out_vma_allocator));

  return true;
}

VkDescriptorSetLayoutBinding
descriptorSetLayoutBinding(uint32_t binding, VkDescriptorType descriptor_type,
                           VkShaderStageFlags shader_stage_flags) {
  VkDescriptorSetLayoutBinding descriptor_set_layout_binding = {};
  descriptor_set_layout_binding.binding = binding;
  descriptor_set_layout_binding.descriptorType = descriptor_type;
  descriptor_set_layout_binding.descriptorCount = 1;
  descriptor_set_layout_binding.stageFlags = shader_stage_flags;
  descriptor_set_layout_binding.pImmutableSamplers = 0;

  return descriptor_set_layout_binding;
}

VkPipelineShaderStageCreateInfo
pipelineShaderStageCreateInfo(VkShaderStageFlagBits stage_flag,
                              VkShaderModule shader_module) {
  VkPipelineShaderStageCreateInfo pipeline_shader_stage_create_info = {};
  pipeline_shader_stage_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  pipeline_shader_stage_create_info.pNext = 0;
  pipeline_shader_stage_create_info.flags = 0;
  pipeline_shader_stage_create_info.stage = stage_flag;
  pipeline_shader_stage_create_info.module = shader_module;
  pipeline_shader_stage_create_info.pName = "main";
  pipeline_shader_stage_create_info.pSpecializationInfo = 0;

  return pipeline_shader_stage_create_info;
}

void writeDescriptorSet(VulkanDevice *device, VkDescriptorSet descriptor_set,
                        uint32_t binding, VkDescriptorType descriptor_type,
                        VkDescriptorImageInfo *image_info,
                        VkDescriptorBufferInfo *buffer_info) {
  VkWriteDescriptorSet write_descriptor_set = {};
  write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write_descriptor_set.pNext = 0;
  write_descriptor_set.dstSet = descriptor_set;
  write_descriptor_set.dstBinding = binding;
  write_descriptor_set.dstArrayElement = 0;
  write_descriptor_set.descriptorCount = 1;
  write_descriptor_set.descriptorType = descriptor_type;
  write_descriptor_set.pImageInfo = image_info;
  write_descriptor_set.pBufferInfo = buffer_info;
  write_descriptor_set.pTexelBufferView = 0;

  vkUpdateDescriptorSets(device->logical_device, 1, &write_descriptor_set, 0,
                         0);
}

bool loadTexture(const char *path, VulkanDevice *device,
                 VmaAllocator vma_allocator, VkQueue queue,
                 VkCommandPool command_pool, uint32_t queue_family_index,
                 VulkanTexture *out_texture) {
  int texture_width, texture_height, texture_num_channels;
  stbi_set_flip_vertically_on_load(true);
  unsigned char *data = stbi_load(path, &texture_width, &texture_height,
                                  &texture_num_channels, STBI_rgb_alpha);
  if (!data) {
    FATAL("Failed to load image at path %s!", path);
    return false;
  }

  createTexture(device, vma_allocator, VK_FORMAT_R8G8B8A8_SRGB, texture_width,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT,
                texture_height, out_texture);
  writeTextureData(out_texture, device, data, vma_allocator, queue,
                   command_pool, queue_family_index);

  stbi_set_flip_vertically_on_load(false);
  stbi_image_free(data);

  return true;
}