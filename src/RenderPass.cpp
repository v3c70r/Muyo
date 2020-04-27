#include "RenderPass.h"
#include "VkRenderDevice.h"
RenderPassFinal::RenderPassFinal(VkFormat swapChainFormat)
{
    // Attachments
    //
    // Color Attachements
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = swapChainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;

    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;  // Clear on load
    colorAttachment.storeOp =
        VK_ATTACHMENT_STORE_OP_STORE;  // Store in the memory to read back
                                       // later

    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;

    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Depth attachments
    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // attachment reference
    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment =
        0;  // indicates the attachemnt index in the attachments array
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Subpass
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;  // the index is the
                                                      // output from the
                                                      // fragment shader
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // subpass deps
    VkSubpassDependency subpassDep = {};
    subpassDep.srcSubpass = VK_SUBPASS_EXTERNAL;
    subpassDep.dstSubpass = 0;
    subpassDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDep.srcAccessMask = 0;
    subpassDep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    // Render pass
    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment,
                                                          depthAttachment};
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &subpassDep;

    assert(vkCreateRenderPass(GetRenderDevice()->GetDevice(), &renderPassInfo,
                              nullptr, &m_renderPass) == VK_SUCCESS);
}

RenderPassFinal::~RenderPassFinal()
{
    ClearFramebuffers();
    vkDestroyRenderPass(GetRenderDevice()->GetDevice(), m_renderPass, nullptr);
}

RenderPassUI::RenderPassUI()
{
    // Create render pass with 1 color attachment
    // Color Attachements
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_SRGB;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;

    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;  // Clear on load
    colorAttachment.storeOp =
        VK_ATTACHMENT_STORE_OP_STORE;  // Store in the memory to read back
                                       // later

    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;

    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // attachment reference
    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment =
        0;  // indicates the attachemnt index in the attachments array
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Subpass
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;  // the index is the
                                                      // output from the
                                                      // fragment shader
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // subpass deps
    VkSubpassDependency subpassDep = {};
    subpassDep.srcSubpass = VK_SUBPASS_EXTERNAL;
    subpassDep.dstSubpass = 0;
    subpassDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDep.srcAccessMask = 0;
    subpassDep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    // Render pass
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &subpassDep;

    assert(vkCreateRenderPass(GetRenderDevice()->GetDevice(), &renderPassInfo,
                              nullptr, &m_renderPass) == VK_SUCCESS);
}

void RenderPassFinal::SetSwapchainImageViews(
    std::vector<VkImageView>& vImageViews, VkImageView depthImageView,
    uint32_t nWidth, uint32_t nHeight)
{
    ClearFramebuffers();
    m_vFramebuffers.resize(vImageViews.size());
    for (size_t i = 0; i < vImageViews.size(); i++)
    {
        std::array<VkImageView, 2> attachmentViews = {vImageViews[i],
                                                      depthImageView};

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount =
            static_cast<uint32_t>(attachmentViews.size());
        framebufferInfo.pAttachments = attachmentViews.data();
        framebufferInfo.width = nWidth;
        framebufferInfo.height = nHeight;
        framebufferInfo.layers = 1;
        assert(vkCreateFramebuffer(GetRenderDevice()->GetDevice(),
                                   &framebufferInfo, nullptr,
                                   &m_vFramebuffers[i]) == VK_SUCCESS);
    }
    mRenderArea = {nWidth, nHeight};
}

void RenderPassFinal::RecordOnce(VkBuffer vertexBuffer, VkBuffer indexBuffer,
                             uint32_t numIndices,
                             //std::vector<VkCommandBuffer>& commandBuffers,
                             VkPipeline pipeline,
                             VkPipelineLayout pipelineLayout,
                             std::vector<VkDescriptorSet> vDescriptorSets)
{
    VkCommandBufferBeginInfo beginInfo = {};

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    for (size_t i = 0; i < m_vFramebuffers.size(); i++)
    {
        VkCommandBuffer curCmdBuf = GetRenderDevice()->allocatePrimaryCommandbuffer();
        vkBeginCommandBuffer(curCmdBuf, &beginInfo);

        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = m_renderPass;
        renderPassBeginInfo.framebuffer = m_vFramebuffers[i];

        renderPassBeginInfo.renderArea.offset = {0, 0};
        renderPassBeginInfo.renderArea.extent = mRenderArea;
        std::array<VkClearValue, 2> clearValues = {};
        clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassBeginInfo.clearValueCount =
            static_cast<uint32_t>(clearValues.size());
        renderPassBeginInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(curCmdBuf, &renderPassBeginInfo,
                             VK_SUBPASS_CONTENTS_INLINE);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(curCmdBuf, 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(curCmdBuf, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindPipeline(curCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(curCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout, 0, 1, &vDescriptorSets[i], 0,
                                nullptr);

        // vkCmdDraw(s_commandBuffers[i], 3, 1, 0, 0);
        vkCmdDrawIndexed(curCmdBuf, numIndices, 1, 0, 0, 0);

        vkCmdEndRenderPass(curCmdBuf);
        vkEndCommandBuffer(curCmdBuf);
        m_vCommandBuffers.push_back(curCmdBuf);
    }
}

void RenderPassFinal::ClearFramebuffers()
{
    for(auto& cmdBuf : m_vCommandBuffers)
    {
        GetRenderDevice()->freePrimaryCommandbuffer(cmdBuf);
    }
    for (auto& framebuffer : m_vFramebuffers)
    {
        if (framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(GetRenderDevice()->GetDevice(), framebuffer,
                                 nullptr);
        }
    }
    m_vFramebuffers.clear();
}

RenderPassUI::~RenderPassUI()
{
    if (m_framebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(GetRenderDevice()->GetDevice(), m_framebuffer,
                             nullptr);
    }
    vkDestroyRenderPass(GetRenderDevice()->GetDevice(), m_renderPass, nullptr);
}

void RenderPassUI::SetRenderTargetImageView(VkImageView targetView,
                                            uint32_t nWidth, uint32_t nHeight)
{
    if (m_framebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(GetRenderDevice()->GetDevice(), m_framebuffer,
                             nullptr);
    }
    // Create Frame buffer
    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_renderPass;
    framebufferInfo.width = nWidth;
    framebufferInfo.height = nHeight;
    framebufferInfo.layers = 1;
    assert(vkCreateFramebuffer(GetRenderDevice()->GetDevice(), &framebufferInfo,
                               nullptr, &m_framebuffer) == VK_SUCCESS);
}
