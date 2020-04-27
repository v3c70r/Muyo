#include "RenderPassGBuffer.h"
#include "VkRenderDevice.h"

RenderPassGBuffer::RenderPassGBuffer()
{
    std::array<VkAttachmentReference,
               GBufferAttachments::COLOR_ATTACHMENTS_COUNT>
        aColorReferences;
    for (uint32_t i = 0; i < GBufferAttachments::COLOR_ATTACHMENTS_COUNT; i++)
    {
        aColorReferences[i] = {i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    }

    VkAttachmentReference depthReference = {GBufferAttachments::GBUFFER_DEPTH, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    // Subpass
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = aColorReferences.size();
    subpass.pColorAttachments = aColorReferences.data();  // the index is the
                                                          // output from the
                                                          // fragment shader
    subpass.pDepthStencilAttachment = &depthReference;

    // subpass deps
    VkSubpassDependency subpassDep = {};
    subpassDep.srcSubpass = VK_SUBPASS_EXTERNAL;
    subpassDep.dstSubpass = 0;
    subpassDep.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    subpassDep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDep.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    subpassDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(m_attachments.aAttachmentDesc.size());
    renderPassInfo.pAttachments = m_attachments.aAttachmentDesc.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &subpassDep;

    assert(vkCreateRenderPass(GetRenderDevice()->GetDevice(), &renderPassInfo,
                              nullptr, &m_renderPass) == VK_SUCCESS);

}


RenderPassGBuffer::~RenderPassGBuffer()
{
    vkDestroyRenderPass(GetRenderDevice()->GetDevice(), m_renderPass, nullptr);
}

void RenderPassGBuffer::RecordCommandBuffer(VkPipeline pipeline, VkPipelineLayout pipelineLayout)
{
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;
}
