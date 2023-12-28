#include "vulkan_descriptor_layout_cache.h"

#include "vulkan_common.h"

static VulkanDescriptorLayoutCache *layout_cache;

bool initializeDescriptorLayoutCache() {
    layout_cache = new VulkanDescriptorLayoutCache();
    
    return true;
}

void shutdownDescriptorLayoutCache(VulkanDevice *device) {
    for(auto pair : layout_cache->layout_cache) {
        vkDestroyDescriptorSetLayout(device->logical_device, pair.second, 0);
    }

    delete layout_cache;
}

VkDescriptorSetLayout createDescriptorLayoutFromCache(VulkanDevice *device, VkDescriptorSetLayoutCreateInfo *layout_create_info) {
  VulkanDescriptorLayoutCache::DescriptorLayoutInfo layout_info;
  layout_info.bindings.reserve(layout_create_info->bindingCount);
  bool is_sorted = true;
  int32_t last_binding = -1;

  for (uint32_t i = 0; i < layout_create_info->bindingCount; i++) {
    layout_info.bindings.push_back(layout_create_info->pBindings[i]);

    if (layout_create_info->pBindings[i].binding > last_binding) {
      last_binding = layout_create_info->pBindings[i].binding;
    } else {
      is_sorted = false;
    }
  }

  if (!is_sorted) {
    std::sort(
        layout_info.bindings.begin(), layout_info.bindings.end(),
        [](VkDescriptorSetLayoutBinding &a, VkDescriptorSetLayoutBinding &b) {
          return a.binding < b.binding;
        });
  }

  auto it = layout_cache->layout_cache.find(layout_info);
  if (it != layout_cache->layout_cache.end()) {
    return (*it).second;
  }

  VkDescriptorSetLayout layout;
  VK_CHECK(vkCreateDescriptorSetLayout(device->logical_device,
                                       layout_create_info, 0,
                                       &layout));

  layout_cache->layout_cache[layout_info] = layout;
  return layout;
}

bool VulkanDescriptorLayoutCache::DescriptorLayoutInfo::operator==(
    const DescriptorLayoutInfo &other) const {
  if (other.bindings.size() != bindings.size()) {
    return false;
  }

  for (uint32_t i = 0; i < bindings.size(); i++) {
    if (other.bindings[i].binding != bindings[i].binding) {
      return false;
    }
    if (other.bindings[i].descriptorType != bindings[i].descriptorType) {
      return false;
    }
    if (other.bindings[i].descriptorCount != bindings[i].descriptorCount) {
      return false;
    }
    if (other.bindings[i].stageFlags != bindings[i].stageFlags) {
      return false;
    }
  }

  return true;
}

size_t VulkanDescriptorLayoutCache::DescriptorLayoutInfo::hash() const {
  using std::hash;
  using std::size_t;

  size_t result = hash<size_t>()(bindings.size());

  for (const VkDescriptorSetLayoutBinding &b : bindings) {
    size_t binding_hash = b.binding | b.descriptorType << 8 |
                          b.descriptorCount << 16 | b.stageFlags << 24;

    result ^= hash<size_t>()(binding_hash);
  }

  return result;
}