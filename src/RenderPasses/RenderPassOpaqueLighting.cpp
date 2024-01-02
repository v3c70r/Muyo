#include "RenderPassOpaqueLighting.h"

#include <algorithm>

#include "Camera.h"
#include "MeshResourceManager.h"
#include "MeshVertex.h"
#include "PipelineStateBuilder.h"
#include "RenderPassGBuffer.h"
#include "RenderResourceManager.h"
#include "ResourceBarrier.h"
#include "SamplerManager.h"
#include "SharedStructures.h"

namespace Muyo
{

void RenderPassOpaqueLighting::PrepareRenderPass()
{
    m_renderPassParameters.SetRenderArea(m_renderArea);

    // opaque lighting output
    const RenderTarget* pRenderTarget = GetRenderResourceManager()->GetRenderTarget("opaqueLightingOutput", m_renderArea, VK_FORMAT_R16G16B16A16_SFLOAT);
    m_renderPassParameters.AddAttachment(pRenderTarget, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);

    // Set 0: Camera UBO
    const UniformBuffer<PerViewData>* perView = GetRenderResourceManager()->GetUniformBuffer<PerViewData>("perView");
    m_renderPassParameters.AddParameter(perView, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);

    // Set 1: GBuffer textures
    for (int i = 0; i < RenderPassGBuffer::COLOR_ATTACHMENT_COUNT; i++)
    {
        const RenderPassGBuffer::GBufferAttachment& attachment = RenderPassGBuffer::attachments[i];
        m_renderPassParameters.AddImageParameter(GetRenderResourceManager()->GetResource<ImageResource>(attachment.sName),
                                                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                                                 GetSamplerManager()->getSampler(SAMPLER_1_MIPS), 1);
    }

    // Set 2: IBL textures
    const RenderTarget* pIrradianceMap = GetRenderResourceManager()->GetRenderTarget("irr_cube_map", m_renderArea, VK_FORMAT_R16G16B16A16_SFLOAT);
    const RenderTarget* pPrefilteredCubeMap = GetRenderResourceManager()->GetRenderTarget("prefiltered_cubemap", m_renderArea, VK_FORMAT_R16G16B16A16_SFLOAT);
    const RenderTarget* pSpecularBrdfLut = GetRenderResourceManager()->GetRenderTarget("specular_brdf_lut", m_renderArea, VK_FORMAT_R16G16B16A16_SFLOAT);

    m_renderPassParameters.AddImageParameter(pIrradianceMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, GetSamplerManager()->getSampler(SAMPLER_1_MIPS), 2);
    m_renderPassParameters.AddImageParameter(pPrefilteredCubeMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, GetSamplerManager()->getSampler(SAMPLER_8_MIPS), 2);
    m_renderPassParameters.AddImageParameter(pSpecularBrdfLut, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, GetSamplerManager()->getSampler(SAMPLER_8_MIPS), 2);

    // Set 3: Light UBO

    m_renderPassParameters.AddParameter(GetRenderResourceManager()->GetResource<UniformBuffer<uint32_t>>("light count"), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 3);
    const StorageBuffer<LightData>* lightDataStorageBuffer = GetRenderResourceManager()->GetResource<StorageBuffer<LightData>>("light data");
    m_renderPassParameters.AddParameter(lightDataStorageBuffer, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 3);

    // Set 4: Shadows

    std::vector<RSMResources> vpShadowMaps = m_shadowPassManager.GetShadowMaps();

    if (vpShadowMaps.size() > 0)
    {
        std::vector<const ImageResource*> vpDepthResources;
        std::vector<const ImageResource*> vpNormalResources;
        std::vector<const ImageResource*> vpPositionResources;
        std::vector<const ImageResource*> vpFluxResources;
        std::for_each(vpShadowMaps.begin(), vpShadowMaps.end(),
                      [&vpDepthResources,
                       &vpNormalResources,
                       &vpPositionResources,
                       &vpFluxResources](const RSMResources& rsmResources)
                      {
                          vpDepthResources.push_back(rsmResources.pDepth);
                          vpNormalResources.push_back(rsmResources.pNormal);
                          vpPositionResources.push_back(rsmResources.pPosition);
                          vpFluxResources.push_back(rsmResources.pFlux);
                      });

        m_renderPassParameters.AddImageParameter(vpDepthResources, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, GetSamplerManager()->getSampler(SAMPLER_1_MIPS), 4);
        m_renderPassParameters.AddImageParameter(vpNormalResources, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, GetSamplerManager()->getSampler(SAMPLER_1_MIPS), 4);
        m_renderPassParameters.AddImageParameter(vpPositionResources, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, GetSamplerManager()->getSampler(SAMPLER_1_MIPS), 4);
        m_renderPassParameters.AddImageParameter(vpFluxResources, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, GetSamplerManager()->getSampler(SAMPLER_1_MIPS), 4);
    }
    m_renderPassParameters.Finalize("Lighting");

    CreatePipeline();
}

void RenderPassOpaqueLighting::CreatePipeline()
{
    VkPipelineLayout pipelineLayout = m_renderPassParameters.GetPipelineLayout();

    VkShaderModule vertexShader = CreateShaderModule(ReadSpv("shaders/lighting.vert.spv"));
    VkShaderModule fragShader = CreateShaderModule(ReadSpv("shaders/lighting.frag.spv"));

    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(m_renderArea).Build();
    VkRect2D scissorRect;
    scissorRect.offset = {0, 0};
    scissorRect.extent = m_renderArea;

    InputAssemblyStateCIBuilder iaBuilder;
    RasterizationStateCIBuilder rsBuilder;
    MultisampleStateCIBuilder msBuilder;
    BlendStateCIBuilder blendBuilder;
    blendBuilder.setAttachments(1, false);
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
            .setDepthStencil(depthStencilBuilder.setDepthTestEnabled(false).setDepthWriteEnabled(false).Build())
            .setRenderPass(m_renderPassParameters.GetRenderPass())
            .Build(GetRenderDevice()->GetDevice());

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertexShader, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShader, nullptr);

