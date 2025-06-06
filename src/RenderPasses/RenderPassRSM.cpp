#include "RenderPassRSM.h"

#include "Camera.h"
#include "DescriptorManager.h"
#include "Geometry.h"
#include "MeshResourceManager.h"
#include "PerObjResourceManager.h"
#include "PipelineStateBuilder.h"
#include "RenderResourceManager.h"
#include "SamplerManager.h"
#include "Scene.h"
#include "vulkan/vulkan_core.h"

namespace Muyo
{

RenderPassRSM::~RenderPassRSM()
{
    if (m_pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
}

void RenderPassRSM::PrepareRenderPass()
{
    m_renderPassParameters.SetRenderArea(m_shadowMapSize);

    // Depth attachments
    RenderTarget* shadowMap = GetRenderResourceManager()->GetDepthTarget(m_aRSMNames[SHADOW_MAP_DEPTH], m_shadowMapSize);
    m_renderPassParameters.AddAttachment(shadowMap, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true);

    // Shadow Normal
    RenderTarget* shadowNormal = GetRenderResourceManager()->GetColorTarget(m_aRSMNames[SHADOW_MAP_NORMAL], m_shadowMapSize);
    m_renderPassParameters.AddAttachment(shadowNormal, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true);

    // Shadow position
    RenderTarget* shadowPosition = GetRenderResourceManager()->GetColorTarget(m_aRSMNames[SHADOW_MAP_POSITION], m_shadowMapSize);
    m_renderPassParameters.AddAttachment(shadowPosition, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true);

    // Shadow flux
    RenderTarget* shadowFlux = GetRenderResourceManager()->GetColorTarget(m_aRSMNames[SHADOW_MAP_FLUX], m_shadowMapSize);
    m_renderPassParameters.AddAttachment(shadowFlux, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true);

    // Set0, Binding 0
    const StorageBuffer<LightData>* lightDataStorageBuffer = GetRenderResourceManager()->GetResource<StorageBuffer<LightData>>("light data");
    m_renderPassParameters.AddParameter(lightDataStorageBuffer, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    // Set 1, Binding 0
    // All textures
    const auto& vpUniquePtrTextures = GetTextureResourceManager()->GetTextures();
    std::vector<const ImageResource*> vpTextures;
    vpTextures.reserve(vpUniquePtrTextures.size());
    for (const auto& pTexture : vpUniquePtrTextures)
    {
        vpTextures.push_back(pTexture.get());
    }
    m_renderPassParameters.AddImageParameter(vpTextures, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, GetSamplerManager()->getSampler(SAMPLER_1_MIPS), 1);

    // Set 1, Binding 1
    const auto* materialBuffer = GetMaterialManager()->GetMaterialBuffer();
    m_renderPassParameters.AddParameter(materialBuffer, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 1);

    // Set 2, binding 0 PerObjData
    m_renderPassParameters.AddParameter(GetPerObjResourceManager()->GetPerObjResource(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 2);
    // Push constants for light index
    m_renderPassParameters.AddPushConstantParameter<PushConstant>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    m_renderPassParameters.Finalize("Render pass shadow");

    CreatePipeline();
}

void RenderPassRSM::CreatePipeline()
{
    VkPipelineLayout pipelineLayout = m_renderPassParameters.GetPipelineLayout();

    VkShaderModule vertexShader = CreateShaderModule(ReadSpv("shaders/shadow.vert.spv"));
    VkShaderModule fragShader = CreateShaderModule(ReadSpv("shaders/shadow.frag.spv"));

    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(m_shadowMapSize).Build();
    VkRect2D scissorRect;
    scissorRect.offset = {0, 0};
    scissorRect.extent = m_shadowMapSize;

    InputAssemblyStateCIBuilder iaBuilder;
    RasterizationStateCIBuilder rsBuilder;
    MultisampleStateCIBuilder msBuilder;
    BlendStateCIBuilder blendBuilder;
    blendBuilder.setAttachments(3, false);
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
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipeline), VK_OBJECT_TYPE_PIPELINE, "Shadow pass");
}

void RenderPassRSM::RecordCommandBuffers(const std::vector<const SceneNode*>& vpGeometryNodes)
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
        RenderPassBeginInfoBuilder builder;
        std::vector<VkClearValue> clearValue = {
            {.depthStencil = {1.0, 0}},
            {.color = {0.0, 0.0, 0.0, 0.0}},
            {.color = {0.0, 0.0, 0.0, 0.0}},
            {.color = {0.0, 0.0, 0.0, 0.0}}};
        VkRenderPassBeginInfo renderPassBeginInfo =
            builder.setRenderPass(m_renderPassParameters.GetRenderPass())
                .setFramebuffer(m_renderPassParameters.GetFramebuffer())
                .setRenderArea(m_shadowMapSize)
                .setClearValues(clearValue)
                .Build();

        vkCmdBeginRenderPass(m_commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        PushConstant pushConstant = {m_nLightIndex, m_shadowMapSize.width};
        vkCmdPushConstants(m_commandBuffer, m_renderPassParameters.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstant), &pushConstant);

        // Global mesh resource
        const MeshVertexResources& vertexResource = GetMeshResourceManager()->GetMeshVertexResources();
        VkDeviceSize offset = 0;
        const VkBuffer& vertexBuffer = vertexResource.m_pVertexBuffer->buffer();
        const VkBuffer& indexBuffer = vertexResource.m_pIndexBuffer->buffer();

        // Upload draw commands
        const DrawCommandBuffer<VkDrawIndexedIndirectCommand>* pDrawCommandBuffer = GetRenderResourceManager()->GetDrawCommandBuffer("rsm shadow " + m_shadowCasterName, drawCommands);

        // Setup descriptor set for the whole pass

        std::vector<VkDescriptorSet> vDescSets = {
            m_renderPassParameters.AllocateDescriptorSet("", 0),
            m_renderPassParameters.AllocateDescriptorSet("", 1),
            m_renderPassParameters.AllocateDescriptorSet("", 2)};

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

        SCOPED_MARKER(m_commandBuffer, "Shadow pass: " + m_shadowCasterName);
        vkCmdDrawIndexedIndirect(m_commandBuffer, pDrawCommandBuffer->buffer(), 0, pDrawCommandBuffer->GetDrawCommandCount(), pDrawCommandBuffer->GetStride());
        vkCmdEndRenderPass(m_commandBuffer);
    }
    vkEndCommandBuffer(m_commandBuffer);
}

RSMResources RenderPassRSM::GetRSM()
{
    return {
        GetRenderResourceManager()->GetResource<RenderTarget>(m_aRSMNames[SHADOW_MAP_DEPTH]),
        GetRenderResourceManager()->GetResource<RenderTarget>(m_aRSMNames[SHADOW_MAP_NORMAL]),
        GetRenderResourceManager()->GetResource<RenderTarget>(m_aRSMNames[SHADOW_MAP_POSITION]),
        GetRenderResourceManager()->GetResource<RenderTarget>(m_aRSMNames[SHADOW_MAP_FLUX])};
}

}  // namespace Muyo
