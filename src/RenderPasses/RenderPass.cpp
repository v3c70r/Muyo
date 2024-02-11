#include "RenderPass.h"

#include "Debug.h"
#include "DescriptorManager.h"
#include "Geometry.h"
#include "MeshResourceManager.h"
#include "PipelineStateBuilder.h"
#include "RenderResourceManager.h"
#include "ResourceBarrier.h"
#include "VkRenderDevice.h"

namespace Muyo
{

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
    DestroyFramebuffers();
    VkRenderPass& renderPass = (m_vRenderPasses.back());
    vkDestroyRenderPass(GetRenderDevice()->GetDevice(), renderPass, nullptr);
    m_vRenderPasses.clear();
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr);
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(), m_pipelineLayout, nullptr);
}

void RenderPassFinal::SetSwapchainImageViews(
    const std::vector<VkImageView>& vImageViews, VkImageView depthImageView,
    uint32_t nWidth, uint32_t nHeight)
{
    DestroyFramebuffers();
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
}

void RenderPassFinal::CreatePipeline()
{
    if (m_pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr);
        vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(), m_pipelineLayout, nullptr);
    }
    // Allocate pipeline layout
    std::vector<VkDescriptorSetLayout> descLayouts = {
        GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_SINGLE_SAMPLER),
#ifdef FEATURE_RAY_TRACING
        GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_SIGNLE_STORAGE_IMAGE),
        GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_PER_VIEW_DATA)
#endif
    };

    std::vector<VkPushConstantRange> pushConstants;
    m_pipelineLayout = GetRenderDevice()->CreatePipelineLayout(descLayouts, pushConstants);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipelineLayout),
                            VK_OBJECT_TYPE_PIPELINE_LAYOUT, "Final pass");

    // Allocate pipeline
    VkShaderModule vertShdr =
        CreateShaderModule(ReadSpv("shaders/triangle.vert.spv"));
#ifdef FEATURE_RAY_TRACING
    VkShaderModule fragShdr =
        CreateShaderModule(ReadSpv("shaders/triangle_rt.frag.spv"));
#else

    VkShaderModule fragShdr =
        CreateShaderModule(ReadSpv("shaders/triangle.frag.spv"));
#endif

    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(mRenderArea).Build();
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
    std::vector<VkDynamicState> vDynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    m_pipeline =
        builder.setShaderModules({vertShdr, fragShdr})
            .setVertextInfo({Vertex::getBindingDescription()},
                            Vertex::getAttributeDescriptions())
            .setAssembly(iaBuilder.Build())
            .setViewport(viewport, scissorRect)
            .setRasterizer(rsBuilder.Build())
            .setMSAA(msBuilder.Build())
            .setColorBlending(blendBuilder.Build())
            .setPipelineLayout(m_pipelineLayout)
            .setDepthStencil(depthStencilBuilder.Build())
            .setRenderPass(m_vRenderPasses.back())
            .setDynamicStates(vDynamicStates)
            .Build(GetRenderDevice()->GetDevice());

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr, nullptr);

    // Set debug name for the pipeline
    setDebugUtilsObjectName(
        reinterpret_cast<uint64_t>(m_pipeline),
        VK_OBJECT_TYPE_PIPELINE, "Final Pass");
}

