#include "UIOverlay.h"
#include <vulkan/vulkan.h>

// glsl_shader.vert, compiled with:
// # glslangValidator -V -x -o glsl_shader.vert.u32 glsl_shader.vert
static uint32_t __glsl_shader_vert_spv[] = {
    0x07230203, 0x00010000, 0x00080001, 0x0000002e, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x000a000f, 0x00000000,
    0x00000004, 0x6e69616d, 0x00000000, 0x0000000b, 0x0000000f, 0x00000015,
    0x0000001b, 0x0000001c, 0x00030003, 0x00000002, 0x000001c2, 0x00040005,
    0x00000004, 0x6e69616d, 0x00000000, 0x00030005, 0x00000009, 0x00000000,
    0x00050006, 0x00000009, 0x00000000, 0x6f6c6f43, 0x00000072, 0x00040006,
    0x00000009, 0x00000001, 0x00005655, 0x00030005, 0x0000000b, 0x0074754f,
    0x00040005, 0x0000000f, 0x6c6f4361, 0x0000726f, 0x00030005, 0x00000015,
    0x00565561, 0x00060005, 0x00000019, 0x505f6c67, 0x65567265, 0x78657472,
    0x00000000, 0x00060006, 0x00000019, 0x00000000, 0x505f6c67, 0x7469736f,
    0x006e6f69, 0x00030005, 0x0000001b, 0x00000000, 0x00040005, 0x0000001c,
    0x736f5061, 0x00000000, 0x00060005, 0x0000001e, 0x73755075, 0x6e6f4368,
    0x6e617473, 0x00000074, 0x00050006, 0x0000001e, 0x00000000, 0x61635375,
    0x0000656c, 0x00060006, 0x0000001e, 0x00000001, 0x61725475, 0x616c736e,
    0x00006574, 0x00030005, 0x00000020, 0x00006370, 0x00040047, 0x0000000b,
    0x0000001e, 0x00000000, 0x00040047, 0x0000000f, 0x0000001e, 0x00000002,
    0x00040047, 0x00000015, 0x0000001e, 0x00000001, 0x00050048, 0x00000019,
    0x00000000, 0x0000000b, 0x00000000, 0x00030047, 0x00000019, 0x00000002,
    0x00040047, 0x0000001c, 0x0000001e, 0x00000000, 0x00050048, 0x0000001e,
    0x00000000, 0x00000023, 0x00000000, 0x00050048, 0x0000001e, 0x00000001,
    0x00000023, 0x00000008, 0x00030047, 0x0000001e, 0x00000002, 0x00020013,
    0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006,
    0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040017,
    0x00000008, 0x00000006, 0x00000002, 0x0004001e, 0x00000009, 0x00000007,
    0x00000008, 0x00040020, 0x0000000a, 0x00000003, 0x00000009, 0x0004003b,
    0x0000000a, 0x0000000b, 0x00000003, 0x00040015, 0x0000000c, 0x00000020,
    0x00000001, 0x0004002b, 0x0000000c, 0x0000000d, 0x00000000, 0x00040020,
    0x0000000e, 0x00000001, 0x00000007, 0x0004003b, 0x0000000e, 0x0000000f,
    0x00000001, 0x00040020, 0x00000011, 0x00000003, 0x00000007, 0x0004002b,
    0x0000000c, 0x00000013, 0x00000001, 0x00040020, 0x00000014, 0x00000001,
    0x00000008, 0x0004003b, 0x00000014, 0x00000015, 0x00000001, 0x00040020,
    0x00000017, 0x00000003, 0x00000008, 0x0003001e, 0x00000019, 0x00000007,
    0x00040020, 0x0000001a, 0x00000003, 0x00000019, 0x0004003b, 0x0000001a,
    0x0000001b, 0x00000003, 0x0004003b, 0x00000014, 0x0000001c, 0x00000001,
    0x0004001e, 0x0000001e, 0x00000008, 0x00000008, 0x00040020, 0x0000001f,
    0x00000009, 0x0000001e, 0x0004003b, 0x0000001f, 0x00000020, 0x00000009,
    0x00040020, 0x00000021, 0x00000009, 0x00000008, 0x0004002b, 0x00000006,
    0x00000028, 0x00000000, 0x0004002b, 0x00000006, 0x00000029, 0x3f800000,
    0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8,
    0x00000005, 0x0004003d, 0x00000007, 0x00000010, 0x0000000f, 0x00050041,
    0x00000011, 0x00000012, 0x0000000b, 0x0000000d, 0x0003003e, 0x00000012,
    0x00000010, 0x0004003d, 0x00000008, 0x00000016, 0x00000015, 0x00050041,
    0x00000017, 0x00000018, 0x0000000b, 0x00000013, 0x0003003e, 0x00000018,
    0x00000016, 0x0004003d, 0x00000008, 0x0000001d, 0x0000001c, 0x00050041,
    0x00000021, 0x00000022, 0x00000020, 0x0000000d, 0x0004003d, 0x00000008,
    0x00000023, 0x00000022, 0x00050085, 0x00000008, 0x00000024, 0x0000001d,
    0x00000023, 0x00050041, 0x00000021, 0x00000025, 0x00000020, 0x00000013,
    0x0004003d, 0x00000008, 0x00000026, 0x00000025, 0x00050081, 0x00000008,
    0x00000027, 0x00000024, 0x00000026, 0x00050051, 0x00000006, 0x0000002a,
    0x00000027, 0x00000000, 0x00050051, 0x00000006, 0x0000002b, 0x00000027,
    0x00000001, 0x00070050, 0x00000007, 0x0000002c, 0x0000002a, 0x0000002b,
    0x00000028, 0x00000029, 0x00050041, 0x00000011, 0x0000002d, 0x0000001b,
    0x0000000d, 0x0003003e, 0x0000002d, 0x0000002c, 0x000100fd, 0x00010038};