    // Set debug name for the pipeline
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipeline), VK_OBJECT_TYPE_PIPELINE, "lighting pass");
}

void RenderPassOpaqueLighting::RecordCommandBuffers()
{
    VkCommandBufferBeginInfo beginInfo = {};

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;
    m_commandBuffer = GetRenderDevice()->AllocateStaticPrimaryCommandbuffer();

    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
    {
        SCOPED_MARKER(m_commandBuffer, "Opaque lighting");

        // Transit gbuffer attachments
        //for (int i = 0; i < RenderPassGBuffer::COLOR_ATTACHMENT_COUNT; i++)
        //{
        //    const RenderPassGBuffer::GBufferAttachment& attachment = RenderPassGBuffer::attachments[i];
        //    // TODO: Finish barrier refactoring to cover attachments to read optimal transition
        //    ImageResourceBarrier(GetRenderResourceManager()->GetResource<ImageResource>(attachment.sName)->getImage(), VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_UNDEFINED, 1, 1).AddToCommandBuffer(m_commandBuffer);
        //}

        RenderPassBeginInfoBuilder builder;

        std::vector<VkClearValue> clearValues = {{.color={0.0f, 0.0f, 0.0f, 1.0f}}};
        VkRenderPassBeginInfo renderPassBeginInfo =
            builder.setRenderPass(m_renderPassParameters.GetRenderPass())
                .setFramebuffer(m_renderPassParameters.GetFramebuffer())
                .setRenderArea(m_renderArea)
                .setClearValues(clearValues)
                .Build();
        vkCmdBeginRenderPass(m_commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        const Mesh& quadMesh = GetMeshResourceManager()->GetQuad();
        const MeshVertexResources& meshVertexResources = GetMeshResourceManager()->GetMeshVertexResources();
        VkDeviceSize offset = 0;
        VkBuffer vertexBuffer = meshVertexResources.m_pVertexBuffer->buffer();
        VkBuffer indexBuffer = meshVertexResources.m_pIndexBuffer->buffer();
        uint32_t nIndexCount = quadMesh.m_nIndexCount;
        uint32_t nIndexOffset = quadMesh.m_nIndexOffset;


        std::vector<VkDescriptorSet> vDescSets = {
            m_renderPassParameters.AllocateDescriptorSet("", 0),
            m_renderPassParameters.AllocateDescriptorSet("", 1),
            m_renderPassParameters.AllocateDescriptorSet("", 2),
            m_renderPassParameters.AllocateDescriptorSet("", 3),
            m_renderPassParameters.AllocateDescriptorSet("", 4)
        };

        vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &vertexBuffer,
                               &offset);
        vkCmdBindIndexBuffer(m_commandBuffer, indexBuffer, 0,
                             VK_INDEX_TYPE_UINT32);
        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_pipeline);
        vkCmdBindDescriptorSets(
            m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_renderPassParameters.GetPipelineLayout(), 0, vDescSets.size(),
            vDescSets.data(), 0, nullptr);
        vkCmdDrawIndexed(m_commandBuffer, nIndexCount, 1, nIndexOffset, 0, 0);
        vkCmdEndRenderPass(m_commandBuffer);
    }
    vkEndCommandBuffer(m_commandBuffer);
}
}
