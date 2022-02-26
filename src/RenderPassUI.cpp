#include "RenderPassUI.h"
#include "DescriptorManager.h"
#include "VkRenderDevice.h"
#include "DescriptorManager.h"
#include "Debug.h"
#include "RenderResourceManager.h"
#include "PipelineStateBuilder.h"
#include "PushConstantBlocks.h"

void ImGuiResource::createResources(uint32_t numSwapchainBuffers) { 
    ImGuiIO& io = ImGui::GetIO();

    // Enable docking
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Create font texture
    unsigned char* fontData;
    int texWidth, texHeight;
    io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);

    // UI font texture has texture id of 0. It has to be inserted before other texture
    GetRenderResourceManager()->GetTexture("ui_font_texture", fontData, texWidth, texHeight);
    GetDescriptorManager()->GetImGuiTextureId("ui_font_texture");

    vpVertexBuffers.resize(numSwapchainBuffers);
    vpIndexBuffers.resize(numSwapchainBuffers);
	std::vector<ImDrawVert> vDummyVert = { ImDrawVert{
		{0.0, 0.0},
		{0.0, 0.0},
		0
		}
	};
    std::vector<ImDrawIdx> vDummpyIndex = { 0 };
    //ImDrawIdx
    for (uint32_t i = 0; i< numSwapchainBuffers; i++)
    {
        vpVertexBuffers[i] = GetRenderResourceManager()->GetVertexBuffer<ImDrawVert>("UIVertex_buffer_"+std::to_string(i),vDummyVert, false);
        vpIndexBuffers[i] = GetRenderResourceManager()->GetIndexBuffer("UIIndex_buffer_" + std::to_string(i), vDummpyIndex, false);
    }
}



RenderPassUI::RenderPassUI(VkFormat swapChainFormat)
    : RenderPassFinal(swapChainFormat, false)
{
    ImGui::CreateContext();
}

void RenderPassUI::CreateImGuiResources()
{
    assert(m_vFramebuffers.size() > 0);
    m_uiResources.createResources((uint32_t)m_vFramebuffers.size());
}

RenderPassUI::~RenderPassUI()
{
    ImGui::DestroyContext();
}

void RenderPassUI::CreatePipeline()
{
    // Create pipeline layout
    std::vector<VkDescriptorSetLayout> descLayouts = {
        GetDescriptorManager()->getDescriptorLayout(
            DESCRIPTOR_LAYOUT_SINGLE_SAMPLER)};

    std::vector<VkPushConstantRange> pushConstants = {
        GetPushConstantRange<UIPushConstBlock>(VK_SHADER_STAGE_VERTEX_BIT)};

    m_pipelineLayout = GetRenderDevice()->CreatePipelineLayout(descLayouts, pushConstants);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipelineLayout),
                            VK_OBJECT_TYPE_PIPELINE_LAYOUT, "ImGui");

    // Create pipeline
    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(1, 1).Build();
    VkRect2D scissorRect = {{0, 0}, {1, 1}};

    // Dynmaic state
    std::vector<VkDynamicState> dynamicStateEnables = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkShaderModule vertShdr =
        CreateShaderModule(ReadSpv("shaders/ui.vert.spv"));
    VkShaderModule fragShdr =
        CreateShaderModule(ReadSpv("shaders/ui.frag.spv"));
    PipelineStateBuilder builder;
    // Build pipeline

    InputAssemblyStateCIBuilder iaBuilder;
    RasterizationStateCIBuilder rsBuilder;
    MultisampleStateCIBuilder msBuilder;
    BlendStateCIBuilder blendBuilder;
    blendBuilder.setAttachments(1);
    DepthStencilCIBuilder depthStencilBuilder;
    depthStencilBuilder.setDepthTestEnabled(false);
    depthStencilBuilder.setDepthWriteEnabled(false);
    depthStencilBuilder.setDepthCompareOp(VK_COMPARE_OP_LESS_OR_EQUAL);

    m_pipeline = builder.setShaderModules({vertShdr, fragShdr})
                     .setVertextInfo({UIVertex::getBindingDescription()},
                                     UIVertex::getAttributeDescriptions())
                     .setAssembly(iaBuilder.Build())
                     .setViewport(viewport, scissorRect)
                     .setRasterizer(rsBuilder.SetCullMode(VK_CULL_MODE_NONE).SetFrontFace(VK_FRONT_FACE_COUNTER_CLOCKWISE).Build())
                     .setMSAA(msBuilder.Build())
                     .setColorBlending(blendBuilder.Build())
                     .setPipelineLayout(m_pipelineLayout)
                     .setDepthStencil(depthStencilBuilder.Build())
                     .setRenderPass(m_vRenderPasses.back())
                     .setDynamicStates(dynamicStateEnables)
                     .setSubpassIndex(0)
                     .Build(GetRenderDevice()->GetDevice());

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr, nullptr);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipeline),
                            VK_OBJECT_TYPE_PIPELINE, "ImGui");
}

