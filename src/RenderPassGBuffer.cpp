#include "RenderPassGBuffer.h"
#include "VkRenderDevice.h"
#include "RenderResourceManager.h"
#include "Debug.h"

RenderPassGBuffer::RenderPassGBuffer()
{
    std::array<VkAttachmentReference,
               GBufferAttachments::COLOR_ATTACHMENTS_COUNT>
        aColorReferences;
    for (uint32_t i = 0; i < GBufferAttachments::COLOR_ATTACHMENTS_COUNT; i++)
    {
        aColorReferences[i] = {i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    }

    VkAttachmentReference depthReference = {
        GBufferAttachments::GBUFFER_DEPTH,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

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
    destroyFramebuffer();
    vkDestroyRenderPass(GetRenderDevice()->GetDevice(), m_renderPass, nullptr);
}

void RenderPassGBuffer::setGBufferImageViews(VkImageView positionView,
                                             VkImageView albedoView,
                                             VkImageView normalView,
                                             VkImageView uvView,
                                             VkImageView depthView,
                                             uint32_t nWidth, uint32_t nHeight)
{
    if (m_framebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(GetRenderDevice()->GetDevice(), m_framebuffer,
                             nullptr);
        m_framebuffer = VK_NULL_HANDLE;
    }
    std::array<VkImageView, GBufferAttachments::ATTACHMENTS_COUNT> views = {
        positionView, albedoView, normalView, uvView, depthView};
    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_renderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(views.size());
    framebufferInfo.pAttachments = views.data();
    framebufferInfo.width = nWidth;
    framebufferInfo.height = nHeight;
    framebufferInfo.layers = 1;
    assert(vkCreateFramebuffer(GetRenderDevice()->GetDevice(), &framebufferInfo,
                               nullptr, &m_framebuffer) == VK_SUCCESS);
}

void RenderPassGBuffer::destroyFramebuffer()
{
    vkDestroyFramebuffer(GetRenderDevice()->GetDevice(), m_framebuffer,
                         nullptr);
}

void RenderPassGBuffer::recordCommandBuffer(VkBuffer vertexBuffer,
                                            VkBuffer indexBuffer,
                                            uint32_t numIndices,
                                            VkPipeline pipeline,
                                            VkPipelineLayout pipelineLayout,
                                            VkDescriptorSet descriptorSet)
{
    VkCommandBufferBeginInfo beginInfo = {};

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    assert(m_commandBuffer == VK_NULL_HANDLE &&
           "Command buffer has been created");
    m_commandBuffer = GetRenderDevice()->allocatePrimaryCommandbuffer();
    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);

    VkRenderPassBeginInfo renderPassBeginInfo = {};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = m_renderPass;
    renderPassBeginInfo.framebuffer = m_framebuffer;

    renderPassBeginInfo.renderArea.offset = {0, 0};
    renderPassBeginInfo.renderArea.extent = mRenderArea;

    std::array<VkClearValue, GBufferAttachments::ATTACHMENTS_COUNT>
        clearValues = {};
    clearValues[GBufferAttachments::GBUFFER_POSITION].color = {
        {0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[GBufferAttachments::GBUFFER_ALBEDO].color = {
        {0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[GBufferAttachments::GBUFFER_NORMAL].color = {
        {0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[GBufferAttachments::GBUFFER_UV].color = {
        {0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[GBufferAttachments::GBUFFER_DEPTH].depthStencil = {1.0f, 0};

    renderPassBeginInfo.clearValueCount =
        static_cast<uint32_t>(clearValues.size());
    renderPassBeginInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(m_commandBuffer, &renderPassBeginInfo,
                         VK_SUBPASS_CONTENTS_INLINE);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &vertexBuffer, &offset);
    vkCmdBindIndexBuffer(m_commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline);
    vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    // vkCmdDraw(s_commandBuffers[i], 3, 1, 0, 0);
    vkCmdDrawIndexed(m_commandBuffer, numIndices, 1, 0, 0, 0);

    vkCmdEndRenderPass(m_commandBuffer);

    // Adding barrier to transit gbuffer targets to SRV
    //for (const auto& name  : m_attachments.aNames)
    //{
    //    VkImage image = GetRenderResourceManager()->getColorTarget(name, VkExtent2D())->getImage();
    //    VkImageMemoryBarrier barrier = {};
    //    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

    //    barrier.oldLayout =
    //        name == "GBUFFER_DEPTH"
    //            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    //            : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    //    barrier.newLayout =
    //        name == "GBUFFER_DEPTH"
    //            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    //            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    //    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    //    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    //    barrier.image = image;
    //    barrier.subresourceRange.baseMipLevel = 0;
    //    barrier.subresourceRange.levelCount = 1;
    //    barrier.subresourceRange.baseArrayLayer = 0;
    //    barrier.subresourceRange.layerCount = 1;

    //    barrier.subresourceRange.aspectMask = name == "GBUFFER_DEPTH"
    //                                              ? VK_IMAGE_ASPECT_DEPTH_BIT
    //                                              : VK_IMAGE_ASPECT_COLOR_BIT;

    //    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    //    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    //    vkCmdPipelineBarrier(
    //        m_commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
    //        /*srcStage*/ VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, /*dstStage*/
    //        0, 0, nullptr, 0, nullptr, 1, &barrier);
    //}

    vkEndCommandBuffer(m_commandBuffer);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_commandBuffer),
                            VK_OBJECT_TYPE_COMMAND_BUFFER, "[CB] GBuffer");
}

void RenderPassGBuffer::createGBufferViews(VkExtent2D size)
{
    std::array<VkImageView, GBufferAttachments::ATTACHMENTS_COUNT> views;
    // Color attachments
    for (int i = 0; i < GBufferAttachments::COLOR_ATTACHMENTS_COUNT; i++)
    {
        views[i] = GetRenderResourceManager()
                       ->getColorTarget(m_attachments.aNames[i], size,
                                        m_attachments.aFormats[i])
                       ->getView();
    }
    // Depth attachments
    for (int i = GBufferAttachments::COLOR_ATTACHMENTS_COUNT;
         i < GBufferAttachments::ATTACHMENTS_COUNT; i++)
    {
        views[i] = GetRenderResourceManager()
                       ->getDepthTarget(m_attachments.aNames[i], size,
                                        m_attachments.aFormats[i])
                       ->getView();
    }

    setGBufferImageViews(views[0], views[1], views[2], views[3], views[4],
                         size.width, size.height);
}
void RenderPassGBuffer::removeGBufferViews()
{
    for (int i = 0; i < GBufferAttachments::ATTACHMENTS_COUNT; i++)
    {
        GetRenderResourceManager()->removeResource(m_attachments.aNames[i]);
    }
    destroyFramebuffer();
}
