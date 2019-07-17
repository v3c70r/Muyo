#include "UIOverlay.h"
#include "PipelineStateBuilder.h"
#include "RenderContext.h"

extern VkRenderPass s_renderPass;

bool UIOverlay::initialize(RenderContext& context, uint32_t numBuffers,
                           VkPhysicalDevice physicalDevice)
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

    // create vertex and index buffers, one for each context
    mpVertexBuffers.resize(numBuffers);
    for (auto& pVertexBuffer: mpVertexBuffers)
    {
        pVertexBuffer = std::make_unique<VertexBuffer>(mDevice, physicalDevice);
    }
    mpIndexBuffers.resize(numBuffers);
    for (auto& pIndexbuffer: mpIndexBuffers)
    {
        pIndexbuffer = std::make_unique<IndexBuffer>(mDevice, physicalDevice);
    }

    return true;
}

    bool UIOverlay::initializeFontTexture(VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue)
{
    // create font texture
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    //size_t textureSize = width*height*4*sizeof(char);

    mpFontTexture = std::make_unique<Texture>();
    mpFontTexture->LoadPixels(pixels, width, height, mDevice, physicalDevice,
            commandPool, graphicsQueue);

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

bool UIOverlay::mCreatePipeline(VkDevice &device)
{



    PipelineStateBuilder builder;
    
    VkResult err;
    VkShaderModule vert_module;
    VkShaderModule frag_module;

    // Create The Shader Modules:
    {
        const uint32_t VERT_SPV[] = {
            0x07230203, 0x00010000, 0x00080001, 0x0000002e, 0x00000000,
            0x00020011, 0x00000001, 0x0006000b, 0x00000001, 0x4c534c47,
            0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000,
            0x00000001, 0x000a000f, 0x00000000, 0x00000004, 0x6e69616d,
            0x00000000, 0x0000000b, 0x0000000f, 0x00000015, 0x0000001b,
            0x0000001c, 0x00030003, 0x00000002, 0x000001c2, 0x00040005,
            0x00000004, 0x6e69616d, 0x00000000, 0x00030005, 0x00000009,
            0x00000000, 0x00050006, 0x00000009, 0x00000000, 0x6f6c6f43,
            0x00000072, 0x00040006, 0x00000009, 0x00000001, 0x00005655,
            0x00030005, 0x0000000b, 0x0074754f, 0x00040005, 0x0000000f,
            0x6c6f4361, 0x0000726f, 0x00030005, 0x00000015, 0x00565561,
            0x00060005, 0x00000019, 0x505f6c67, 0x65567265, 0x78657472,
            0x00000000, 0x00060006, 0x00000019, 0x00000000, 0x505f6c67,
            0x7469736f, 0x006e6f69, 0x00030005, 0x0000001b, 0x00000000,
            0x00040005, 0x0000001c, 0x736f5061, 0x00000000, 0x00060005,
            0x0000001e, 0x73755075, 0x6e6f4368, 0x6e617473, 0x00000074,
            0x00050006, 0x0000001e, 0x00000000, 0x61635375, 0x0000656c,
            0x00060006, 0x0000001e, 0x00000001, 0x61725475, 0x616c736e,
            0x00006574, 0x00030005, 0x00000020, 0x00006370, 0x00040047,
            0x0000000b, 0x0000001e, 0x00000000, 0x00040047, 0x0000000f,
            0x0000001e, 0x00000002, 0x00040047, 0x00000015, 0x0000001e,
            0x00000001, 0x00050048, 0x00000019, 0x00000000, 0x0000000b,
            0x00000000, 0x00030047, 0x00000019, 0x00000002, 0x00040047,
            0x0000001c, 0x0000001e, 0x00000000, 0x00050048, 0x0000001e,
            0x00000000, 0x00000023, 0x00000000, 0x00050048, 0x0000001e,
            0x00000001, 0x00000023, 0x00000008, 0x00030047, 0x0000001e,
            0x00000002, 0x00020013, 0x00000002, 0x00030021, 0x00000003,
            0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017,
            0x00000007, 0x00000006, 0x00000004, 0x00040017, 0x00000008,
            0x00000006, 0x00000002, 0x0004001e, 0x00000009, 0x00000007,
            0x00000008, 0x00040020, 0x0000000a, 0x00000003, 0x00000009,
            0x0004003b, 0x0000000a, 0x0000000b, 0x00000003, 0x00040015,
            0x0000000c, 0x00000020, 0x00000001, 0x0004002b, 0x0000000c,
            0x0000000d, 0x00000000, 0x00040020, 0x0000000e, 0x00000001,
            0x00000007, 0x0004003b, 0x0000000e, 0x0000000f, 0x00000001,
            0x00040020, 0x00000011, 0x00000003, 0x00000007, 0x0004002b,
            0x0000000c, 0x00000013, 0x00000001, 0x00040020, 0x00000014,
            0x00000001, 0x00000008, 0x0004003b, 0x00000014, 0x00000015,
            0x00000001, 0x00040020, 0x00000017, 0x00000003, 0x00000008,
            0x0003001e, 0x00000019, 0x00000007, 0x00040020, 0x0000001a,
            0x00000003, 0x00000019, 0x0004003b, 0x0000001a, 0x0000001b,
            0x00000003, 0x0004003b, 0x00000014, 0x0000001c, 0x00000001,
            0x0004001e, 0x0000001e, 0x00000008, 0x00000008, 0x00040020,
            0x0000001f, 0x00000009, 0x0000001e, 0x0004003b, 0x0000001f,
            0x00000020, 0x00000009, 0x00040020, 0x00000021, 0x00000009,
            0x00000008, 0x0004002b, 0x00000006, 0x00000028, 0x00000000,
            0x0004002b, 0x00000006, 0x00000029, 0x3f800000, 0x00050036,
            0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8,
            0x00000005, 0x0004003d, 0x00000007, 0x00000010, 0x0000000f,
            0x00050041, 0x00000011, 0x00000012, 0x0000000b, 0x0000000d,
            0x0003003e, 0x00000012, 0x00000010, 0x0004003d, 0x00000008,
            0x00000016, 0x00000015, 0x00050041, 0x00000017, 0x00000018,
            0x0000000b, 0x00000013, 0x0003003e, 0x00000018, 0x00000016,
            0x0004003d, 0x00000008, 0x0000001d, 0x0000001c, 0x00050041,
            0x00000021, 0x00000022, 0x00000020, 0x0000000d, 0x0004003d,
            0x00000008, 0x00000023, 0x00000022, 0x00050085, 0x00000008,
            0x00000024, 0x0000001d, 0x00000023, 0x00050041, 0x00000021,
            0x00000025, 0x00000020, 0x00000013, 0x0004003d, 0x00000008,
            0x00000026, 0x00000025, 0x00050081, 0x00000008, 0x00000027,
            0x00000024, 0x00000026, 0x00050051, 0x00000006, 0x0000002a,
            0x00000027, 0x00000000, 0x00050051, 0x00000006, 0x0000002b,
            0x00000027, 0x00000001, 0x00070050, 0x00000007, 0x0000002c,
            0x0000002a, 0x0000002b, 0x00000028, 0x00000029, 0x00050041,
            0x00000011, 0x0000002d, 0x0000001b, 0x0000000d, 0x0003003e,
            0x0000002d, 0x0000002c, 0x000100fd, 0x00010038};

        // glsl_shader.frag, compiled with:
        // # glslangValidator -V -x -o glsl_shader.frag.u32 glsl_shader.frag
        const uint32_t FRAG_SPV[] = {
            0x07230203, 0x00010000, 0x00080001, 0x0000001e, 0x00000000,
            0x00020011, 0x00000001, 0x0006000b, 0x00000001, 0x4c534c47,
            0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000,
            0x00000001, 0x0007000f, 0x00000004, 0x00000004, 0x6e69616d,
            0x00000000, 0x00000009, 0x0000000d, 0x00030010, 0x00000004,
            0x00000007, 0x00030003, 0x00000002, 0x000001c2, 0x00040005,
            0x00000004, 0x6e69616d, 0x00000000, 0x00040005, 0x00000009,
            0x6c6f4366, 0x0000726f, 0x00030005, 0x0000000b, 0x00000000,
            0x00050006, 0x0000000b, 0x00000000, 0x6f6c6f43, 0x00000072,
            0x00040006, 0x0000000b, 0x00000001, 0x00005655, 0x00030005,
            0x0000000d, 0x00006e49, 0x00050005, 0x00000016, 0x78655473,
            0x65727574, 0x00000000, 0x00040047, 0x00000009, 0x0000001e,
            0x00000000, 0x00040047, 0x0000000d, 0x0000001e, 0x00000000,
            0x00040047, 0x00000016, 0x00000022, 0x00000000, 0x00040047,
            0x00000016, 0x00000021, 0x00000000, 0x00020013, 0x00000002,
            0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006,
            0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004,
            0x00040020, 0x00000008, 0x00000003, 0x00000007, 0x0004003b,
            0x00000008, 0x00000009, 0x00000003, 0x00040017, 0x0000000a,
            0x00000006, 0x00000002, 0x0004001e, 0x0000000b, 0x00000007,
            0x0000000a, 0x00040020, 0x0000000c, 0x00000001, 0x0000000b,
            0x0004003b, 0x0000000c, 0x0000000d, 0x00000001, 0x00040015,
            0x0000000e, 0x00000020, 0x00000001, 0x0004002b, 0x0000000e,
            0x0000000f, 0x00000000, 0x00040020, 0x00000010, 0x00000001,
            0x00000007, 0x00090019, 0x00000013, 0x00000006, 0x00000001,
            0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000,
            0x0003001b, 0x00000014, 0x00000013, 0x00040020, 0x00000015,
            0x00000000, 0x00000014, 0x0004003b, 0x00000015, 0x00000016,
            0x00000000, 0x0004002b, 0x0000000e, 0x00000018, 0x00000001,
            0x00040020, 0x00000019, 0x00000001, 0x0000000a, 0x00050036,
            0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8,
            0x00000005, 0x00050041, 0x00000010, 0x00000011, 0x0000000d,
            0x0000000f, 0x0004003d, 0x00000007, 0x00000012, 0x00000011,
            0x0004003d, 0x00000014, 0x00000017, 0x00000016, 0x00050041,
            0x00000019, 0x0000001a, 0x0000000d, 0x00000018, 0x0004003d,
            0x0000000a, 0x0000001b, 0x0000001a, 0x00050057, 0x00000007,
            0x0000001c, 0x00000017, 0x0000001b, 0x00050085, 0x00000007,
            0x0000001d, 0x00000012, 0x0000001c, 0x0003003e, 0x00000009,
            0x0000001d, 0x000100fd, 0x00010038};

        VkShaderModuleCreateInfo vert_info = {};
        vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vert_info.codeSize = sizeof(VERT_SPV);
        vert_info.pCode = (uint32_t*)VERT_SPV;
        err = vkCreateShaderModule(device, &vert_info, nullptr, &vert_module);
        assert(err == VK_SUCCESS);
        VkShaderModuleCreateInfo frag_info = {};
        frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        frag_info.codeSize = sizeof(FRAG_SPV);
        frag_info.pCode = (uint32_t*)FRAG_SPV;
        err = vkCreateShaderModule(device, &frag_info, nullptr, &frag_module);
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




    VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {};
    inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;


    VkPipelineViewportStateCreateInfo viewport_info = {};
    viewport_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_info.viewportCount = 1;
    viewport_info.scissorCount = 1;

    // rasterizer
    VkPipelineRasterizationStateCreateInfo rasterInfo = {};
    rasterInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
    rasterInfo.cullMode = VK_CULL_MODE_NONE;
    rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterInfo.lineWidth = 1.0f;


    // MSAA
    VkPipelineMultisampleStateCreateInfo MSAAInfo = {};
    MSAAInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    MSAAInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // blending
    std::vector<VkPipelineColorBlendAttachmentState> colorAttachments(1);
    colorAttachments[0].blendEnable = VK_TRUE;
    colorAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorAttachments[0].dstColorBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorAttachments[0].colorBlendOp = VK_BLEND_OP_ADD;
    colorAttachments[0].srcAlphaBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorAttachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorAttachments[0].alphaBlendOp = VK_BLEND_OP_ADD;
    colorAttachments[0].colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineDepthStencilStateCreateInfo depthInfo = {};
    depthInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    VkPipelineColorBlendStateCreateInfo blendInfo = {};
    blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendInfo.attachmentCount = colorAttachments.size();
    blendInfo.pAttachments = colorAttachments.data();

    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                     VK_DYNAMIC_STATE_SCISSOR};

    // viewport

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = 1600;
    viewport.height = 1200;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = VkExtent2D({1600, 1200});

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = mDescriptorLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = pushConstants.data();
    assert(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                                  &mPipelineLayout) == VK_SUCCESS);


    mPipeline =
        builder.setShaderModules({vert_module, frag_module})
            .setPipelineLayout(mPipelineLayout)
            .setVertextInfo(bindingDescs, attributeDescs)
            .setAssembly(inputAssemblyInfo)
            .setViewport(viewport, scissor)
            .setRasterizer(rasterInfo)
            .setMSAA(MSAAInfo)
            .setColorBlending(blendInfo)
            .setRenderPass(s_renderPass)
            .setDynamicStates(dynamicStates)
            .setDepthStencil(depthInfo)
            .build(mDevice);

    vkDestroyShaderModule(device, vert_module, nullptr);
    vkDestroyShaderModule(device, frag_module, nullptr);

    return true;
}

