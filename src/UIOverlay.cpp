#include "UIOverlay.h"
#include "RenderContext.h"
#include "PipelineStateBuilder.h"

bool UIOverlay::initialize(RenderContext& context)
{
    if (mFontSampler == VK_NULL_HANDLE)
    {
        mCreateFontSampler();
    }
    if (mFontSampler == VK_NULL_HANDLE)
        mCreateFontSampler();
    if (mDescriptorLayouts.size() == 0)
        mCreateDescriptorSetLayout();
    if (mPipelineLayout == VK_NULL_HANDLE)
    mCreatePipeline(*(context.getDevice()));
    return true;
}

bool UIOverlay::mCreateFontSampler()
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
    return (vkCreateSampler(mDevice, &info, nullptr, &mFontSampler) == VK_SUCCESS);
}

bool UIOverlay::mCreateDescriptorSetLayout()
{
    mDescriptorLayouts.resize(1);

    VkSampler sampler[1] = {mFontSampler};
    VkDescriptorSetLayoutBinding binding[1] = {};
    binding[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding[0].descriptorCount = 1;
    binding[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binding[0].pImmutableSamplers = sampler;
    VkDescriptorSetLayoutCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 1;
    info.pBindings = binding;

    return (vkCreateDescriptorSetLayout(mDevice, &info, nullptr,
                                        mDescriptorLayouts.data()) == VK_SUCCESS);
}

bool UIOverlay::mAllcoateDescriptorSets(VkDescriptorPool &descriptorPool)
{
    mDescriptorSets.resize(1);
    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = descriptorPool;
    alloc_info.descriptorSetCount = mDescriptorLayouts.size();
    alloc_info.pSetLayouts = mDescriptorLayouts.data();
    return (vkAllocateDescriptorSets(mDevice, &alloc_info,
                                     mDescriptorSets.data()) == VK_SUCCESS);
}

bool UIOverlay::mCreatePipelineLayout()
{
}

bool UIOverlay::mCreatePipeline(VkDevice &device)
{


    PipelineStateBuilder builder;
    
    VkResult err;
    VkShaderModule vert_module;
    VkShaderModule frag_module;

    // Create The Shader Modules:
    {
        VkShaderModuleCreateInfo vert_info = {};
        vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vert_info.codeSize = sizeof(VERT_SPV);
        vert_info.pCode = (uint32_t*)VERT_SPV;
        err = vkCreateShaderModule(device, &vert_info, nullptr,
                                   &vert_module);
        assert(err == VK_SUCCESS);
        VkShaderModuleCreateInfo frag_info = {};
        frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        frag_info.codeSize = sizeof(FRAG_SPV);
        frag_info.pCode = (uint32_t*)FRAG_SPV;
        err = vkCreateShaderModule(device, &frag_info, nullptr,
                                   &frag_module);
        assert(err == VK_SUCCESS);
    }


    // Constants: we are using 'vec2 offset' and 'vec2 scale' instead of a
    // full 3d projection matrix
    std::vector<VkPushConstantRange> pushConstants(1);
    pushConstants[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstants[0].offset = sizeof(float) * 0;
    pushConstants[0].size = sizeof(float) * 4;


    std::vector<VkVertexInputBindingDescription> bindingDescs(1);

    bindingDescs[0].stride = sizeof(ImDrawVert);
    bindingDescs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attributeDescs(3);
    attributeDescs[0].location = 0;
    attributeDescs[0].binding = bindingDescs[0].binding;
    attributeDescs[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescs[0].offset = IM_OFFSETOF(ImDrawVert, pos);
    attributeDescs[1].location = 1;
    attributeDescs[1].binding = bindingDescs[0].binding;
    attributeDescs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescs[1].offset = IM_OFFSETOF(ImDrawVert, uv);
    attributeDescs[2].location = 2;
    attributeDescs[2].binding = bindingDescs[0].binding;
    attributeDescs[2].format = VK_FORMAT_R8G8B8A8_UNORM;
    attributeDescs[2].offset = IM_OFFSETOF(ImDrawVert, col);




    VkPipelineInputAssemblyStateCreateInfo ia_info = {};
    ia_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;


    VkPipelineViewportStateCreateInfo viewport_info = {};
    viewport_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_info.viewportCount = 1;
    viewport_info.scissorCount = 1;

    builder.setShaderModules({vert_module, frag_module})
        .setPipelineLayout(mDevice, mDescriptorLayouts, pushConstants)
        .setVertextInfo(bindingDescs, attributeDescs)
        .setAssembly(ia_info);

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
    err = vkCreateGraphicsPipelines(device, g_PipelineCache, 1, &info,
                                    nullptr, &g_Pipeline);
    check_vk_result(err);

    vkDestroyShaderModule(device, vert_module, nullptr);
    vkDestroyShaderModule(device, frag_module, nullptr);

    return true;
}
