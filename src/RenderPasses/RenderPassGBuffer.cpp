#include "RenderPassGBuffer.h"
#include "RenderResourceManager.h"
#include "PerObjResourceManager.h"
#include "Material.h"
#include "SamplerManager.h"
#include "PipelineStateBuilder.h"
#include "MeshVertex.h"
#include "MeshResourceManager.h"
#include "Scene.h"
#include "Camera.h"

namespace Muyo
{

const RenderPassGBuffer::GBufferAttachment RenderPassGBuffer::attachments[] =
    {
        {"GBufferPositionAO_", VK_FORMAT_R16G16B16A16_SFLOAT, {.color = {0.0f, 0.0f, 0.0f, 0.0f}}},
        {"GBufferAlbedoTransmittance_", VK_FORMAT_R16G16B16A16_SFLOAT, {.color = {0.0f, 0.0f, 0.0f, 0.0f}}},
        {"GBufferNormalRoughness_", VK_FORMAT_R16G16B16A16_SFLOAT, {.color = {0.0f, 0.0f, 0.0f, 0.0f}}},
        {"GbufferMetalnessTranslucency_", VK_FORMAT_R16G16B16A16_SFLOAT, {.color = {0.0f, 0.0f, 0.0f, 0.0f}}},
        {"GBufferDepth_", VK_FORMAT_D32_SFLOAT, {.depthStencil = {1.0f, 0}}}};

RenderPassGBuffer::~RenderPassGBuffer()
{
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr);
}
void RenderPassGBuffer::PrepareRenderPass()
{
    m_renderPassParameters.SetRenderArea(m_renderArea);

    // Output attachments
    for (int i = 0; i < ATTACHMENT_COUNT; i++)
    {
        const GBufferAttachment& attachment = attachments[i];
        RenderTarget* pTarget = GetRenderResourceManager()->GetRenderTarget(attachment.sName, m_renderArea, attachment.format);

        m_renderPassParameters.AddAttachment(pTarget,
                                             VK_IMAGE_LAYOUT_UNDEFINED,  // init layout
                                             attachment.format == VK_FORMAT_D32_SFLOAT ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,  // final layout
                                             true);
    }

    // Input resources
    const UniformBuffer<PerViewData>* perView = GetRenderResourceManager()->GetUniformBuffer<PerViewData>("perView");
    m_renderPassParameters.AddParameter(perView, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);

    // Set 1, Binding 0: PerObjData
    m_renderPassParameters.AddParameter(GetPerObjResourceManager()->GetPerObjResource(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1);

    // Set 2, Binding 0: All texture
    const auto& vpUniquePtrTextures = GetTextureResourceManager()->GetTextures();
    std::vector<const ImageResource*> vpTextures;
    vpTextures.reserve(vpUniquePtrTextures.size());
    for (const auto& pTexture : vpUniquePtrTextures)
    {
        vpTextures.push_back(pTexture.get());
    }
    m_renderPassParameters.AddImageParameter(vpTextures, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, GetSamplerManager()->getSampler(SAMPLER_1_MIPS), 2);

    // Set 2, Binding 1: All materials
    const auto* materialBuffer = GetMaterialManager()->GetMaterialBuffer();
    m_renderPassParameters.AddParameter(materialBuffer, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 2);

    m_renderPassParameters.Finalize("Render pass gbuffer");
    CreatePipeline();
}

void RenderPassGBuffer::CreatePipeline()
{
    VkPipelineLayout pipelineLayout = m_renderPassParameters.GetPipelineLayout();
    VkShaderModule vertexShader = CreateShaderModule(ReadSpv("shaders/GBuffer.vert.spv"));
    VkShaderModule fragShader = CreateShaderModule(ReadSpv("shaders/GBuffer.frag.spv"));

    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(m_renderArea).Build();
    VkRect2D scissorRect;
    scissorRect.offset = {0, 0};
    scissorRect.extent = m_renderArea;

    InputAssemblyStateCIBuilder iaBuilder;
    RasterizationStateCIBuilder rsBuilder;
    MultisampleStateCIBuilder msBuilder;
    BlendStateCIBuilder blendBuilder;
    blendBuilder.setAttachments(COLOR_ATTACHMENT_COUNT, false);
    DepthStencilCIBuilder depthStencilBuilder;
    PipelineStateBuilder builder;

    m_pipeline =
        builder.setShaderModules({vertexShader, fragShader})
            .setVertextInfo({Vertex::getBindingDescription()},
                            Vertex::getAttributeDescriptions())
            .setAssembly(iaBuilder.Build())
            .setViewport(viewport, scissorRect)
            .setRasterizer(rsBuilder.Build())
            .setMSAA(msBuilder.Build())
            .setColorBlending(blendBuilder.Build())
            .setPipelineLayout(pipelineLayout)
            .setDepthStencil(depthStencilBuilder.Build())
            .setRenderPass(m_renderPassParameters.GetRenderPass())
            .Build(GetRenderDevice()->GetDevice());

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertexShader, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShader, nullptr);

    // Set debug name for the pipeline
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipeline), VK_OBJECT_TYPE_PIPELINE, "Gbuffer pass");
}

