#include "RenderPassGaussianSplats.h"
#include <vulkan/vulkan_core.h>

#include "Camera.h"
#include "PipelineStateBuilder.h"
#include "RenderResourceManager.h"
namespace Muyo
{
void RenderPassGaussianSplats::PrepareRenderPass() {

    if (m_pGaussianSplatsSceneNode == nullptr) {
        return;
    }

    m_renderPassParameters.SetRenderArea(m_renderArea);
    m_renderPassParameters.AddAttachment(GetRenderResourceManager()->GetColorTarget("GaussianSplats", m_renderArea, VK_FORMAT_R16G16B16A16_SFLOAT), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true);

    // Perview
    m_renderPassParameters.AddParameter(GetRenderResourceManager()->GetUniformBuffer<PerViewData>("perView"),
                                        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);

    assert(m_pGaussianSplatsSceneNode != nullptr);
    // bind Gaussian splats buffers
    m_renderPassParameters.AddParameter(m_pGaussianSplatsSceneNode->GetGpuPositionBuffer(),
                                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1);
    m_renderPassParameters.AddParameter(m_pGaussianSplatsSceneNode->GetGpuScaleBuffer(),
                                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1);
    m_renderPassParameters.AddParameter(m_pGaussianSplatsSceneNode->GetGpuRotationBuffer(),
                                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1);
    m_renderPassParameters.AddParameter(m_pGaussianSplatsSceneNode->GetGpuAlphaBuffer(),
                                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1);
    m_renderPassParameters.AddParameter(m_pGaussianSplatsSceneNode->GetGpuColorBuffer(),
                                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1);

    m_renderPassParameters.AddPushConstantParameter<int>(VK_SHADER_STAGE_VERTEX_BIT);
    m_renderPassParameters.Finalize("Render pass Gaussian splats");
    CreatePipeline();
}

void RenderPassGaussianSplats::CreatePipeline()
{
    VkPipelineLayout pipelineLayout = m_renderPassParameters.GetPipelineLayout();

    VkShaderModule vertShader = CreateShaderModule(ReadSpv("shaders/gaussianSplats.vert.slang.spv"));
    VkShaderModule fragShader = CreateShaderModule(ReadSpv("shaders/gaussianSplats.frag.slang.spv"));

    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(m_renderArea).Build();
    VkRect2D scissorRect;
    scissorRect.offset = {0, 0};
    scissorRect.extent = m_renderArea;

    RasterizationStateCIBuilder rsBuilder;
    MultisampleStateCIBuilder msBuilder;

    InputAssemblyStateCIBuilder iaBuilder;
    BlendStateCIBuilder blendBuilder;
    blendBuilder.setAttachments(1, true);
    DepthStencilCIBuilder depthStencilBuilder;
    depthStencilBuilder.setDepthTestEnabled(false).setDepthWriteEnabled(false);
    PipelineStateBuilder builder;

    m_pipeline = builder.SetShaderModule(vertShader, VK_SHADER_STAGE_VERTEX_BIT)
                     .setVertextInfo({}, {})
                     .setAssembly(iaBuilder.Build())
                     .SetShaderModule(fragShader, VK_SHADER_STAGE_FRAGMENT_BIT)
                     .setViewport(viewport, scissorRect)
                     .setRasterizer(rsBuilder.Build())
                     .setMSAA(msBuilder.Build())
                     .setColorBlending(blendBuilder.Build())
                     .setPipelineLayout(pipelineLayout)
                     .setDepthStencil(depthStencilBuilder.Build())
                     .setRenderPass(m_renderPassParameters.GetRenderPass())
                     .Build(GetRenderDevice()->GetDevice());

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShader, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShader, nullptr);
}

void RenderPassGaussianSplats::RecordCommandBuffers()
{
    // Construct a draw command to draw a quad for each splat
    VkDrawIndirectCommand drawCommand;
    drawCommand.vertexCount = 6;
    drawCommand.instanceCount = m_pGaussianSplatsSceneNode->GetNumSplats(); 
    drawCommand.firstVertex = 0;
    drawCommand.firstInstance = 0;
    std::vector<VkDrawIndirectCommand> drawCommands{drawCommand};

    // Upload draw commands to GPU
    const DrawCommandBuffer<VkDrawIndirectCommand> *pDrawCommandBuffer = GetRenderResourceManager()->GetDrawCommandBuffer("Gaussian splats draw commands", drawCommands);

    VkCommandBufferBeginInfo beginInfo = {};

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    m_commandBuffer = GetRenderDevice()->AllocateStaticPrimaryCommandbuffer();
    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
    {
        SCOPED_MARKER(m_commandBuffer, "Points");
        std::vector<VkClearValue> clearValues{VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}}};

        RenderPassBeginInfoBuilder builder;
        VkRenderPassBeginInfo renderPassBeginInfo =
            builder.setRenderPass(m_renderPassParameters.GetRenderPass())
                .setFramebuffer(m_renderPassParameters.GetFramebuffer())
                .setRenderArea(m_renderArea)
                .setClearValues(clearValues)
                .Build();

        vkCmdBeginRenderPass(m_commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Setup descriptor set for the whole pass
        std::vector<VkDescriptorSet> vDescSets = {
            m_renderPassParameters.AllocateDescriptorSet("", 0),
            m_renderPassParameters.AllocateDescriptorSet("", 1),
        };

        // Push number of SH degrees
        vkCmdPushConstants(m_commandBuffer, m_renderPassParameters.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(int), &m_pGaussianSplatsSceneNode->m_ShDegrees);
        vkCmdBindDescriptorSets(
            m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_renderPassParameters.GetPipelineLayout(), 0,
            static_cast<uint32_t>(vDescSets.size()),
            vDescSets.data(), 0, nullptr);

        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

        vkCmdDrawIndirect(m_commandBuffer, pDrawCommandBuffer->buffer(), 0, pDrawCommandBuffer->GetDrawCommandCount(), pDrawCommandBuffer->GetStride());
        vkCmdEndRenderPass(m_commandBuffer);
    }
    vkEndCommandBuffer(m_commandBuffer);
}
RenderPassGaussianSplats::~RenderPassGaussianSplats()
{
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr);
}

}  // namespace Muyo
