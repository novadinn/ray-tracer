#include "vulkan_pipeline.h"

#include "vulkan_common.h"

bool createGraphicsPipeline(
    VulkanDevice *device, VkRenderPass render_pass,
    std::vector<VkDescriptorSetLayout> descriptor_set_layouts,
    std::vector<VkPipelineShaderStageCreateInfo> stages,
    VulkanPipeline *out_pipeline) {
  VkPipelineViewportStateCreateInfo viewport_state = {};
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.pNext = 0;
  viewport_state.flags = 0;
  viewport_state.viewportCount = 1;
  viewport_state.pViewports = 0;
  viewport_state.scissorCount = 1;
  viewport_state.pScissors = 0;

  VkPipelineRasterizationStateCreateInfo rasterizer_create_info = {};
  rasterizer_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer_create_info.pNext = 0;
  rasterizer_create_info.flags = 0;
  rasterizer_create_info.depthClampEnable = VK_FALSE;
  rasterizer_create_info.rasterizerDiscardEnable = VK_FALSE;
  rasterizer_create_info.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer_create_info.cullMode = VK_CULL_MODE_NONE;
  rasterizer_create_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer_create_info.depthBiasEnable = VK_FALSE;
  rasterizer_create_info.depthBiasConstantFactor = 0.0f;
  rasterizer_create_info.depthBiasClamp = 0.0f;
  rasterizer_create_info.depthBiasSlopeFactor = 0.0f;
  rasterizer_create_info.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisampling_create_info = {};
  multisampling_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling_create_info.pNext = 0;
  multisampling_create_info.flags = 0;
  multisampling_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampling_create_info.sampleShadingEnable = VK_FALSE;
  multisampling_create_info.minSampleShading = 1.0f;
  multisampling_create_info.pSampleMask = 0;
  multisampling_create_info.alphaToCoverageEnable = VK_FALSE;
  multisampling_create_info.alphaToOneEnable = VK_FALSE;

  VkPipelineDepthStencilStateCreateInfo depth_stencil = {};
  depth_stencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depth_stencil.pNext = 0;
  depth_stencil.flags = 0;
  depth_stencil.depthTestEnable = VK_FALSE;
  depth_stencil.depthWriteEnable = VK_FALSE;
  depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
  depth_stencil.depthBoundsTestEnable = VK_FALSE;
  depth_stencil.stencilTestEnable = VK_FALSE;
  depth_stencil.front = {};
  depth_stencil.back = {};
  depth_stencil.minDepthBounds = 0.0f;
  depth_stencil.maxDepthBounds = 1.0f;

  VkPipelineColorBlendAttachmentState color_blend_attachment_state;
  color_blend_attachment_state.blendEnable = VK_TRUE;
  color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  color_blend_attachment_state.dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  color_blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
  color_blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  color_blend_attachment_state.dstAlphaBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  color_blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;
  color_blend_attachment_state.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo color_blend_state_create_info = {};
  color_blend_state_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blend_state_create_info.pNext = 0;
  color_blend_state_create_info.flags = 0;
  color_blend_state_create_info.logicOpEnable = VK_FALSE;
  color_blend_state_create_info.logicOp = VK_LOGIC_OP_COPY;
  color_blend_state_create_info.attachmentCount = 1;
  color_blend_state_create_info.pAttachments = &color_blend_attachment_state;
  /* color_blend_state_create_info.blendConstants[4]; */

  std::vector<VkDynamicState> dynamic_states;
  dynamic_states.emplace_back(VK_DYNAMIC_STATE_VIEWPORT);
  dynamic_states.emplace_back(VK_DYNAMIC_STATE_SCISSOR);

  VkPipelineDynamicStateCreateInfo dynamic_state_create_info = {};
  dynamic_state_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state_create_info.pNext = 0;
  dynamic_state_create_info.flags = 0;
  dynamic_state_create_info.dynamicStateCount = dynamic_states.size();
  dynamic_state_create_info.pDynamicStates = &dynamic_states[0];

  VkVertexInputBindingDescription binding_description = {};
  binding_description.binding = 0;
  binding_description.stride = 0;
  binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
  vertex_input_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input_info.pNext = 0;
  vertex_input_info.flags = 0;
  vertex_input_info.vertexBindingDescriptionCount = 1;
  vertex_input_info.pVertexBindingDescriptions = &binding_description;
  vertex_input_info.vertexAttributeDescriptionCount = 0;
  vertex_input_info.pVertexAttributeDescriptions = 0;

  VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
  input_assembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly.pNext = 0;
  input_assembly.flags = 0;
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  input_assembly.primitiveRestartEnable = VK_FALSE;

  VkPipelineLayoutCreateInfo pipeline_layout_create_info = {};
  pipeline_layout_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_create_info.pNext = 0;
  pipeline_layout_create_info.flags = 0;
  pipeline_layout_create_info.setLayoutCount = descriptor_set_layouts.size();
  pipeline_layout_create_info.pSetLayouts = descriptor_set_layouts.data();
  pipeline_layout_create_info.pushConstantRangeCount = 0;
  pipeline_layout_create_info.pPushConstantRanges = 0;

  VK_CHECK(vkCreatePipelineLayout(device->logical_device,
                                  &pipeline_layout_create_info, 0,
                                  &out_pipeline->layout));

  VkGraphicsPipelineCreateInfo pipeline_create_info = {};
  pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_create_info.pNext = 0;
  pipeline_create_info.flags = 0;
  pipeline_create_info.stageCount = stages.size();
  pipeline_create_info.pStages = &stages[0];
  pipeline_create_info.pVertexInputState = &vertex_input_info;
  pipeline_create_info.pInputAssemblyState = &input_assembly;
  pipeline_create_info.pTessellationState = 0;
  pipeline_create_info.pViewportState = &viewport_state;
  pipeline_create_info.pRasterizationState = &rasterizer_create_info;
  pipeline_create_info.pMultisampleState = &multisampling_create_info;
  pipeline_create_info.pDepthStencilState = &depth_stencil;
  pipeline_create_info.pColorBlendState = &color_blend_state_create_info;
  pipeline_create_info.pDynamicState = &dynamic_state_create_info;
  pipeline_create_info.layout = out_pipeline->layout;
  pipeline_create_info.renderPass = render_pass;
  pipeline_create_info.subpass = 0;
  pipeline_create_info.basePipelineHandle = 0;
  pipeline_create_info.basePipelineIndex = -1;

  VK_CHECK(vkCreateGraphicsPipelines(device->logical_device, 0, 1,
                                     &pipeline_create_info, 0,
                                     &out_pipeline->handle));

  return true;
}

void destroyPipeline(VulkanPipeline *pipeline, VulkanDevice *device) {
  vkDestroyPipeline(device->logical_device, pipeline->handle, 0);
  vkDestroyPipelineLayout(device->logical_device, pipeline->layout, 0);
}