void RenderPassGBuffer::RecordCommandBuffers(const std::vector<const SceneNode*>& vpGeometryNodes)
{
    // construct draw commands
    std::vector<VkDrawIndexedIndirectCommand> drawCommands;
    for (const SceneNode* pGeometryNode : vpGeometryNodes)
    {
        const Geometry* pGeometry = static_cast<const GeometrySceneNode*>(pGeometryNode)->GetGeometry();
        uint32_t nSubmeshIndex = 0;
        for (const auto& pSubmesh : pGeometry->getSubmeshes())
        {
            VkDrawIndexedIndirectCommand drawCommand;
            const Mesh& mesh = GetMeshResourceManager()->GetMesh(pSubmesh->GetMeshIndex());

            drawCommand.indexCount = mesh.m_nIndexCount;
            drawCommand.instanceCount = 1;
            drawCommand.firstIndex = mesh.m_nIndexOffset;
            drawCommand.vertexOffset = 0;
            drawCommand.firstInstance = PackSubmeshObjectIndex(pGeometryNode->GetPerObjId(), nSubmeshIndex++);

            drawCommands.push_back(drawCommand);
        }
    }

    // Early return if there's nothing to draw;
    if (drawCommands.size() == 0) return;

    VkCommandBufferBeginInfo beginInfo = {};

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    m_commandBuffer = GetRenderDevice()->AllocateStaticPrimaryCommandbuffer();
    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
    {
        SCOPED_MARKER(m_commandBuffer, "GBuffer pass");
        RenderPassBeginInfoBuilder builder;
        std::vector<VkClearValue> clearValues;
        clearValues.resize(ATTACHMENT_COUNT);
        for (int i = 0; i < ATTACHMENT_COUNT; i++)
        {
            clearValues[i] = attachments[i].clearValue;
        }
       
        VkRenderPassBeginInfo renderPassBeginInfo =
            builder.setRenderPass(m_renderPassParameters.GetRenderPass())
                .setFramebuffer(m_renderPassParameters.GetFramebuffer())
                .setRenderArea(m_renderArea)
                .setClearValues(clearValues)
                .Build();

        vkCmdBeginRenderPass(m_commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Global mesh resource
        const MeshVertexResources& vertexResource = GetMeshResourceManager()->GetMeshVertexResources();
        VkDeviceSize offset = 0;
        const VkBuffer& vertexBuffer = vertexResource.m_pVertexBuffer->buffer();
        const VkBuffer& indexBuffer = vertexResource.m_pIndexBuffer->buffer();

        // Upload draw commands
        const DrawCommandBuffer<VkDrawIndexedIndirectCommand>* pDrawCommandBuffer = GetRenderResourceManager()->GetDrawCommandBuffer("GBuffer draw commands", drawCommands);

        // Setup descriptor set for the whole pass
        std::vector<VkDescriptorSet> vDescSets = {
            m_renderPassParameters.AllocateDescriptorSet("", 0),
            m_renderPassParameters.AllocateDescriptorSet("", 1),
            m_renderPassParameters.AllocateDescriptorSet("", 2)
        };

        vkCmdBindDescriptorSets(
            m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_renderPassParameters.GetPipelineLayout(), 0,
            static_cast<uint32_t>(vDescSets.size()),
            vDescSets.data(), 0, nullptr);

        vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &vertexBuffer,
                               &offset);
        vkCmdBindIndexBuffer(m_commandBuffer, indexBuffer, 0,
                             VK_INDEX_TYPE_UINT32);
        vkCmdBindPipeline(m_commandBuffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_pipeline);

        vkCmdDrawIndexedIndirect(m_commandBuffer, pDrawCommandBuffer->buffer(), 0, pDrawCommandBuffer->GetDrawCommandCount(), pDrawCommandBuffer->GetStride());
        vkCmdEndRenderPass(m_commandBuffer);
    }
    vkEndCommandBuffer(m_commandBuffer);
}
}  // namespace Muyo

