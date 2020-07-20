#include "RenderPassGBuffer.h"

#include "Debug.h"
#include "RenderResourceManager.h"
#include "VkRenderDevice.h"
#include "PipelineStateBuilder.h"

RenderPassGBuffer::LightingAttachments::LightingAttachments()
{
    // Construct color attachment descriptions
    VkAttachmentDescription desc = {};
    desc.samples = VK_SAMPLE_COUNT_1_BIT;
    desc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    desc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    aAttachmentDesc[GBUFFER_POSITION] = desc;
    aAttachmentDesc[GBUFFER_POSITION].format = VK_FORMAT_R16G16B16A16_SFLOAT;

    aAttachmentDesc[GBUFFER_ALBEDO] = desc;
    aAttachmentDesc[GBUFFER_ALBEDO].format = VK_FORMAT_R16G16B16A16_SFLOAT;

    aAttachmentDesc[GBUFFER_UV] = desc;
    aAttachmentDesc[GBUFFER_UV].format = VK_FORMAT_R16G16B16A16_SFLOAT;

    aAttachmentDesc[GBUFFER_NORMAL] = desc;
    aAttachmentDesc[GBUFFER_NORMAL].format = VK_FORMAT_R16G16B16A16_SFLOAT;

    aAttachmentDesc[LIGHTING_OUTPUT] = desc;
    aAttachmentDesc[LIGHTING_OUTPUT].format = VK_FORMAT_R16G16B16A16_SFLOAT;

    // Depth target
    aAttachmentDesc[GBUFFER_DEPTH] = desc;
    aAttachmentDesc[GBUFFER_DEPTH].format = VK_FORMAT_D32_SFLOAT;
    aAttachmentDesc[GBUFFER_DEPTH].finalLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
}

RenderPassGBuffer::RenderPassGBuffer()
{
    // Create two supasses, one for GBuffer and another for opaque lighting
    // Subpass
    constexpr uint32_t NUM_SUBPASSES = 2;
    std::array<VkSubpassDescription, NUM_SUBPASSES> aSubpasses = {{{}, {}}};
    // First subpass, render to gbuffer attachments
    {
        VkSubpassDescription& subpass = aSubpasses[0];
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount =
            m_attachments.aGBufferColorAttachmentRef.size();
        subpass.pColorAttachments =
            m_attachments.aGBufferColorAttachmentRef.data();
        subpass.pDepthStencilAttachment = &m_attachments.m_depthAttachment;
    }
    // Second subpass, render to lighting output
    {
        VkSubpassDescription& subpass = aSubpasses[1];
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount =
            m_attachments.aLightingColorAttachmentRef.size();
        subpass.pColorAttachments =
            m_attachments.aLightingColorAttachmentRef.data();
        subpass.pDepthStencilAttachment = &m_attachments.m_depthAttachment;
    }

    VkSubpassDescription &subpass = aSubpasses[0];

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
    renderPassInfo.attachmentCount =
        static_cast<uint32_t>(m_attachments.aAttachmentDesc.size());
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
                                             VkImageView lightingOutput,
                                             VkImageView depthView,
                                             uint32_t nWidth, uint32_t nHeight)
{
    if (m_framebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(GetRenderDevice()->GetDevice(), m_framebuffer,
                             nullptr);
        m_framebuffer = VK_NULL_HANDLE;
    }
    std::array<VkImageView, LightingAttachments::ATTACHMENTS_COUNT> views = {
        positionView, albedoView,     normalView,
        uvView,       lightingOutput, depthView};
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
    mRenderArea = {nWidth, nHeight};
}

void RenderPassGBuffer::destroyFramebuffer()
{
    vkDestroyFramebuffer(GetRenderDevice()->GetDevice(), m_framebuffer,
                         nullptr);
}

void RenderPassGBuffer::recordCommandBuffer(const PrimitiveList& primitives,
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
    m_commandBuffer = GetRenderDevice()->allocateStaticPrimaryCommandbuffer();
    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);

    RenderPassBeginInfoBuilder rpbiBuilder;

    std::vector<VkClearValue> vClearValeus(
        LightingAttachments::aClearValues.begin(),
        LightingAttachments::aClearValues.end());
    VkRenderPassBeginInfo renderPassBeginInfo =
        rpbiBuilder.setRenderArea(mRenderArea)
            .setRenderPass(m_renderPass)
            .setFramebuffer(m_framebuffer)
            .setClearValues(vClearValeus)
            .build();

    vkCmdBeginRenderPass(m_commandBuffer, &renderPassBeginInfo,
                         VK_SUBPASS_CONTENTS_INLINE);

    for (const auto& prim : primitives)
    {
        VkDeviceSize offset = 0;
        VkBuffer vertexBuffer = prim->getVertexDeviceBuffer();
        VkBuffer indexBuffer = prim->getIndexDeviceBuffer();
        uint32_t nIndexCount = prim->getIndexCount();
        vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(m_commandBuffer, indexBuffer, 0,
                             VK_INDEX_TYPE_UINT32);
        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline);
        vkCmdBindDescriptorSets(m_commandBuffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                0, 1, &descriptorSet, 0, nullptr);

        // vkCmdDraw(s_commandBuffers[i], 3, 1, 0, 0);
        vkCmdDrawIndexed(m_commandBuffer, nIndexCount, 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(m_commandBuffer);
    vkEndCommandBuffer(m_commandBuffer);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_commandBuffer),
                            VK_OBJECT_TYPE_COMMAND_BUFFER, "[CB] GBuffer");
}

void RenderPassGBuffer::createGBufferViews(VkExtent2D size)
{
    std::array<VkImageView, LightingAttachments::ATTACHMENTS_COUNT> views;
    // Color attachments
    for (int i = 0; i < LightingAttachments::COLOR_ATTACHMENTS_COUNT; i++)
    {
        views[i] = GetRenderResourceManager()
                       ->getColorTarget(m_attachments.aNames[i], size,
                                        m_attachments.aFormats[i])
                       ->getView();
    }
    
    // Depth attachments
    for (int i = LightingAttachments::COLOR_ATTACHMENTS_COUNT;
         i < LightingAttachments::ATTACHMENTS_COUNT; i++)
    {
        views[i] = GetRenderResourceManager()
                       ->getDepthTarget(m_attachments.aNames[i], size,
                                        m_attachments.aFormats[i])
                       ->getView();
    }

    setGBufferImageViews(views[0], views[1], views[2], views[3], views[4],
                         views[5], size.width, size.height);
}
void RenderPassGBuffer::removeGBufferViews()
{
    for (int i = 0; i < LightingAttachments::ATTACHMENTS_COUNT; i++)
    {
        GetRenderResourceManager()->removeResource(m_attachments.aNames[i]);
    }
    destroyFramebuffer();
}