// glsl_shader.frag, compiled with:
// # glslangValidator -V -x -o glsl_shader.frag.u32 glsl_shader.frag
static uint32_t __glsl_shader_frag_spv[] = {
    0x07230203, 0x00010000, 0x00080001, 0x0000001e, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0007000f, 0x00000004,
    0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000d, 0x00030010,
    0x00000004, 0x00000007, 0x00030003, 0x00000002, 0x000001c2, 0x00040005,
    0x00000004, 0x6e69616d, 0x00000000, 0x00040005, 0x00000009, 0x6c6f4366,
    0x0000726f, 0x00030005, 0x0000000b, 0x00000000, 0x00050006, 0x0000000b,
    0x00000000, 0x6f6c6f43, 0x00000072, 0x00040006, 0x0000000b, 0x00000001,
    0x00005655, 0x00030005, 0x0000000d, 0x00006e49, 0x00050005, 0x00000016,
    0x78655473, 0x65727574, 0x00000000, 0x00040047, 0x00000009, 0x0000001e,
    0x00000000, 0x00040047, 0x0000000d, 0x0000001e, 0x00000000, 0x00040047,
    0x00000016, 0x00000022, 0x00000000, 0x00040047, 0x00000016, 0x00000021,
    0x00000000, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002,
    0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006,
    0x00000004, 0x00040020, 0x00000008, 0x00000003, 0x00000007, 0x0004003b,
    0x00000008, 0x00000009, 0x00000003, 0x00040017, 0x0000000a, 0x00000006,
    0x00000002, 0x0004001e, 0x0000000b, 0x00000007, 0x0000000a, 0x00040020,
    0x0000000c, 0x00000001, 0x0000000b, 0x0004003b, 0x0000000c, 0x0000000d,
    0x00000001, 0x00040015, 0x0000000e, 0x00000020, 0x00000001, 0x0004002b,
    0x0000000e, 0x0000000f, 0x00000000, 0x00040020, 0x00000010, 0x00000001,
    0x00000007, 0x00090019, 0x00000013, 0x00000006, 0x00000001, 0x00000000,
    0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x0003001b, 0x00000014,
    0x00000013, 0x00040020, 0x00000015, 0x00000000, 0x00000014, 0x0004003b,
    0x00000015, 0x00000016, 0x00000000, 0x0004002b, 0x0000000e, 0x00000018,
    0x00000001, 0x00040020, 0x00000019, 0x00000001, 0x0000000a, 0x00050036,
    0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005,
    0x00050041, 0x00000010, 0x00000011, 0x0000000d, 0x0000000f, 0x0004003d,
    0x00000007, 0x00000012, 0x00000011, 0x0004003d, 0x00000014, 0x00000017,
    0x00000016, 0x00050041, 0x00000019, 0x0000001a, 0x0000000d, 0x00000018,
    0x0004003d, 0x0000000a, 0x0000001b, 0x0000001a, 0x00050057, 0x00000007,
    0x0000001c, 0x00000017, 0x0000001b, 0x00050085, 0x00000007, 0x0000001d,
    0x00000012, 0x0000001c, 0x0003003e, 0x00000009, 0x0000001d, 0x000100fd,
    0x00010038};
