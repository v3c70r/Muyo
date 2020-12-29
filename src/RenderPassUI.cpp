#include "RenderPassUI.h"
#include "VkRenderDevice.h"
#include "DescriptorManager.h"
#include "Debug.h"
#include "RenderResourceManager.h"
#include "PipelineStateBuilder.h"
#include "PushConstantBlocks.h"
#include <imgui.h>

void ImGuiResource::createResources(VkRenderPass UIRenderPass, uint32_t numSwapchainBuffers) { 
    ImGuiIO& io = ImGui::GetIO();
    // Create font texture
    unsigned char* fontData;
    int texWidth, texHeight;
    io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
    pTexture = std::make_unique<Texture>();
    pTexture->LoadPixels(fontData, texWidth, texHeight);
    // Allocate descriptor
    descriptorSet = GetDescriptorManager()->allocateSingleSamplerDescriptorSet(
        pTexture->getView());
    GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_SINGLE_SAMPLER);

    vertexBuffers.resize(numSwapchainBuffers);
    indexBuffers.resize(numSwapchainBuffers);
    for (uint32_t i = 0; i< numSwapchainBuffers; i++)
    {
        vertexBuffers[i] = VertexBuffer(false, "UI Vertex buffer");
        indexBuffers[i] = IndexBuffer(false, "UI Index buffer");
    }
}

void ImGuiResource::destroyResources()
{
    pTexture = nullptr;
}

RenderPassUI::RenderPassUI(VkFormat swapChainFormat)
    : RenderPassFinal(swapChainFormat, false)
{
    ImGui::CreateContext();
}

void RenderPassUI::setSwapchainImageViews(std::vector<VkImageView>& vImageViews, VkImageView depthImageView, uint32_t nWidth, uint32_t nHeight)
{
    m_uiResources.createResources(m_vRenderPasses.back(), vImageViews.size());
    RenderPassFinal::setSwapchainImageViews(vImageViews, depthImageView, nWidth, nHeight);
}

RenderPassUI::~RenderPassUI()
{
    m_uiResources.destroyResources();
    ImGui::DestroyContext();
}

void RenderPassUI::setupPipeline()
{
    // Create pipeline layout
    std::vector<VkDescriptorSetLayout> descLayouts = {
        GetDescriptorManager()->getDescriptorLayout(
            DESCRIPTOR_LAYOUT_SINGLE_SAMPLER)};

    std::vector<VkPushConstantRange> pushConstants = {
        getPushConstantRange<UIPushConstBlock>(VK_SHADER_STAGE_VERTEX_BIT)};

    m_pipelineLayout =
        PipelineManager::CreatePipelineLayout(descLayouts, pushConstants);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipelineLayout),
                            VK_OBJECT_TYPE_PIPELINE_LAYOUT, "ImGui");

    // Create pipeline
    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(1, 1).build();
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
                     .setAssembly(iaBuilder.build())
                     .setViewport(viewport, scissorRect)
                     .setRasterizer(rsBuilder.build())
                     .setMSAA(msBuilder.build())
                     .setColorBlending(blendBuilder.build())
                     .setPipelineLayout(m_pipelineLayout)
                     .setDepthStencil(depthStencilBuilder.build())
                     .setRenderPass(m_vRenderPasses.back())
                     .setDynamicStates(dynamicStateEnables)
                     .setSubpassIndex(0)
                     .build(GetRenderDevice()->GetDevice());

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
    {
        ImGui::Begin("Debug Resources");
        // TODO: Add /raw functions here
        const auto& resourceMap = GetRenderResourceManager()->getResourceMap();
        for (const auto& resMap : resourceMap)
        {
            ImGui::Text("%s", resMap.first.c_str());
        }
        ImGui::End();
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
    m_uiResources.vertexBuffers[nSwapchainBufferIndex].setData(nullptr, vertexBufferSize);
    m_uiResources.indexBuffers[nSwapchainBufferIndex].setData(nullptr, indexBufferSize);

    ImDrawVert* vtxDst = (ImDrawVert*)m_uiResources.vertexBuffers[nSwapchainBufferIndex].map();
    ImDrawIdx* idxDst = (ImDrawIdx*)m_uiResources.indexBuffers[nSwapchainBufferIndex].map();

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

    m_uiResources.vertexBuffers[nSwapchainBufferIndex].unmap();
    m_uiResources.indexBuffers[nSwapchainBufferIndex].unmap();
}

void RenderPassUI::recordCommandBuffer(VkExtent2D screenExtent, uint32_t nSwapchainBufferIndex)
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

        VkRect2D scissor = {0, 0, screenExtent};
        vkCmdSetScissor(curCmdBuf, 0, 1, &scissor);

        // UI scale and translate via push constants
        PushConstBlock pushConstBlock;
        pushConstBlock.scale =
            glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
        pushConstBlock.translate = glm::vec2(-1.0f);
        vkCmdPushConstants(curCmdBuf, m_pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(PushConstBlock), &pushConstBlock);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(curCmdBuf, 0, 1,
                               &m_uiResources.vertexBuffers[nSwapchainBufferIndex].buffer(), &offset);
        vkCmdBindIndexBuffer(curCmdBuf, m_uiResources.indexBuffers[nSwapchainBufferIndex].buffer(), 0,
                             VK_INDEX_TYPE_UINT32);
        vkCmdBindPipeline(curCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        vkCmdBindDescriptorSets(curCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_pipelineLayout, 0, 1,
                                &m_uiResources.descriptorSet, 0, nullptr);

        vkCmdDrawIndexed(curCmdBuf, m_uiResources.nTotalIndexCount, 1, 0, 0, 0);

        vkCmdEndRenderPass(curCmdBuf);
        vkEndCommandBuffer(curCmdBuf);

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(curCmdBuf),
                                VK_OBJECT_TYPE_COMMAND_BUFFER, "[CB] UI");
    }
}

