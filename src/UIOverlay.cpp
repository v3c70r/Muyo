#include "UIOverlay.h"
#include "RenderContext.h"
#include "PipelineStateBuilder.h"

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

    // create font texture
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    size_t textureSize = width*height*4*sizeof(char);

    mpFontTexture = std::make_unique<Texture>();
    mpFontTexture->LoadPixels(pixels, width, height, mDevice, physicalDevice);


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

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = mDescriptorLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = 0;
    assert(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                                  &mPipelineLayout) == VK_SUCCESS);

    mPipeline =
        builder.setShaderModules({vert_module, frag_module})
            .setPipelineLayout(mPipelineLayout)
            .setVertextInfo(bindingDescs, attributeDescs)
            .setAssembly(inputAssemblyInfo)
            .setRasterizer(rasterInfo)
            .setMSAA(MSAAInfo)
            .setColorBlending(blendInfo)
            .setDynamicStates(dynamicStates)
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
