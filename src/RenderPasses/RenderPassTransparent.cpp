#include "RenderPassTransparent.h"

#include "Debug.h"
#include "DescriptorManager.h"
#include "MeshResourceManager.h"
#include "PerObjResourceManager.h"
#include "PipelineStateBuilder.h"
#include "RenderResourceManager.h"
#include "SamplerManager.h"
#include "VkRenderDevice.h"

namespace Muyo
{

RenderPassTransparent::RenderPassTransparent(VkExtent2D renderArea) : m_renderArea(renderArea)
{
}

void RenderPassTransparent::PrepareRenderPass()
{
    m_renderPassParameters.SetRenderArea(m_renderArea);

    // Attachments
    RenderTarget* colorAttachment = GetRenderResourceManager()->GetResource<RenderTarget>("opaqueLightingOutput");
    m_renderPassParameters.AddAttachment(colorAttachment, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, false);

    RenderTarget* depthAttachment = GetRenderResourceManager()->GetResource<RenderTarget>("GBufferDepth_");
    m_renderPassParameters.AddAttachment(depthAttachment, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, false);

    // Set 0: Perview
    const UniformBuffer<PerViewData>* perView = GetRenderResourceManager()->GetUniformBuffer<PerViewData>("perView");
    m_renderPassParameters.AddParameter(perView, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);

    // Set 1: Per object
    m_renderPassParameters.AddParameter(GetPerObjResourceManager()->GetPerObjResource(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1);

    // Set 2 Binding 0: All textures
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

    m_renderPassParameters.Finalize("transparent pass");
    CreatePipeline();
}

RenderPassTransparent::~RenderPassTransparent()
{
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr);
}

void RenderPassTransparent::CreatePipeline()
{
    VkPipelineLayout pipelineLayout = m_renderPassParameters.GetPipelineLayout();
    VkShaderModule vertShader = CreateShaderModule(ReadSpv("shaders/GBuffer.vert.spv"));
    VkShaderModule fragShader = CreateShaderModule(ReadSpv("shaders/transparent.frag.spv"));

    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(m_renderArea).Build();
    VkRect2D scissorRect;
    scissorRect.offset = {0, 0};
    scissorRect.extent = m_renderArea;

    // Descriptor layouts
    std::vector<VkDescriptorSetLayout> descLayouts = {
        GetDescriptorManager()->getDescriptorLayout(
            DESCRIPTOR_LAYOUT_PER_VIEW_DATA),
        GetDescriptorManager()->getDescriptorLayout(
            DESCRIPTOR_LAYOUT_MATERIALS),
        GetDescriptorManager()->getDescriptorLayout(
            DESCRIPTOR_LAYOUT_PER_OBJ_DATA)};

    std::vector<VkPushConstantRange> pushConstants;

    InputAssemblyStateCIBuilder iaBuilder;
    RasterizationStateCIBuilder rsBuilder;
    rsBuilder.SetCullMode(VK_CULL_MODE_NONE);
    MultisampleStateCIBuilder msBuilder;
    BlendStateCIBuilder blendBuilder;
    blendBuilder.setAttachments(1);

    // Disable depth write, enable depth test with opaque pass
    DepthStencilCIBuilder depthStencilBuilder;
    depthStencilBuilder.setDepthTestEnabled(true).setDepthWriteEnabled(false);

    PipelineStateBuilder builder;

    m_pipeline =
        builder.setShaderModules({vertShader, fragShader})
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
            .setSubpassIndex(0)
            .Build(GetRenderDevice()->GetDevice());

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShader, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShader, nullptr);

    // Set debug name for the pipeline
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipeline),
                            VK_OBJECT_TYPE_PIPELINE, "Transparent");
}

void RenderPassTransparent::RecordCommandBuffers(const std::vector<const SceneNode*>& vpGeometryNodes)
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
    if (drawCommands.size() == 0) return;
    VkCommandBufferBeginInfo beginInfo = {};

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    m_commandBuffer = GetRenderDevice()->AllocateStaticPrimaryCommandbuffer();
    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
    {
        RenderPassBeginInfoBuilder builder;
        std::vector<VkClearValue> vClearValeus = {{{.color = {0.0f, 0.0f, 0.0f, 0.0f}},
                                                   {.depthStencil = {1.0f, 0}}}};
        VkRenderPassBeginInfo renderPassBeginInfo =
            builder.setRenderPass(m_renderPassParameters.GetRenderPass())
                .setFramebuffer(m_renderPassParameters.GetFramebuffer())
                .setRenderArea(m_renderArea)
                .setClearValues(vClearValeus)
                .Build();

        vkCmdBeginRenderPass(m_commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Global mesh resource
        const MeshVertexResources& vertexResource = GetMeshResourceManager()->GetMeshVertexResources();
        VkDeviceSize offset = 0;
        const VkBuffer& vertexBuffer = vertexResource.m_pVertexBuffer->buffer();
        const VkBuffer& indexBuffer = vertexResource.m_pIndexBuffer->buffer();

        // Upload draw commands
        const DrawCommandBuffer<VkDrawIndexedIndirectCommand>* pDrawCommandBuffer = GetRenderResourceManager()->GetDrawCommandBuffer("transparent draw commands", drawCommands);

        std::vector<VkDescriptorSet> vDescSets;
        m_renderPassParameters.AllocateDescriptorSet(vDescSets);
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