bool UIOverlay::mCreatePipeline()
{
    VkResult err;
    VkShaderModule vert_module;
    VkShaderModule frag_module;

    // Create The Shader Modules:
    {
        VkShaderModuleCreateInfo vert_info = {};
        vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vert_info.codeSize = sizeof(__glsl_shader_vert_spv);
        vert_info.pCode = (uint32_t*)__glsl_shader_vert_spv;
        err = vkCreateShaderModule(g_Device, &vert_info, g_Allocator,
                                   &vert_module);
        check_vk_result(err);
        VkShaderModuleCreateInfo frag_info = {};
        frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        frag_info.codeSize = sizeof(__glsl_shader_frag_spv);
        frag_info.pCode = (uint32_t*)__glsl_shader_frag_spv;
        err = vkCreateShaderModule(g_Device, &frag_info, g_Allocator,
                                   &frag_module);
        check_vk_result(err);
    }

    if (!g_FontSampler)
    {
        VkSamplerCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = VK_FILTER_LINEAR;
        info.minFilter = VK_FILTER_LINEAR;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.minLod = -1000;
        info.maxLod = 1000;
        info.maxAnisotropy = 1.0f;
        err = vkCreateSampler(g_Device, &info, g_Allocator, &g_FontSampler);
        check_vk_result(err);
    }

    if (!g_DescriptorSetLayout)
    {
        VkSampler sampler[1] = {g_FontSampler};
        VkDescriptorSetLayoutBinding binding[1] = {};
        binding[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding[0].descriptorCount = 1;
        binding[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        binding[0].pImmutableSamplers = sampler;
        VkDescriptorSetLayoutCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = 1;
        info.pBindings = binding;
        err = vkCreateDescriptorSetLayout(g_Device, &info, g_Allocator,
                                          &g_DescriptorSetLayout);
        check_vk_result(err);
    }

    // Create Descriptor Set:
    {
        VkDescriptorSetAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = g_DescriptorPool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &g_DescriptorSetLayout;
        err = vkAllocateDescriptorSets(g_Device, &alloc_info, &g_DescriptorSet);
        check_vk_result(err);
    }

    if (!g_PipelineLayout)
    {
        // Constants: we are using 'vec2 offset' and 'vec2 scale' instead of a
        // full 3d projection matrix
        VkPushConstantRange push_constants[1] = {};
        push_constants[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_constants[0].offset = sizeof(float) * 0;
        push_constants[0].size = sizeof(float) * 4;
        VkDescriptorSetLayout set_layout[1] = {g_DescriptorSetLayout};
        VkPipelineLayoutCreateInfo layout_info = {};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = set_layout;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = push_constants;
        err = vkCreatePipelineLayout(g_Device, &layout_info, g_Allocator,
                                     &g_PipelineLayout);
        check_vk_result(err);
    }

    VkPipelineShaderStageCreateInfo stage[2] = {};
    stage[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stage[0].module = vert_module;
    stage[0].pName = "main";
    stage[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stage[1].module = frag_module;
    stage[1].pName = "main";

    VkVertexInputBindingDescription binding_desc[1] = {};
    binding_desc[0].stride = sizeof(ImDrawVert);
    binding_desc[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attribute_desc[3] = {};
    attribute_desc[0].location = 0;
    attribute_desc[0].binding = binding_desc[0].binding;
    attribute_desc[0].format = VK_FORMAT_R32G32_SFLOAT;
    attribute_desc[0].offset = IM_OFFSETOF(ImDrawVert, pos);
    attribute_desc[1].location = 1;
    attribute_desc[1].binding = binding_desc[0].binding;
    attribute_desc[1].format = VK_FORMAT_R32G32_SFLOAT;
    attribute_desc[1].offset = IM_OFFSETOF(ImDrawVert, uv);
    attribute_desc[2].location = 2;
    attribute_desc[2].binding = binding_desc[0].binding;
    attribute_desc[2].format = VK_FORMAT_R8G8B8A8_UNORM;
    attribute_desc[2].offset = IM_OFFSETOF(ImDrawVert, col);

    VkPipelineVertexInputStateCreateInfo vertex_info = {};
    vertex_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_info.vertexBindingDescriptionCount = 1;
    vertex_info.pVertexBindingDescriptions = binding_desc;
    vertex_info.vertexAttributeDescriptionCount = 3;
    vertex_info.pVertexAttributeDescriptions = attribute_desc;

    VkPipelineInputAssemblyStateCreateInfo ia_info = {};
    ia_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_info = {};
    viewport_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_info.viewportCount = 1;
    viewport_info.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster_info = {};
    raster_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster_info.polygonMode = VK_POLYGON_MODE_FILL;
    raster_info.cullMode = VK_CULL_MODE_NONE;
    raster_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster_info.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms_info = {};
    ms_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState color_attachment[1] = {};
    color_attachment[0].blendEnable = VK_TRUE;
    color_attachment[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_attachment[0].dstColorBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_attachment[0].colorBlendOp = VK_BLEND_OP_ADD;
    color_attachment[0].srcAlphaBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_attachment[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_attachment[0].alphaBlendOp = VK_BLEND_OP_ADD;
    color_attachment[0].colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineDepthStencilStateCreateInfo depth_info = {};
    depth_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    VkPipelineColorBlendStateCreateInfo blend_info = {};
    blend_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend_info.attachmentCount = 1;
    blend_info.pAttachments = color_attachment;

    VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT,
                                        VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state = {};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = (uint32_t)IM_ARRAYSIZE(dynamic_states);
    dynamic_state.pDynamicStates = dynamic_states;

    VkGraphicsPipelineCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.flags = g_PipelineCreateFlags;
    info.stageCount = 2;
    info.pStages = stage;
    info.pVertexInputState = &vertex_info;
    info.pInputAssemblyState = &ia_info;
    info.pViewportState = &viewport_info;
    info.pRasterizationState = &raster_info;
    info.pMultisampleState = &ms_info;
    info.pDepthStencilState = &depth_info;
    info.pColorBlendState = &blend_info;
    info.pDynamicState = &dynamic_state;
    info.layout = g_PipelineLayout;
    info.renderPass = g_RenderPass;
    err = vkCreateGraphicsPipelines(g_Device, g_PipelineCache, 1, &info,
                                    g_Allocator, &g_Pipeline);
    check_vk_result(err);

    vkDestroyShaderModule(g_Device, vert_module, g_Allocator);
    vkDestroyShaderModule(g_Device, frag_module, g_Allocator);

    return true;
}
