#include "camera.h"
#include "input.h"
#include "logger.h"
#include "platform.h"
#include "vulkan_buffer.h"
#include "vulkan_common.h"
#include "vulkan_descriptor_allocator.h"
#include "vulkan_descriptor_builder.h"
#include "vulkan_descriptor_layout_cache.h"
#include "vulkan_device.h"
#include "vulkan_pipeline.h"
#include "vulkan_resources.h"
#include "vulkan_swapchain.h"
#include "vulkan_texture.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
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
#include "imgui/backends/imgui_impl_sdl2.h"
#include "imgui/backends/imgui_impl_vulkan.h"
#include "imgui/imgui.h"
#include "stb/stb_image.h"

#if PLATFORM_APPLE == 1
#define VK_ENABLE_BETA_EXTENSIONS
#endif

struct UniformBufferObject {
  glm::mat4 view;
  glm::mat4 projection;
  glm::vec4 viewport_size;
  glm::vec4 camera_position;
  glm::vec4 render_settings;
  glm::vec4 frame;
  glm::vec4 ground_colour;
  glm::vec4 sky_colour_horizon;
  glm::vec4 sky_colour_zenith;
  glm::vec4 sun_position;
  float sun_focus;
  float sun_intensity;
  float defocus_strenght;
  float diverge_strength;
};

struct RayTracingMaterial {
  glm::vec4 colour;
  glm::vec4 emission_colour;
  glm::vec4 specular_colour;
};