void RenderPassUI::newFrame(VkExtent2D screenExtent)
{
    ImGui::GetIO().DisplaySize =
        ImVec2(screenExtent.width, screenExtent.height);
    ImGui::NewFrame();
    // Render registered pages
    for (const auto& pDebugPage : m_vpDebugPages)
    {
        if (pDebugPage->ShouldRender())
        {
            pDebugPage->Render();
        }
    }
    ImGui::Render();
}

void RenderPassUI::updateBuffers(uint32_t nSwapchainBufferIndex)
{
    ImDrawData* imDrawData = ImGui::GetDrawData();

    // Note: Alignment is done inside buffer creation
    VkDeviceSize vertexBufferSize =
        imDrawData->TotalVtxCount * sizeof(ImDrawVert);
    VkDeviceSize indexBufferSize =
        imDrawData->TotalIdxCount * sizeof(ImDrawIdx);
    m_uiResources.nTotalIndexCount = imDrawData->TotalIdxCount;

    if (m_uiResources.nTotalIndexCount == 0)
    {
        return;
    }

    // Prepare vertex buffer and index buffer
    m_uiResources.vpVertexBuffers[nSwapchainBufferIndex]->SetData(nullptr, vertexBufferSize);
    m_uiResources.vpIndexBuffers[nSwapchainBufferIndex]->SetData(nullptr, indexBufferSize);

    ImDrawVert* vtxDst = (ImDrawVert*)m_uiResources.vpVertexBuffers[nSwapchainBufferIndex]->Map();
    ImDrawIdx* idxDst = (ImDrawIdx*)m_uiResources.vpIndexBuffers[nSwapchainBufferIndex]->Map();

    for (int n = 0; n < imDrawData->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = imDrawData->CmdLists[n];
		memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.size_in_bytes());
		memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.size_in_bytes());
        vtxDst += cmd_list->VtxBuffer.size();
        idxDst += cmd_list->IdxBuffer.size();
    }

    m_uiResources.vpVertexBuffers[nSwapchainBufferIndex]->Unmap();
    m_uiResources.vpIndexBuffers[nSwapchainBufferIndex]->Unmap();
}

