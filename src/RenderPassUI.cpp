#include "RenderPassUI.h"
#include "VkRenderDevice.h"
#include "DescriptorManager.h"
#include "PipelineManager.h"
#include "Debug.h"
#include <imgui.h>

void ImGuiResource::createResources(VkRenderPass UIRenderPass) { 
    ImGuiIO& io = ImGui::GetIO();
    // Create font texture
    unsigned char* fontData;
    int texWidth, texHeight;
    io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
    pTexture = std::make_unique<Texture>();
    pTexture->LoadPixels(fontData, texWidth, texHeight);
    // Allocate descriptor
    descriptorSet = GetDescriptorManager()->allocateImGuiDescriptorSet(
        pTexture->getImageView());
    GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_IMGUI);

    GetPipelineManager()->CreateImGuiPipeline(
        1, 1,
        GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_IMGUI),
        UIRenderPass);
    pipeline = GetPipelineManager()->GetImGuiPipeline();
    pipelineLayout = GetPipelineManager()->GetImGuiPipelineLayout();

    vertexBuffer = VertexBuffer(false, "UI Vertex buffer");
    indexBuffer = IndexBuffer(false, "UI Index buffer");
}

void ImGuiResource::destroyResources()
{
    pTexture = nullptr;
    GetPipelineManager()->DestroyImGuiPipeline();
}

RenderPassUI::RenderPassUI(VkFormat swapChainFormat)
    : RenderPassFinal(swapChainFormat, false)
{
    ImGui::CreateContext();
    m_uiResources.createResources(m_renderPass);
}

RenderPassUI::~RenderPassUI()
{
    m_uiResources.destroyResources();
    ImGui::DestroyContext();
}

void RenderPassUI::newFrame(VkExtent2D screenExtent)
{
    ImGui::GetIO().DisplaySize =
        ImVec2(screenExtent.width, screenExtent.height);
    ImGui::NewFrame();
    // TODO: Add /raw functions here
    ImGui::Text("Hello");
    ImGui::Render();
}

void RenderPassUI::updateBuffers()
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
    m_uiResources.vertexBuffer.setData(nullptr, vertexBufferSize);
    m_uiResources.indexBuffer.setData(nullptr, indexBufferSize);

    ImDrawVert* vtxDst = (ImDrawVert*)m_uiResources.vertexBuffer.map();
    ImDrawIdx* idxDst = (ImDrawIdx*)m_uiResources.indexBuffer.map();

    for (int n = 0; n < imDrawData->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = imDrawData->CmdLists[n];
        memcpy(vtxDst, cmd_list->VtxBuffer.Data,
               cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(idxDst, cmd_list->IdxBuffer.Data,
               cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtxDst += cmd_list->VtxBuffer.Size;
        idxDst += cmd_list->IdxBuffer.Size;
    }

    m_uiResources.vertexBuffer.unmap();
    m_uiResources.indexBuffer.unmap();
}

void RenderPassUI::recordCommandBuffer(VkExtent2D screenExtent, uint32_t nBufferIdx)
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
        VkCommandBuffer& curCmdBuf = m_vCommandBuffers[nBufferIdx];
        if (curCmdBuf == VK_NULL_HANDLE)
        {
            curCmdBuf = GetRenderDevice()->allocateReusablePrimaryCommandbuffer();
            // No need to free it as it will be destroyed with the pool
        }
        vkBeginCommandBuffer(curCmdBuf, &beginInfo);

        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = m_renderPass;
        renderPassBeginInfo.framebuffer = m_vFramebuffers[nBufferIdx];

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

        VkRect2D scissor = {0, 0, screenExtent};
        vkCmdSetScissor(curCmdBuf, 0, 1, &scissor);

        // UI scale and translate via push constants
        PushConstBlock pushConstBlock;
        pushConstBlock.scale =
            glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
        pushConstBlock.translate = glm::vec2(-1.0f);
        vkCmdPushConstants(curCmdBuf, m_uiResources.pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(PushConstBlock), &pushConstBlock);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(curCmdBuf, 0, 1,
                               &m_uiResources.vertexBuffer.buffer(), &offset);
        vkCmdBindIndexBuffer(curCmdBuf, m_uiResources.indexBuffer.buffer(), 0,
                             VK_INDEX_TYPE_UINT32);
        vkCmdBindPipeline(curCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_uiResources.pipeline);
        vkCmdBindDescriptorSets(curCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_uiResources.pipelineLayout, 0, 1,
                                &m_uiResources.descriptorSet, 0, nullptr);

        vkCmdDrawIndexed(curCmdBuf, m_uiResources.nTotalIndexCount, 1, 0, 0, 0);

        vkCmdEndRenderPass(curCmdBuf);
        vkEndCommandBuffer(curCmdBuf);

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(curCmdBuf),
                                VK_OBJECT_TYPE_COMMAND_BUFFER, "[CB] UI");
    }
}

