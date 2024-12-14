
#include "RenderPassGBufferMeshShader.h"
#include <vulkan/vulkan_core.h>
#include "Camera.h"
#include "PipelineStateBuilder.h"
#include "RenderResourceManager.h"
namespace Muyo
{
void RenderPassGBufferMeshShader::PrepareRenderPass()
{
    m_renderPassParameters.SetRenderArea(m_renderArea);

    RenderTarget* depthMap = GetRenderResourceManager()->GetDepthTarget("depthOnly", m_renderArea);
    m_renderPassParameters.AddAttachment(depthMap, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                         true);

    const UniformBuffer<PerViewData>* perView = GetRenderResourceManager()->GetUniformBuffer<PerViewData>("perView");
    m_renderPassParameters.AddParameter(perView, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_MESH_BIT_EXT);

    m_renderPassParameters.Finalize("Render pass mesh shader");
    CreatePipeline();
}

void RenderPassGBufferMeshShader::CreatePipeline()
{
    VkPipelineLayout pipelineLayout = m_renderPassParameters.GetPipelineLayout();

    VkShaderModule meshShader = CreateShaderModule(ReadSpv("shaders/meshShader.mesh.slang.spv"));
    VkShaderModule fragShader = CreateShaderModule(ReadSpv("shaders/meshShader.frag.slang.spv"));

    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(m_renderArea).Build();
    VkRect2D scissorRect;
    scissorRect.offset = {0, 0};
    scissorRect.extent = m_renderArea;

    RasterizationStateCIBuilder rsBuilder;
    MultisampleStateCIBuilder msBuilder;

    BlendStateCIBuilder blendBuilder;
    blendBuilder.setAttachments(1, false);
    DepthStencilCIBuilder depthStencilBuilder;
    PipelineStateBuilder builder;

    m_pipeline = builder.SetShaderModule(meshShader, VK_SHADER_STAGE_MESH_BIT_EXT)
                     .SetShaderModule(fragShader, VK_SHADER_STAGE_FRAGMENT_BIT)
                     .setViewport(viewport, scissorRect)
                     .setRasterizer(rsBuilder.Build())
                     .setMSAA(msBuilder.Build())
                     .setColorBlending(blendBuilder.Build())
                     .setPipelineLayout(pipelineLayout)
                     .setDepthStencil(depthStencilBuilder.Build())
                     .setRenderPass(m_renderPassParameters.GetRenderPass())
                     .Build(GetRenderDevice()->GetDevice());

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), meshShader, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShader, nullptr);
}

void RenderPassGBufferMeshShader::RecordCommandBuffers()
{
    m_commandBuffer = GetRenderDevice()->AllocateStaticPrimaryCommandbuffer();
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;
    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
    {
        SCOPED_MARKER(m_commandBuffer, "mesh shader pass");

        VkClearValue clearValue;
        clearValue.depthStencil = {1.0, 0};
        std::vector<VkClearValue> clearValues{clearValue};

        RenderPassBeginInfoBuilder rpBuilder;
        VkRenderPassBeginInfo rpBeginInfo = rpBuilder.setRenderPass(m_renderPassParameters.GetRenderPass())
                                                .setFramebuffer(m_renderPassParameters.GetFramebuffer())
                                                .setRenderArea(m_renderArea)
                                                .setClearValues({clearValues})
                                                .Build();
        vkCmdBeginRenderPass(m_commandBuffer, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        {
            std::vector<VkDescriptorSet> vDescSets = {
                m_renderPassParameters.AllocateDescriptorSet("", 0)
            };
            vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_renderPassParameters.GetPipelineLayout(), 0,
                                    static_cast<uint32_t>(vDescSets.size()), vDescSets.data(), 0, nullptr);

            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
            VkExt::vkCmdDrawMeshTasksEXT(m_commandBuffer, 3, 1, 1);
        }
        vkCmdEndRenderPass(m_commandBuffer);
    }
    vkEndCommandBuffer(m_commandBuffer);
}

RenderPassGBufferMeshShader::~RenderPassGBufferMeshShader()
{
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr);
}
}  // namespace Muyo