void RenderPassUI::RecordCommandBuffer(VkExtent2D screenExtent, uint32_t nSwapchainBufferIndex)
{
    if (m_vCommandBuffers.size() != m_vFramebuffers.size())
    {
        m_vCommandBuffers.resize(m_vFramebuffers.size(), VK_NULL_HANDLE);
    }

    if (m_uiResources.nTotalIndexCount == 0)
    {
        return;
    }

    VkCommandBufferBeginInfo beginInfo = {};

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;
    ImGuiIO& io = ImGui::GetIO();

    {
        VkCommandBuffer& curCmdBuf = m_vCommandBuffers[nSwapchainBufferIndex];
        if (curCmdBuf == VK_NULL_HANDLE)
        {
            curCmdBuf = GetRenderDevice()->AllocateReusablePrimaryCommandbuffer();
            // No need to free it as it will be destroyed with the pool
        }
        vkBeginCommandBuffer(curCmdBuf, &beginInfo);

        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = m_vRenderPasses.back();
        renderPassBeginInfo.framebuffer = m_vFramebuffers[nSwapchainBufferIndex];

        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = mRenderArea;
        std::array<VkClearValue, 2> clearValues = {};
        clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassBeginInfo.clearValueCount =
            static_cast<uint32_t>(clearValues.size());
        renderPassBeginInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(curCmdBuf, &renderPassBeginInfo,
                             VK_SUBPASS_CONTENTS_INLINE);

        // Set dynamic viewport and scissor
        VkViewport viewport = {
            0.0, 0.0, (float)screenExtent.width, (float)screenExtent.height,
            0.0, 1.0};
        vkCmdSetViewport(curCmdBuf, 0, 1, &viewport);

        VkRect2D scissor = {{0, 0}, screenExtent};
        vkCmdSetScissor(curCmdBuf, 0, 1, &scissor);

        // UI scale and translate via push constants
        PushConstBlock pushConstBlock;
        pushConstBlock.scale =
            glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
        pushConstBlock.translate = glm::vec2(-1.0f);
        vkCmdPushConstants(curCmdBuf, m_pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(PushConstBlock), &pushConstBlock);

		vkCmdBindPipeline(curCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

        VkBuffer vertexBuffer = m_uiResources.vpVertexBuffers[nSwapchainBufferIndex]->buffer();
        VkBuffer indexBuffer = m_uiResources.vpIndexBuffers[nSwapchainBufferIndex]->buffer();

        ImDrawData* pDrawData = ImGui::GetDrawData();
		int32_t nVertexOffset = 0;
		int32_t nIndexOffset = 0;

        if (pDrawData->CmdListsCount > 0)
        {
			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(curCmdBuf, 0, 1, &vertexBuffer, &offset);
			vkCmdBindIndexBuffer(curCmdBuf, indexBuffer, 0, VK_INDEX_TYPE_UINT16);

            for (int32_t i = 0; i < pDrawData->CmdListsCount; i++)
            {
                const ImDrawList* pDrawList = pDrawData->CmdLists[i];
                for (int32_t j = 0; j < pDrawList->CmdBuffer.Size; j++)
                {
                    const ImDrawCmd& drawCmd = pDrawList->CmdBuffer[j];
                    // Bind correct texture
                    vkCmdBindDescriptorSets(curCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_pipelineLayout, 0, 1,
                                            &GetDescriptorManager()->GetImGuiTextureDescriptorSet((size_t)drawCmd.TextureId), 0, nullptr);
                    // Setup scissor rect according to the draw cmd
                    VkRect2D scissorRect;
                    scissorRect.offset.x = std::max((int32_t)(drawCmd.ClipRect.x), 0);
                    scissorRect.offset.y = std::max((int32_t)(drawCmd.ClipRect.y), 0);
                    scissorRect.extent.width = (uint32_t)(drawCmd.ClipRect.z - drawCmd.ClipRect.x);
                    scissorRect.extent.height = (uint32_t)(drawCmd.ClipRect.w - drawCmd.ClipRect.y);
                    vkCmdSetScissor(curCmdBuf, 0, 1, &scissorRect);
					vkCmdDrawIndexed(curCmdBuf, drawCmd.ElemCount, 1, nIndexOffset, nVertexOffset, 0);
                    nIndexOffset += drawCmd.ElemCount;
                }
                nVertexOffset += pDrawList->VtxBuffer.Size;
            }
        }

        vkCmdEndRenderPass(curCmdBuf);
        vkEndCommandBuffer(curCmdBuf);

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(curCmdBuf),
                                VK_OBJECT_TYPE_COMMAND_BUFFER, "[CB] UI");
    }
}

