#include "RenderPassUI.h"

#include "Debug.h"
#include "DescriptorManager.h"
#include "ImGuiControl.h"
#include "MeshVertex.h"
#include "PipelineStateBuilder.h"
#include "PushConstantBlocks.h"
#include "RenderResourceManager.h"
#include "SamplerManager.h"
#include "VkRenderDevice.h"
#include "RenderResourceNames.h"

namespace Muyo
{

void ImGuiResource::CreateResources()
{
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

    // use dummy geometry data because they will be updated later in UpdateBuffers().
    std::vector<ImDrawVert> vDummyVert = {ImDrawVert{
        {0.0, 0.0},
        {0.0, 0.0},
        0}};
    std::vector<ImDrawIdx> vDummpyIndex = {0};

    pVertexBuffers = GetRenderResourceManager()->GetVertexBuffer<ImDrawVert>("UIVertex_buffer", vDummyVert, false);
    pIndexBuffers = GetRenderResourceManager()->GetIndexBuffer("UIIndex_buffer", vDummpyIndex, false);
}

RenderPassUI::RenderPassUI(const VkExtent2D& renderArea)
  : m_renderArea(renderArea)
{
    ImGui::CreateContext();
    ImGui::Init();
}

void RenderPassUI::PrepareRenderPass()
{
    m_renderPassParameters.SetRenderArea(m_renderArea);

    RenderTarget* pTarget = GetRenderResourceManager()->GetRenderTarget(OPAQUE_LIGHTING_OUTPUT_ATTACHMENT_NAME, m_renderArea, VK_FORMAT_R16G16B16A16_SFLOAT);
    m_renderPassParameters.AddAttachment(pTarget, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false);

    // Image is set on per draw
    m_renderPassParameters.AddImageParameter(nullptr, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, GetSamplerManager()->getSampler(SAMPLER_1_MIPS));

    m_renderPassParameters.AddPushConstantParameter<UIPushConstBlock>(VK_SHADER_STAGE_VERTEX_BIT);

    m_renderPassParameters.Finalize("UI");

    CreatePipeline();
    CreateImGuiResources();
}

void RenderPassUI::CreateImGuiResources()
{
    m_uiResources.CreateResources();
}

RenderPassUI::~RenderPassUI()
{
    ImGui::DestroyContext();
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr);
}

void RenderPassUI::CreatePipeline()
{

    std::vector<VkPushConstantRange> pushConstants = {
        GetPushConstantRange<UIPushConstBlock>(VK_SHADER_STAGE_VERTEX_BIT)};

    // Create pipeline
    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(m_renderArea).Build();
    VkRect2D scissorRect = {{0, 0}, m_renderArea};

    // Dynmaic state
    std::vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_SCISSOR };

    VkShaderModule vertShdr = CreateShaderModule(ReadSpv("shaders/ui.vert.spv"));
    VkShaderModule fragShdr = CreateShaderModule(ReadSpv("shaders/ui.frag.spv"));
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

    VkPipelineLayout pipelineLayout = m_renderPassParameters.GetPipelineLayout();

    m_pipeline = builder.setShaderModules({vertShdr, fragShdr})
                     .setVertextInfo({UIVertex::getBindingDescription()},
                                     UIVertex::getAttributeDescriptions())
                     .setAssembly(iaBuilder.Build())
                     .setViewport(viewport, scissorRect)
                     .setRasterizer(rsBuilder.SetCullMode(VK_CULL_MODE_NONE).SetFrontFace(VK_FRONT_FACE_COUNTER_CLOCKWISE).Build())
                     .setMSAA(msBuilder.Build())
                     .setColorBlending(blendBuilder.Build())
                     .setPipelineLayout(pipelineLayout)
                     .setDepthStencil(depthStencilBuilder.Build())
                     .setRenderPass(m_renderPassParameters.GetRenderPass())
                     .setDynamicStates(dynamicStateEnables)
                     .setSubpassIndex(0)
                     .Build(GetRenderDevice()->GetDevice());

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr, nullptr);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipeline),
                            VK_OBJECT_TYPE_PIPELINE, "ImGui");
}

void RenderPassUI::NewFrame(VkExtent2D screenExtent)
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

void RenderPassUI::UpdateBuffers()
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
    m_uiResources.pVertexBuffers->SetData(nullptr, vertexBufferSize);
    m_uiResources.pIndexBuffers->SetData(nullptr, indexBufferSize);

    ImDrawVert* vtxDst = (ImDrawVert*)m_uiResources.pVertexBuffers->Map();
    ImDrawIdx* idxDst = (ImDrawIdx*)m_uiResources.pIndexBuffers->Map();

    for (int n = 0; n < imDrawData->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = imDrawData->CmdLists[n];
        memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.size_in_bytes());
        memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.size_in_bytes());
        vtxDst += cmd_list->VtxBuffer.size();
        idxDst += cmd_list->IdxBuffer.size();
    }

    m_uiResources.pVertexBuffers->Unmap();
    m_uiResources.pIndexBuffers->Unmap();
}

void RenderPassUI::RecordCommandBuffer()
{

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
        VkCommandBuffer& curCmdBuf = m_commandBuffer;
        if (curCmdBuf == VK_NULL_HANDLE)
        {
            curCmdBuf = GetRenderDevice()->AllocateReusablePrimaryCommandbuffer();
            // No need to free it as it will be destroyed with the pool
        }
        vkBeginCommandBuffer(curCmdBuf, &beginInfo);
        {
            SCOPED_MARKER(curCmdBuf, "UI");

            std::vector<VkClearValue> clearValues(1);
            clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};

            RenderPassBeginInfoBuilder builder;
            VkRenderPassBeginInfo renderPassBeginInfo =
              builder.setRenderPass(m_renderPassParameters.GetRenderPass())
                .setRenderArea(m_renderArea)
                .setFramebuffer(m_renderPassParameters.GetFramebuffer())
                .setClearValues(clearValues)
                .Build();

            vkCmdBeginRenderPass(curCmdBuf, &renderPassBeginInfo,
                                 VK_SUBPASS_CONTENTS_INLINE);

            VkRect2D scissor = {{0, 0}, m_renderArea};
            vkCmdSetScissor(curCmdBuf, 0, 1, &scissor);

            // UI scale and translate via push constants
            PushConstBlock pushConstBlock;
            pushConstBlock.scale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
            pushConstBlock.translate = glm::vec2(-1.0f);
            vkCmdPushConstants(curCmdBuf, m_renderPassParameters.GetPipelineLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT, 0,
                               sizeof(PushConstBlock), &pushConstBlock);

            vkCmdBindPipeline(curCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

            VkBuffer vertexBuffer = m_uiResources.pVertexBuffers->buffer();
            VkBuffer indexBuffer = m_uiResources.pIndexBuffers->buffer();

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
                                                m_renderPassParameters.GetPipelineLayout(), 0, 1,
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
        }
        vkEndCommandBuffer(curCmdBuf);

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(curCmdBuf), VK_OBJECT_TYPE_COMMAND_BUFFER, "[CB] UI");
    }
}

}  // namespace Muyo
