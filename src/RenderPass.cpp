#include "RenderPass.h"

#include "Debug.h"
#include "DescriptorManager.h"
#include "Geometry.h"
#include "PipelineStateBuilder.h"
#include "RenderResourceManager.h"
#include "VkRenderDevice.h"

RenderPassFinal::RenderPassFinal(VkFormat swapChainFormat,
                                 bool bClearAttachments)
{
    // Attachments
    //
    // Color Attachements
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = swapChainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;

    colorAttachment.loadOp = bClearAttachments
                                 ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                 : VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp =
        VK_ATTACHMENT_STORE_OP_STORE;  // Store in the memory to read back
                                       // later
    colorAttachment.stencilLoadOp = bClearAttachments
                                        ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                        : VK_ATTACHMENT_LOAD_OP_LOAD;

    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;

    colorAttachment.initialLayout = bClearAttachments
                                        ? VK_IMAGE_LAYOUT_UNDEFINED
                                        : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Depth attachments
    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = bClearAttachments ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                               : VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = bClearAttachments
                                        ? VK_IMAGE_LAYOUT_UNDEFINED
                                        : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
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

    m_vRenderPasses.resize(1, VK_NULL_HANDLE);
    VkRenderPass* pRenderPss = &(m_vRenderPasses.back());
    assert(vkCreateRenderPass(GetRenderDevice()->GetDevice(), &renderPassInfo,
                              nullptr, pRenderPss) == VK_SUCCESS);
}

RenderPassFinal::~RenderPassFinal()
{
    destroyFramebuffers();
    VkRenderPass& renderPass = (m_vRenderPasses.back());
    vkDestroyRenderPass(GetRenderDevice()->GetDevice(), renderPass, nullptr);
    m_vRenderPasses.clear();
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr);
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(), m_pipelineLayout, nullptr);
}

void RenderPassFinal::setSwapchainImageViews(
    std::vector<VkImageView>& vImageViews, VkImageView depthImageView,
    uint32_t nWidth, uint32_t nHeight)
{
    destroyFramebuffers();
    m_vFramebuffers.resize(vImageViews.size());
    for (size_t i = 0; i < vImageViews.size(); i++)
    {
        std::array<VkImageView, 2> attachmentViews = {vImageViews[i],
                                                      depthImageView};

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_vRenderPasses.back();
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
    setupPipeline();
}

void RenderPassFinal::setupPipeline()
{
    if (m_pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr);
        vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(),
                                m_pipelineLayout, nullptr);
    }
    // Allocate pipeline layout
    std::vector<VkDescriptorSetLayout> descLayouts = {
        GetDescriptorManager()->getDescriptorLayout(
            DESCRIPTOR_LAYOUT_SINGLE_SAMPLER)};

    std::vector<VkPushConstantRange> pushConstants;
    m_pipelineLayout =
        PipelineManager::CreatePipelineLayout(descLayouts, pushConstants);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipelineLayout),
                            VK_OBJECT_TYPE_PIPELINE_LAYOUT, "Final pass");

    // Allocate pipeline
    VkShaderModule vertShdr =
        CreateShaderModule(ReadSpv("shaders/triangle.vert.spv"));

    VkShaderModule fragShdr =
        CreateShaderModule(ReadSpv("shaders/triangle.frag.spv"));

    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(mRenderArea).build();
    VkRect2D scissorRect;
    scissorRect.offset = {0, 0};
    scissorRect.extent = mRenderArea;

    InputAssemblyStateCIBuilder iaBuilder;
    RasterizationStateCIBuilder rsBuilder;
    MultisampleStateCIBuilder msBuilder;
    BlendStateCIBuilder blendBuilder;
    blendBuilder.setAttachments(1);
    DepthStencilCIBuilder depthStencilBuilder;
    PipelineStateBuilder builder;

    m_pipeline =
        builder.setShaderModules({vertShdr, fragShdr})
            .setVertextInfo({Vertex::getBindingDescription()},
                            Vertex::getAttributeDescriptions())
            .setAssembly(iaBuilder.build())
            .setViewport(viewport, scissorRect)
            .setRasterizer(rsBuilder.build())
            .setMSAA(msBuilder.build())
            .setColorBlending(blendBuilder.build())
            .setPipelineLayout(m_pipelineLayout)
            .setDepthStencil(depthStencilBuilder.build())
            .setRenderPass(m_vRenderPasses.back())
            .build(GetRenderDevice()->GetDevice());

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr, nullptr);

    // Set debug name for the pipeline
    setDebugUtilsObjectName(
        reinterpret_cast<uint64_t>(m_pipeline),
        VK_OBJECT_TYPE_PIPELINE, "Final Pass");
}

void RecordCommandBuffers()
{
    
}

void RenderPassFinal::RecordCommandBuffers()
{
    VkImageView imgView = GetRenderResourceManager()
                              ->getColorTarget("LIGHTING_OUTPUT", VkExtent2D({0, 0}))
                              ->getView();
    Geometry* pQuad = GetGeometryManager()->GetQuad();

    VkDescriptorSet descSet = GetDescriptorManager()->AllocateSingleSamplerDescriptorSet(imgView);
    VkCommandBufferBeginInfo beginInfo = {};

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    for (size_t i = 0; i < m_vFramebuffers.size(); i++)
    {
        VkCommandBuffer curCmdBuf =
            GetRenderDevice()->AllocateStaticPrimaryCommandbuffer();
        vkBeginCommandBuffer(curCmdBuf, &beginInfo);

        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = m_vRenderPasses.back();
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

        for (const auto& prim : pQuad->getPrimitives())
        {
            VkDeviceSize offset = 0;
            VkBuffer vertexBuffer = prim->getVertexDeviceBuffer();
            VkBuffer indexBuffer = prim->getIndexDeviceBuffer();
            uint32_t nIndexCount = prim->getIndexCount();
            vkCmdBindVertexBuffers(curCmdBuf, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(curCmdBuf, indexBuffer, 0,
                                 VK_INDEX_TYPE_UINT32);
            vkCmdBindPipeline(curCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              m_pipeline);
            vkCmdBindDescriptorSets(curCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_pipelineLayout, 0, 1, &descSet, 0,
                                    nullptr);
            vkCmdDrawIndexed(curCmdBuf, nIndexCount, 1, 0, 0, 0);
        }

        // vkCmdDraw(s_commandBuffers[i], 3, 1, 0, 0);

        vkCmdEndRenderPass(curCmdBuf);
        vkEndCommandBuffer(curCmdBuf);
        m_vCommandBuffers.push_back(curCmdBuf);

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(curCmdBuf),
                                VK_OBJECT_TYPE_COMMAND_BUFFER, "[CB] Final");
    }
}

void RenderPassFinal::destroyFramebuffers()
{
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

