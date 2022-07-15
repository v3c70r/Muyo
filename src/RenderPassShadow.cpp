#include "RenderPassShadow.h"
#include "Camera.h"
#include "PipelineStateBuilder.h"
#include "RenderResourceManager.h"
#include "DescriptorManager.h"
#include "Geometry.h"


RenderPassShadow::~RenderPassShadow()
{
    if (m_pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
}

void RenderPassShadow::PrepareRenderPass()
{
    m_renderPassParameters.SetRenderArea(m_shadowMapSize);

    RenderTarget* shadowMap = GetRenderResourceManager()->GetDepthTarget(m_shadowCasterName, m_shadowMapSize);
    // Depth attachments
    m_renderPassParameters.AddAttachment(shadowMap, shadowMap->GetImageFormat(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL, true);


    // Binding 0
    UniformBuffer<PerViewData>* perViewDataUniformBuffer = GetRenderResourceManager()->getUniformBuffer<PerViewData>("perView");

    m_renderPassParameters.AddParameter(perViewDataUniformBuffer, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    
    // Set 1, Binding 0
    m_renderPassParameters.AddParameter(nullptr, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1);

    m_renderPassParameters.Finalize("Render pass shadow");

    CreatePipeline();
}

void RenderPassShadow::CreatePipeline()
{
    VkDescriptorSetLayout descLayout = m_renderPassParameters.GetDescriptorSetLayout();
    std::vector<VkDescriptorSetLayout> descLayouts = { descLayout };

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
    blendBuilder.setAttachments(1);
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
    setDebugUtilsObjectName( reinterpret_cast<uint64_t>(m_pipeline), VK_OBJECT_TYPE_PIPELINE, "Shadow pass");
}

void RenderPassShadow::RecordCommandBuffers(const std::vector<const Geometry*>& vpGeometries)
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
            {.depthStencil = {1.0, 0}}};
        VkRenderPassBeginInfo renderPassBeginInfo =
            builder.setRenderPass(m_renderPassParameters.GetRenderPass())
                .setFramebuffer(m_renderPassParameters.GetFramebuffer())
                .setRenderArea(m_shadowMapSize)
                .setClearValues(clearValue)
                .Build();

        vkCmdBeginRenderPass(m_commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        SCOPED_MARKER(m_commandBuffer, "Shadow pass");
        for (const Geometry* pGeometry : vpGeometries)
        {
            for (const auto& pPrimitive : pGeometry->getPrimitives())
            {
                const UniformBuffer<glm::mat4>* worldMatrixBuffer = pGeometry->GetWorldMatrixBuffer();
                assert(worldMatrixBuffer != nullptr && "Buffer must be valid");

                std::vector<VkDescriptorSet> vGBufferDescSets = {
                    m_renderPassParameters.AllocateDescriptorSet(""),
                    m_renderPassParameters.AllocateDescriptorSet("world matrix", {worldMatrixBuffer}, 1)};

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
                    m_renderPassParameters.GetPipelineLayout(), 0, vGBufferDescSets.size(),
                    vGBufferDescSets.data(), 0, nullptr);
                vkCmdDrawIndexed(m_commandBuffer, nIndexCount, 1, 0, 0, 0);
            }
        }
        vkCmdEndRenderPass(m_commandBuffer);
    }
    vkEndCommandBuffer(m_commandBuffer);
}
