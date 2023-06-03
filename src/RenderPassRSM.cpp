#include "RenderPassRSM.h"

#include "Camera.h"
#include "DescriptorManager.h"
#include "Geometry.h"
#include "PipelineStateBuilder.h"
#include "RenderResourceManager.h"
#include "SamplerManager.h"
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
    StorageBuffer<LightData>* lightDataStorageBuffer = GetRenderResourceManager()->GetResource<StorageBuffer<LightData>>("light data");
    m_renderPassParameters.AddParameter(lightDataStorageBuffer, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    // Set 1, Binding 0
    m_renderPassParameters.AddParameter(nullptr, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1);

    // Set 2, Binding 0
    // Dummy vector with proper size
    std::vector<const ImageResource*> vDummyImageResources(TEX_COUNT, nullptr);
    m_renderPassParameters.AddImageParameter(vDummyImageResources, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, GetSamplerManager()->getSampler(SAMPLER_1_MIPS), 2);

    // Set 2, Binding 1
    m_renderPassParameters.AddParameter(nullptr, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 2);

    // Push constants for light index
    m_renderPassParameters.AddPushConstantParameter<PushConstant>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    m_renderPassParameters.Finalize("Render pass shadow");

    CreatePipeline();
}

void RenderPassRSM::CreatePipeline()
{
    VkDescriptorSetLayout descLayout = m_renderPassParameters.GetDescriptorSetLayout();
    std::vector<VkDescriptorSetLayout> descLayouts = {descLayout};

    std::vector<VkPushConstantRange> pushConstants;

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

void RenderPassRSM::RecordCommandBuffers(const std::vector<const Geometry*>& vpGeometries)
{
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

        SCOPED_MARKER(m_commandBuffer, "Shadow pass: " + m_shadowCasterName);
        for (const Geometry* pGeometry : vpGeometries)
        {
            for (const auto& pPrimitive : pGeometry->getPrimitives())
            {
                const UniformBuffer<glm::mat4>* worldMatrixBuffer = pGeometry->GetWorldMatrixBuffer();
                assert(worldMatrixBuffer != nullptr && "Buffer must be valid");

                // Setup to default material
                VkDescriptorSet materialDescSet = GetMaterialManager()->GetDefaultMaterial()->GetDescriptorSet();

                if (pPrimitive->GetMaterial() != nullptr)
                {
                    materialDescSet = pPrimitive->GetMaterial()->GetDescriptorSet();
                }

                std::vector<VkDescriptorSet> vDescSets = {
                    m_renderPassParameters.AllocateDescriptorSet("", 0),
                    m_renderPassParameters.AllocateDescriptorSet("world matrix", {worldMatrixBuffer}, 1),
                    // Use global material descriptor set as last descriptor set
                    materialDescSet};

                VkDeviceSize offset = 0;
                VkBuffer vertexBuffer = pPrimitive->getVertexDeviceBuffer();
                VkBuffer indexBuffer = pPrimitive->getIndexDeviceBuffer();
                uint32_t nIndexCount = pPrimitive->getIndexCount();
                vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &vertexBuffer,
                                       &offset);
                vkCmdBindIndexBuffer(m_commandBuffer, indexBuffer, 0,
                                     VK_INDEX_TYPE_UINT32);
                vkCmdBindPipeline(m_commandBuffer,
                                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  m_pipeline);
                vkCmdBindDescriptorSets(
                    m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    m_renderPassParameters.GetPipelineLayout(), 0, vDescSets.size(),
                    vDescSets.data(), 0, nullptr);
                vkCmdDrawIndexed(m_commandBuffer, nIndexCount, 1, 0, 0, 0);
            }
        }
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