struct Sphere {
  glm::vec3 position;
  float radius;
  RayTracingMaterial material;
};

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
  const uint32_t window_height = 608;

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

  if (!initializeDescriptorAllocator()) {
    FATAL("Failed to initialize a descriptor allocator!");
    exit(1);
  }
  if (!initializeDescriptorLayoutCache()) {
    FATAL("Failed to inititalize a descriptor layout cache!");
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
  VkDescriptorSetLayoutCreateInfo layout_create_info = {};
  layout_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_create_info.pNext = 0;
  layout_create_info.flags = 0;
  layout_create_info.bindingCount = 1;
  layout_create_info.pBindings = &descriptor_set_layout_binding;
  VkDescriptorSetLayout descriptor_set_layout =
      createDescriptorLayoutFromCache(&device, &layout_create_info);

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
  VkDescriptorSetLayoutCreateInfo compute_layout_create_info = {};
  compute_layout_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  compute_layout_create_info.pNext = 0;
  compute_layout_create_info.flags = 0;
  compute_layout_create_info.bindingCount = 1;
  compute_layout_create_info.pBindings = &compute_descriptor_set_layout_binding;
  VkDescriptorSetLayout compute_descriptor_set_layout =
      createDescriptorLayoutFromCache(&device, &compute_layout_create_info);

  VkDescriptorSetLayoutBinding compute_ubo_descriptor_set_layout_binding =
      descriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                 VK_SHADER_STAGE_COMPUTE_BIT);
  VkDescriptorSetLayoutCreateInfo compute_ubo_layout_create_info = {};
  compute_ubo_layout_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  compute_ubo_layout_create_info.pNext = 0;
  compute_ubo_layout_create_info.flags = 0;
  compute_ubo_layout_create_info.bindingCount = 1;
  compute_ubo_layout_create_info.pBindings =
      &compute_ubo_descriptor_set_layout_binding;

  VkDescriptorSetLayout compute_descriptor_set_layout_ubo =
      createDescriptorLayoutFromCache(&device, &compute_ubo_layout_create_info);

  VkPipelineShaderStageCreateInfo compute_stage_create_info =
      pipelineShaderStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT,
                                    compute_shader_module);

  VkDescriptorSetLayoutBinding compute_ssbo_descriptor_set_layout_binding =
      descriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                 VK_SHADER_STAGE_COMPUTE_BIT);
  VkDescriptorSetLayoutCreateInfo compute_ssbo_layout_create_info = {};
  compute_ssbo_layout_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  compute_ssbo_layout_create_info.pNext = 0;
  compute_ssbo_layout_create_info.flags = 0;
  compute_ssbo_layout_create_info.bindingCount = 1;
  compute_ssbo_layout_create_info.pBindings =
      &compute_ssbo_descriptor_set_layout_binding;

  VkDescriptorSetLayout compute_descriptor_set_layout_ssbo =
      createDescriptorLayoutFromCache(&device,
                                      &compute_ssbo_layout_create_info);

  VulkanPipeline compute_pipeline;
  if (!createComputePipeline(&device,
                             std::vector<VkDescriptorSetLayout>{
                                 compute_descriptor_set_layout,
                                 compute_descriptor_set_layout_ubo,
                                 compute_descriptor_set_layout_ssbo},
                             compute_stage_create_info, &compute_pipeline)) {
    FATAL("Failed to create a compute pipeline!");
    exit(1);
  }

  vkDestroyShaderModule(device.logical_device, compute_shader_module, 0);

  VulkanTexture texture;
  if (!createTexture(
          &device, vma_allocator, VK_FORMAT_R8G8B8A8_UNORM, window_width,
          window_height,
          // VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
          &texture)) {
    FATAL("Failed to create a texture!")
    exit(1);
  }
  void *pixels = malloc(window_width * window_height * 4);
  memset(pixels, 0, window_width * window_height * 4);
  writeTextureData(&texture, &device, pixels, vma_allocator, graphics_queue,
                   graphics_command_pool, graphics_family_index);
  free(pixels);
  VkCommandBuffer temp_command_buffer;
  if (!allocateAndBeginSingleUseCommandBuffer(&device, graphics_command_pool,
                                              &temp_command_buffer)) {
    ERROR("Failed to allocate a temp command buffer!");
    exit(1);
  }
  if (!transitionTextureLayout(&texture, temp_command_buffer,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               VK_IMAGE_LAYOUT_GENERAL,
                               graphics_family_index)) {
    ERROR("Failed to transition image layout!");
    exit(1);
  }

  VkImageMemoryBarrier image_memory_barrier = {};
  image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  image_memory_barrier.pNext = 0;
  image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
  image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  image_memory_barrier.image = texture.handle;
  image_memory_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  image_memory_barrier.subresourceRange.baseMipLevel = 0;
  image_memory_barrier.subresourceRange.levelCount = 1;
  image_memory_barrier.subresourceRange.baseArrayLayer = 0;
  image_memory_barrier.subresourceRange.layerCount = 1;

  if (graphics_family_index != compute_family_index) {
    image_memory_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    image_memory_barrier.dstAccessMask = 0;
    image_memory_barrier.srcQueueFamilyIndex = graphics_family_index;
    image_memory_barrier.dstQueueFamilyIndex = compute_family_index;

    vkCmdPipelineBarrier(temp_command_buffer,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, 0, 0, 0, 1,
                         &image_memory_barrier);
  }

  endAndFreeSingleUseCommandBuffer(temp_command_buffer, &device,
                                   graphics_command_pool, graphics_queue);

  VkDescriptorImageInfo descriptor_image_info = {};
  descriptor_image_info.sampler = texture.sampler;
  descriptor_image_info.imageView = texture.view;
  descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

  VulkanDescriptorBuilder descriptor_builder;

  VkDescriptorSet texture_descriptor_set;
  if (!beginDescriptorBuilder(&descriptor_builder)) {
    FATAL("Failed to create a descriptor set!");
    exit(1);
  }
  bindDescriptorBuilderImage(0, &descriptor_image_info,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                             VK_SHADER_STAGE_FRAGMENT_BIT, &descriptor_builder);
  if (!endDescriptorBuilder(&descriptor_builder, &device,
                            &texture_descriptor_set)) {
    FATAL("Failed to create a descriptor set!");
    exit(1);
  }

  descriptor_builder = {};

  VkDescriptorSet compute_texture_descriptor_set;
  if (!beginDescriptorBuilder(&descriptor_builder)) {
    FATAL("Failed to create a descriptor set!");
    exit(1);
  }
  bindDescriptorBuilderImage(0, &descriptor_image_info,
                             VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                             VK_SHADER_STAGE_COMPUTE_BIT, &descriptor_builder);
  if (!endDescriptorBuilder(&descriptor_builder, &device,
                            &compute_texture_descriptor_set)) {
    FATAL("Failed to create a descriptor set!");
    exit(1);
  }

  VulkanBuffer compute_ubo_buffer;
  if (!createBuffer(vma_allocator, sizeof(UniformBufferObject),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    VMA_MEMORY_USAGE_CPU_TO_GPU, &compute_ubo_buffer)) {
    FATAL("Failed to create a uniform buffer!");
    exit(1);
  }

  descriptor_builder = {};

  VkDescriptorSet compute_ubo_descriptor_set;
  if (!beginDescriptorBuilder(&descriptor_builder)) {
    FATAL("Failed to create a descriptor set!");
    exit(1);
  }
  VkDescriptorBufferInfo descriptor_buffer_info = {};
  descriptor_buffer_info.buffer = compute_ubo_buffer.handle;
  descriptor_buffer_info.offset = 0;
  descriptor_buffer_info.range = compute_ubo_buffer.size;
  bindDescriptorBuilderBuffer(0, &descriptor_buffer_info,
                              VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                              VK_SHADER_STAGE_COMPUTE_BIT, &descriptor_builder);
  if (!endDescriptorBuilder(&descriptor_builder, &device,
                            &compute_ubo_descriptor_set)) {
    FATAL("Failed to create a descriptor set!");
    exit(1);
  }

  std::vector<Sphere> spheres;
  spheres.resize(3);
  int index = 0;
  spheres[index].position = glm::vec3(0, 0, -5);
  spheres[index].radius = 1.0;
  spheres[index].material.colour = glm::vec4(0.5, 0.5, 0.5, 1.0);
  spheres[index].material.emission_colour = glm::vec4(0);
  spheres[index].material.specular_colour = glm::vec4(1.0, 1.0, 1.0, 0.5);

  index = 1;
  spheres[index].position = glm::vec3(3, 0, -5);
  spheres[index].radius = 1.0;
  spheres[index].material.colour = glm::vec4(0.8, 0.2, 0.2, 0.5);
  spheres[index].material.emission_colour = glm::vec4(0);
  spheres[index].material.specular_colour = glm::vec4(1.0, 1.0, 1.0, 0.0);

  index = 2;
  spheres[index].position = glm::vec3(0, -101, -5);
  spheres[index].radius = 100.0;
  spheres[index].material.colour = glm::vec4(0.2, 0.8, 0.05, 0.0);
  spheres[index].material.emission_colour = glm::vec4(0);
  spheres[index].material.specular_colour = glm::vec4(1.0, 1.0, 1.0, 0.0);

  VulkanBuffer compute_ssbo;
  if (!createBuffer(vma_allocator, spheres.size() * sizeof(Sphere),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    VMA_MEMORY_USAGE_GPU_ONLY, &compute_ssbo)) {
    FATAL("Failed to create a SSBO!");
    exit(1);
  }
  if (!loadBufferDataStaging(&compute_ssbo, &device, vma_allocator,
                             spheres.data(), graphics_queue,
                             graphics_command_pool)) {
    FATAL("Failed to load SSBO data!");
    exit(1);
  }

  descriptor_builder = {};

  VkDescriptorSet compute_ssbo_descriptor_set;
  if (!beginDescriptorBuilder(&descriptor_builder)) {
    FATAL("Failed to create a descriptor set!");
    exit(1);
  }
  descriptor_buffer_info = {};
  descriptor_buffer_info.buffer = compute_ssbo.handle;
  descriptor_buffer_info.offset = 0;
  descriptor_buffer_info.range = compute_ssbo.size;
  bindDescriptorBuilderBuffer(0, &descriptor_buffer_info,
                              VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                              VK_SHADER_STAGE_COMPUTE_BIT, &descriptor_builder);
  if (!endDescriptorBuilder(&descriptor_builder, &device,
                            &compute_ssbo_descriptor_set)) {
    FATAL("Failed to create a descriptor set!");
    exit(1);
  }

  VkDescriptorPoolSize pool_sizes[] = {
      {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
      {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

  VkDescriptorPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = 1000;
  pool_info.poolSizeCount = std::size(pool_sizes);
  pool_info.pPoolSizes = pool_sizes;

  VkDescriptorPool imgui_pool;
  VK_CHECK(vkCreateDescriptorPool(device.logical_device, &pool_info, 0,
                                  &imgui_pool));

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;

  ImGui::StyleColorsDark();

  ImGui_ImplSDL2_InitForVulkan(window);

  ImGui_ImplVulkan_InitInfo init_info = {};
  init_info.Instance = instance;
  init_info.PhysicalDevice = device.physical_device;
  init_info.Device = device.logical_device;
  init_info.Queue = graphics_queue;
  init_info.DescriptorPool = imgui_pool;
  init_info.MinImageCount = 3;
  init_info.ImageCount = 3;
  init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

  ImGui_ImplVulkan_Init(&init_info, render_pass);

  ImGui_ImplVulkan_CreateFontsTexture();

  Camera camera;
  createCamera(90, window_width / window_height, 0.01f, 10000.0f, &camera);

  UniformBufferObject ubo = {};
  ubo.render_settings.x = 50;
  ubo.render_settings.y = 25;
  ubo.ground_colour = glm::vec4(0.35, 0.3, 0.35, 1.0);
  ubo.sky_colour_horizon = glm::vec4(1.0);
  ubo.sky_colour_zenith = glm::vec4(0.078, 0.36, 0.72, 1.0);
  ubo.sun_position = glm::normalize(glm::vec4(1.0));
  ubo.sun_focus = 1.0;
  ubo.sun_intensity = 0;
  ubo.defocus_strenght = 0.0;
  ubo.diverge_strength = 1.0;

  bool running = true;
  glm::ivec2 previous_mouse = {0, 0};
  uint32_t last_update_time = SDL_GetTicks();

  while (running) {
    uint32_t start_time_ms = SDL_GetTicks();
    SDL_Event event;
    Input::Begin();

    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);

      switch (event.type) {
      case SDL_KEYDOWN: {
        if (!event.key.repeat) {
          Input::KeyDownEvent(event);
        }
      } break;
      case SDL_KEYUP: {
        Input::KeyUpEvent(event);
      } break;
      case SDL_MOUSEBUTTONDOWN: {
        Input::MouseButtonDownEvent(event);
      } break;
      case SDL_MOUSEBUTTONUP: {
        Input::MouseButtonUpEvent(event);
      } break;
      case SDL_MOUSEWHEEL: {
        Input::WheelEvent(event);
      } break;
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

    float delta_time = 0.01f;
    glm::ivec2 current_mouse;
    Input::GetMousePosition(&current_mouse.x, &current_mouse.y);
    glm::vec2 mouse_delta = current_mouse - previous_mouse;
    mouse_delta *= delta_time;

    glm::ivec2 wheel_movement;
    Input::GetWheelMovement(&wheel_movement.x, &wheel_movement.y);

    bool camera_is_dirty = false;
    if (Input::WasMouseButtonHeld(SDL_BUTTON_MIDDLE)) {
      cameraRotate(&camera, mouse_delta);
      camera_is_dirty = true;
    }

    ubo.view = cameraGetViewMatrix(&camera);
    ubo.projection = cameraGetProjectionMatrix(&camera);
    ubo.viewport_size =
        glm::vec4(camera.viewport_width, camera.viewport_height, 0.0, 0.0);
    ubo.camera_position = glm::vec4(glm::vec3(0.0), 0.0);

    if (!loadBufferData(&compute_ubo_buffer, vma_allocator, &ubo)) {
      FATAL("Failed to load a buffer data!");
      exit(1);
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame(window);
    ImGui::NewFrame();

    if (ImGui::Begin("Render settings")) {
      int samples = ubo.render_settings.x;
      if (ImGui::DragInt("Number of Samples", &samples, 1.0f, 1, INT_MAX)) {
        ubo.render_settings.x = samples;
        camera_is_dirty = true;
      }
      int bounce_count = ubo.render_settings.y;
      if (ImGui::DragInt("Bounce Count", &bounce_count, 1.0f, 1, INT_MAX)) {
        ubo.render_settings.y = bounce_count;
        camera_is_dirty = true;
      }

      glm::vec3 ground_colour = glm::vec3(ubo.ground_colour);
      if (ImGui::DragFloat3("Ground Colour", &ground_colour.x, 0.1f, 0.0f,
                            1.0f)) {
        ubo.ground_colour = glm::vec4(ground_colour, 1.0);
        camera_is_dirty = true;
      }

      glm::vec3 sky_colour_horizon = glm::vec3(ubo.sky_colour_horizon);
      if (ImGui::DragFloat3("Sky Colour Horizon", &sky_colour_horizon.x, 0.1f,
                            0.0f, 1.0f)) {
        ubo.sky_colour_horizon = glm::vec4(sky_colour_horizon, 1.0);
        camera_is_dirty = true;
      }

      glm::vec3 sky_colour_zenith = glm::vec3(ubo.sky_colour_zenith);
      if (ImGui::DragFloat3("Sky Colour Zenith", &sky_colour_zenith.x, 0.1f,
                            0.0f, 1.0f)) {
        ubo.sky_colour_zenith = glm::vec4(sky_colour_zenith, 1.0);
        camera_is_dirty = true;
      }

      glm::vec3 sun_position = glm::vec3(ubo.sun_position);
      if (ImGui::DragFloat3("Sun Position", &sun_position.x, 0.1f)) {
        ubo.sun_position = glm::vec4(sun_position, 1.0);
        camera_is_dirty = true;
      }

      float sun_focus = ubo.sun_focus;
      if (ImGui::DragFloat("Sun Focus", &sun_focus, 0.1f, 0, FLT_MAX)) {
        ubo.sun_focus = sun_focus;
        camera_is_dirty = true;
      }

      float sun_intensity = ubo.sun_intensity;
      if (ImGui::DragFloat("Sun Intensity", &sun_intensity, 0.1f, 0, FLT_MAX)) {
        ubo.sun_intensity = sun_intensity;
        camera_is_dirty = true;
      }

      float defocus_strenght = ubo.defocus_strenght;
      if (ImGui::DragFloat("Defocus Strength", &defocus_strenght, 0.1f, 0,
                           FLT_MAX)) {
        ubo.defocus_strenght = defocus_strenght;
        camera_is_dirty = true;
      }

      float diverge_strength = ubo.diverge_strength;
      if (ImGui::DragFloat("Diverge Strength", &diverge_strength, 0.1f, 0,
                           FLT_MAX)) {
        ubo.diverge_strength = diverge_strength;
        camera_is_dirty = true;
      }

      ImGui::End();
    }

    ImGui::Render();

    vkDeviceWaitIdle(device.logical_device);

    vkWaitForFences(device.logical_device, 1,
                    &compute_in_flight_fences[current_frame], VK_TRUE,
                    UINT64_MAX);
    vkResetFences(device.logical_device, 1,
                  &compute_in_flight_fences[current_frame]);

    VkCommandBuffer compute_command_buffer =
        compute_command_buffers[current_frame];
    beginCommandBuffer(compute_command_buffer, 0);

    VkImageMemoryBarrier image_memory_barrier = {};
    image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_memory_barrier.pNext = 0;
    image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_memory_barrier.image = texture.handle;
    image_memory_barrier.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;
    image_memory_barrier.subresourceRange.baseMipLevel = 0;
    image_memory_barrier.subresourceRange.levelCount = 1;
    image_memory_barrier.subresourceRange.baseArrayLayer = 0;
    image_memory_barrier.subresourceRange.layerCount = 1;

    if (graphics_family_index != compute_family_index) {
      image_memory_barrier.srcAccessMask = 0;
      image_memory_barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
      image_memory_barrier.srcQueueFamilyIndex = graphics_family_index;
      image_memory_barrier.dstQueueFamilyIndex = compute_family_index;

      vkCmdPipelineBarrier(compute_command_buffer,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, 0, 0, 0,
                           1, &image_memory_barrier);
    }

    vkCmdBindPipeline(compute_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                      compute_pipeline.handle);
    vkCmdBindDescriptorSets(
        compute_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        compute_pipeline.layout, 0, 1, &compute_texture_descriptor_set, 0, 0);
    vkCmdBindDescriptorSets(
        compute_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        compute_pipeline.layout, 1, 1, &compute_ubo_descriptor_set, 0, 0);
    vkCmdBindDescriptorSets(
        compute_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        compute_pipeline.layout, 2, 1, &compute_ssbo_descriptor_set, 0, 0);

    vkCmdDispatch(compute_command_buffer, texture.width / 16,
                  texture.height / 16, 1);

    if (graphics_family_index != compute_family_index) {
      image_memory_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
      image_memory_barrier.dstAccessMask = 0;
      image_memory_barrier.srcQueueFamilyIndex = compute_family_index;
      image_memory_barrier.dstQueueFamilyIndex = graphics_family_index;

      vkCmdPipelineBarrier(compute_command_buffer,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, 0, 0, 0,
                           1, &image_memory_barrier);
    }

    vkEndCommandBuffer(compute_command_buffer);

    VkPipelineStageFlags wait_dst_stage_mask =
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

    VkSubmitInfo compute_submit_info = {};
    compute_submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    compute_submit_info.pNext = 0;
    // compute_submit_info.waitSemaphoreCount = ubo.frame == 0 ? 0 : 1;
    // compute_submit_info.pWaitSemaphores =
    //     ubo.frame == 0 ? 0 : &render_finished_semaphores[current_frame];
    // compute_submit_info.pWaitDstStageMask =
    //     ubo.frame == 0 ? 0 : &wait_dst_stage_mask;
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

    vkWaitForFences(device.logical_device, 1,
                    &compute_in_flight_fences[current_frame], VK_TRUE,
                    UINT64_MAX);
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

    if (graphics_family_index != compute_family_index) {
      image_memory_barrier.srcAccessMask = 0;
      image_memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      image_memory_barrier.srcQueueFamilyIndex = compute_family_index;
      image_memory_barrier.dstQueueFamilyIndex = graphics_family_index;

      vkCmdPipelineBarrier(graphics_command_buffer,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, 0, 0, 0,
                           1, &image_memory_barrier);
    } else {
      image_memory_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
      image_memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

      vkCmdPipelineBarrier(graphics_command_buffer,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, 0, 0, 0,
                           1, &image_memory_barrier);
    }

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

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),
                                    graphics_command_buffer, 0);
    vkCmdEndRenderPass(graphics_command_buffer);

    if (graphics_family_index != compute_family_index) {
      image_memory_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
      image_memory_barrier.dstAccessMask = 0;
      image_memory_barrier.srcQueueFamilyIndex = graphics_family_index;
      image_memory_barrier.dstQueueFamilyIndex = compute_family_index;

      vkCmdPipelineBarrier(graphics_command_buffer,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, 0, 0, 0,
                           1, &image_memory_barrier);
    }

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

    const uint32_t ms_per_frame = 1000 / 120;
    const uint32_t elapsed_time_ms = SDL_GetTicks() - start_time_ms;
    if (elapsed_time_ms < ms_per_frame) {
      SDL_Delay(ms_per_frame - elapsed_time_ms);
    }

    Input::GetMousePosition(&previous_mouse.x, &previous_mouse.y);

    ubo.frame.x++;
    if (camera_is_dirty) {
      ubo.frame.x = 0;
    }
  }

  vkDeviceWaitIdle(device.logical_device);

  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  vkDestroyDescriptorPool(device.logical_device, imgui_pool, 0);

  shutdownDescriptorLayoutCache(&device);

  destroyPipeline(&compute_pipeline, &device);

  destroyBuffer(&compute_ssbo, vma_allocator);
  destroyBuffer(&compute_ubo_buffer, vma_allocator);

  destroyTexture(&texture, &device, vma_allocator);

  shutdownDescriptorAllocator(&device);

  destroyPipeline(&graphics_pipeline, &device);

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