void RenderPassFinal::RecordCommandBuffers()
{
    //VkImageView imgView = GetRenderResourceManager()->GetColorTarget("opaqueLightingOutput", VkExtent2D({0, 0}))->getView();
    auto* colorOutputResource = GetRenderResourceManager()->GetResource<RenderTarget>("opaqueLightingOutput");

#ifdef FEATURE_RAY_TRACING
    // VkImageView rtOutputView = GetRenderResourceManager()->GetStorageImageResource("Ray Tracing Output", VkExtent2D({1, 1}), VK_FORMAT_R16G16B16A16_SFLOAT)->getView();
    ImageResource* pRTOutput = GetRenderResourceManager()->GetResource<ImageResource>("Ray Tracing Output");
    assert(pRTOutput);
    VkImageView rtOutputView = pRTOutput->getView();
    UniformBuffer<PerViewData>* perView = GetRenderResourceManager()->GetResource<UniformBuffer<PerViewData>>("perView");

    std::vector<VkDescriptorSet>
        descSets = {
            GetDescriptorManager()->AllocateSingleSamplerDescriptorSet(colorOutputResource->getView()),
            GetDescriptorManager()->AllocateSingleStorageImageDescriptorSet(rtOutputView),
            GetDescriptorManager()->AllocatePerviewDataDescriptorSet(*perView)};
#else
    std::vector<VkDescriptorSet> descSets = {
        GetDescriptorManager()->AllocateSingleSamplerDescriptorSet(colorOutputResource->getView())};
#endif
    VkCommandBufferBeginInfo beginInfo = {};

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    for (size_t i = 0; i < m_vFramebuffers.size(); i++)
    {
        VkCommandBuffer curCmdBuf = VK_NULL_HANDLE;
        if (m_vCommandBuffers.size() <= i)
        {
            curCmdBuf = GetRenderDevice()->AllocateStaticPrimaryCommandbuffer();
            m_vCommandBuffers.push_back(curCmdBuf);
        }
        else
        {
            vkResetCommandBuffer(m_vCommandBuffers[i], VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
            curCmdBuf = m_vCommandBuffers[i];
        }

        vkBeginCommandBuffer(curCmdBuf, &beginInfo);

        // barrier to transit opaque lighting image
        ImageResourceBarrier colroOutputBarrier(colorOutputResource->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        colroOutputBarrier.AddToCommandBuffer(curCmdBuf);

        // Set dynamic viewport and scissor
        VkViewport viewport = {0.0, 0.0, (float)mRenderArea.width, (float)mRenderArea.height, 0.0, 1.0};
        vkCmdSetViewport(curCmdBuf, 0, 1, &viewport);
        VkRect2D scissor = {{0, 0}, {mRenderArea.width, mRenderArea.height}};
        vkCmdSetScissor(curCmdBuf, 0, 1, &scissor);

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

        {
            const Mesh& quadMesh = GetMeshResourceManager()->GetQuad();
            const MeshVertexResources& meshVertexResources = GetMeshResourceManager()->GetMeshVertexResources();
            VkDeviceSize offset = 0;
            VkBuffer vertexBuffer = meshVertexResources.m_pVertexBuffer->buffer();
            VkBuffer indexBuffer = meshVertexResources.m_pIndexBuffer->buffer();
            uint32_t nIndexCount = quadMesh.m_nIndexCount;
            uint32_t nIndexOffset = quadMesh.m_nIndexOffset;

            vkCmdBindVertexBuffers(curCmdBuf, 0, 1, &vertexBuffer, &offset);
            vkCmdBindIndexBuffer(curCmdBuf, indexBuffer, 0,
                                 VK_INDEX_TYPE_UINT32);
            vkCmdBindPipeline(curCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              m_pipeline);
            vkCmdBindDescriptorSets(curCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_pipelineLayout, 0,
                                    static_cast<uint32_t>(descSets.size()), descSets.data(), 0,
                                    nullptr);
            vkCmdDrawIndexed(curCmdBuf, nIndexCount, 1, nIndexOffset, 0, 0);
        }

        // vkCmdDraw(s_commandBuffers[i], 3, 1, 0, 0);

        vkCmdEndRenderPass(curCmdBuf);
        vkEndCommandBuffer(curCmdBuf);

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(curCmdBuf),
                                VK_OBJECT_TYPE_COMMAND_BUFFER, "[CB] Final");
    }
}

void RenderPassFinal::Resize(const std::vector<VkImageView>& vImageViews, VkImageView depthImageView,
                             uint32_t uWidth, uint32_t uHeight)
{
    mRenderArea = {uWidth, uHeight};
    SetSwapchainImageViews(vImageViews, depthImageView, uWidth, uHeight);
}

void RenderPassFinal::DestroyFramebuffers()
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

}  // namespace Muyo