bool UIOverlay::finalize()
{
    vkDestroySampler(mDevice, mFontSampler, nullptr);
    for (auto& descriptorSetLayout : mDescriptorLayouts)
        vkDestroyDescriptorSetLayout(mDevice, descriptorSetLayout,
                                     nullptr);  // mDescriptorSetLayout
    vkDestroyPipelineLayout(mDevice, mPipelineLayout, nullptr);

    // destroy index and vertex buffers
    for (auto& pVertexbuffer : mpVertexBuffers)
    {
        pVertexbuffer = nullptr;
    }
    for (auto& pIndexbuffer : mpIndexBuffers)
    {
        pIndexbuffer = nullptr;
    }
    mpFontTexture = nullptr;
    
    return true;
}

void UIOverlay::renderDrawData(ImDrawData* drawData,
                               RenderContext renderContext,
                               VkCommandPool commandPool, VkQueue graphicsQueue)
{

    // Upload Vertex and index Data:
    {
        // Vertex and index buffer
        size_t vertexSize = drawData->TotalVtxCount * sizeof(ImDrawVert);
        size_t indexSize = drawData->TotalIdxCount * sizeof(ImDrawIdx);
        std::vector<char> vertexData;
        std::vector<char> indexData;
        vertexData.resize(vertexSize);
        indexData.resize(indexSize);
        for (int i = 0; i < drawData->CmdListsCount; i++)
        {
            const ImDrawList* cmdList = drawData->CmdLists[i];
            char* vtxDst = vertexData.data();
            char* idxDst = indexData.data();
            memcpy(vtxDst, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.size() * sizeof(ImDrawVert));
            memcpy(idxDst, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.size() * sizeof(ImDrawIdx));
        }
        mpVertexBuffers[s_currentContext]->setData(vertexData.data(), vertexData.size(), commandPool, graphicsQueue);
        mpIndexBuffers[s_currentContext]->setData(indexData.data(), indexData.size(), commandPool, graphicsQueue);
    }

    //renderContext.startRecording();
    // bind descriptor set
    {
        vkCmdBindPipeline(renderContext.getCommandBuffer(),
                          VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline);
        VkDescriptorSet descSet[1] = {mDescriptorSets[0]};
        vkCmdBindDescriptorSets(renderContext.getCommandBuffer(),
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                mPipelineLayout, 0, 1, descSet, 0, NULL);
    }

    // Bind Vertex And Index Buffer:
    {
        VkBuffer vertex_buffers[1] = {
            mpVertexBuffers[s_currentContext]->buffer()};
        VkDeviceSize vertexOffset[1] = {0};
        vkCmdBindVertexBuffers(renderContext.getCommandBuffer(), 0, 1,
                               vertex_buffers, vertexOffset);
        vkCmdBindIndexBuffer(renderContext.getCommandBuffer(),
                             mpIndexBuffers[s_currentContext]->buffer(), 0,
                             VK_INDEX_TYPE_UINT16);
    }

    // Setup viewport:
    {
        VkViewport viewport;
        viewport.x = 0;
        viewport.y = 0;
        viewport.width = drawData->DisplaySize.x;
        viewport.height = drawData->DisplaySize.y;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(renderContext.getCommandBuffer(), 0, 1, &viewport);
    }

    {
        float scale[2];
        scale[0] = 2.0f / drawData->DisplaySize.x;
        scale[1] = 2.0f / drawData->DisplaySize.y;
        float translate[2];
        translate[0] = -1.0f - drawData->DisplayPos.x * scale[0];
        translate[1] = -1.0f - drawData->DisplayPos.y * scale[1];
        vkCmdPushConstants(renderContext.getCommandBuffer(), mPipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 0,
                           sizeof(float) * 2, scale);
        vkCmdPushConstants(renderContext.getCommandBuffer(), mPipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT, sizeof(float) * 2,
                           sizeof(float) * 2, translate);
    }

    // Render the command lists:
    int vtxOffset = 0;
    int idxOffset = 0;
    ImVec2 displayPos = drawData->DisplayPos;
    for (int n = 0; n < drawData->CmdListsCount; n++)
    {
        const ImDrawList* cmdList = drawData->CmdLists[n];
        for (int cmd_i = 0; cmd_i < cmdList->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmdList->CmdBuffer[cmd_i];
            if (pcmd->UserCallback)
            {
                pcmd->UserCallback(cmdList, pcmd);
            }
            else
            {
                // Apply scissor/clipping rectangle
                // FIXME: We could clamp width/height based on clamped min/max
                // values.
                VkRect2D scissor;
                scissor.offset.x =
                    (int32_t)(pcmd->ClipRect.x - displayPos.x) > 0
                        ? (int32_t)(pcmd->ClipRect.x - displayPos.x)
                        : 0;
                scissor.offset.y =
                    (int32_t)(pcmd->ClipRect.y - displayPos.y) > 0
                        ? (int32_t)(pcmd->ClipRect.y - displayPos.y)
                        : 0;
                scissor.extent.width =
                    (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x);
                scissor.extent.height =
                    (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y +
                               1);  // FIXME: Why +1 here?
                vkCmdSetScissor(renderContext.getCommandBuffer(), 0, 1,
                                &scissor);

                // Draw
                vkCmdDrawIndexed(renderContext.getCommandBuffer(),
                                 pcmd->ElemCount, 1, idxOffset, vtxOffset, 0);
            }
            idxOffset += pcmd->ElemCount;
        }
        vtxOffset += cmdList->VtxBuffer.Size;
    }
}


