#include "RenderPassAO.h"

#include "Debug.h"
#include "DescriptorManager.h"
#include "PipelineStateBuilder.h"
#include "RenderResourceManager.h"
#include "ResourceBarrier.h"

namespace Muyo
{

RenderPassAO::RenderPassAO()
{
    CreatePipeline();
}
void RenderPassAO::CreatePipeline()
{
    // 1. Linearize depth
    std::vector<VkDescriptorSetLayout> vDescLayouts =
        {
            GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_SINGLE_SAMPLER),
            GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_SIGNLE_STORAGE_IMAGE),
        };

    std::vector<VkPushConstantRange> vPushConstants;
    m_pipelineLayout = GetRenderDevice()->CreatePipelineLayout(vDescLayouts, vPushConstants);

    // Shader module
    VkShaderModule linearizeDepthShdr = CreateShaderModule(ReadSpv("shaders/linearizeDepth.comp.spv"));
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(linearizeDepthShdr), VK_OBJECT_TYPE_SHADER_MODULE, "linearizeDepth.comp.spv");

    ComputePipelineBuilder builder;
    builder.AddShaderModule(linearizeDepthShdr).SetPipelineLayout(m_pipelineLayout);
    VkComputePipelineCreateInfo pipelineCreateInfo = builder.Build();
    VK_ASSERT(vkCreateComputePipelines(GetRenderDevice()->GetDevice(), VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_pipeline));

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), linearizeDepthShdr, nullptr);
}
void RenderPassAO::CreateRenderPass()
{
}

RenderPassAO::~RenderPassAO()
{
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(), m_pipelineLayout, nullptr);
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr);
    m_pipelineLayout = VK_NULL_HANDLE;
    m_pipeline = VK_NULL_HANDLE;
}

void RenderPassAO::RecordCommandBuffer()
{
    // Create descriptor sets
    RenderTarget* pDepthTarget = GetRenderResourceManager()->GetResource<RenderTarget>("GBufferDepth_");
    VkDescriptorSet inputImageDescSet = GetDescriptorManager()->AllocateSingleSamplerDescriptorSet(pDepthTarget->getView());
    VkExtent2D linearDepthImageSize = {800, 600};

    StorageImageResource* pLinearDepth = GetRenderResourceManager()->GetStorageImageResource("linear_depth", linearDepthImageSize, VK_FORMAT_R16_SFLOAT);

    VkDescriptorSet outputImageDescSet = GetDescriptorManager()->AllocateSingleStorageImageDescriptorSet(pLinearDepth->getView());

    std::vector<VkDescriptorSet> vDescSets = {
        inputImageDescSet, outputImageDescSet};

    if (m_cmdBuf == VK_NULL_HANDLE)
    {
        m_cmdBuf = GetRenderDevice()->AllocateComputeCommandBuffer();
    }
    else
    {
        vkResetCommandBuffer(m_cmdBuf, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
    }

    VkCommandBufferBeginInfo beginInfo =
        {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            nullptr,
            VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
            nullptr};

    // vkQueueWaitIdle(GetRenderDevice()->GetComputeQueue());
    VK_ASSERT(vkBeginCommandBuffer(m_cmdBuf, &beginInfo));
    {
        SCOPED_MARKER(m_cmdBuf, "AO");

        ImageResourceBarrier barrier(pLinearDepth->getImage(), VK_IMAGE_LAYOUT_GENERAL);
        GetRenderDevice()->AddResourceBarrier(m_cmdBuf, barrier);

        vkCmdBindPipeline(m_cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
        vkCmdBindDescriptorSets(m_cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, (uint32_t)vDescSets.size(), vDescSets.data(), 0, nullptr);
        vkCmdDispatch(m_cmdBuf, 800, 600, 1);
    }
    vkEndCommandBuffer(m_cmdBuf);
}

}  // namespace Muyo
