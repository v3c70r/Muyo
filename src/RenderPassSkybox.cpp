#include "RenderPassSkybox.h"
#include "VkRenderDevice.h"
#include "RenderResourceManager.h"
#include "PipelineStateBuilder.h"
#include "DescriptorManager.h"
#include "Debug.h"
#include <cassert>

RenderPassSkybox::RenderPassSkybox()
{
    CreateRenderPass();
}

RenderPassSkybox::~RenderPassSkybox()
{
    DestroyRenderPass();
}

void RenderPassSkybox::CreateRenderPass()
{
    std::array<VkAttachmentDescription, 2> attDescs;
    {
        VkAttachmentDescription& attDesc = attDescs[0];
        attDesc = {};
        attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    {
        VkAttachmentDescription& attDesc = attDescs[1];
        attDesc = {};
        attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc.initialLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL;
        attDesc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL;
        attDesc.format = VK_FORMAT_D32_SFLOAT;
    }

    VkAttachmentReference colorAttachment = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthAttachment = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

   // Subpass
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachment;
    subpass.pDepthStencilAttachment = &depthAttachment;

   // Subpass dependency
    VkSubpassDependency subpassDep = {};
    subpassDep.srcSubpass = VK_SUBPASS_EXTERNAL;
    subpassDep.dstSubpass = 0;
    subpassDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDep.srcAccessMask = 0;
    subpassDep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &subpassDep;

    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attDescs.size());
    renderPassInfo.pAttachments = attDescs.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &subpassDep;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    assert(vkCreateRenderPass(GetRenderDevice()->GetDevice(), &renderPassInfo,
                              nullptr, &renderPass) == VK_SUCCESS);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(renderPass),
                            VK_OBJECT_TYPE_RENDER_PASS, "Skybox Pass");

    m_vRenderPasses.push_back(renderPass);
}

void RenderPassSkybox::DestroyRenderPass()
{
    for (auto& renderPass : m_vRenderPasses)
    {
        vkDestroyRenderPass(GetRenderDevice()->GetDevice(), renderPass, nullptr);
    }
    DestroyFramebuffer();
    DestroyPipeline();
}

void RenderPassSkybox::CreateFramebuffer(uint32_t uWidth, uint32_t uHeight)
{
    VkExtent2D vp = {uWidth, uHeight};

    std::array<VkImageView, 2> views = {
        GetRenderResourceManager()->GetColorTarget("LIGHTING_OUTPUT", vp)->getView(),
        GetRenderResourceManager()->GetDepthTarget("GBUFFER_DEPTH", vp)->getView()};

    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_vRenderPasses.back();
    framebufferInfo.attachmentCount = static_cast<uint32_t>(views.size());
    framebufferInfo.pAttachments = views.data();
    framebufferInfo.width = uWidth;
    framebufferInfo.height = uHeight;
    framebufferInfo.layers = 1;
    assert(vkCreateFramebuffer(GetRenderDevice()->GetDevice(), &framebufferInfo,
                               nullptr, &m_frameBuffer) == VK_SUCCESS);
    m_renderArea = vp;
}

void RenderPassSkybox::DestroyFramebuffer()
{
    vkDestroyFramebuffer(GetRenderDevice()->GetDevice(), m_frameBuffer, nullptr);
    m_frameBuffer = VK_NULL_HANDLE;
}

void RenderPassSkybox::CreatePipeline()
{
    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(m_renderArea).Build();
    VkRect2D scissorRect;
    scissorRect.offset = {0, 0};
    scissorRect.extent = m_renderArea;

    // Descriptor layouts
    std::vector<VkDescriptorSetLayout> descLayouts = {
        GetDescriptorManager()->getDescriptorLayout(
            DESCRIPTOR_LAYOUT_PER_VIEW_DATA),
        GetDescriptorManager()->getDescriptorLayout(
            DESCRIPTOR_LAYOUT_SINGLE_SAMPLER)};

    std::vector<VkPushConstantRange> pushConstants;

    m_pipelineLayout = GetRenderDevice()->CreatePipelineLayout(descLayouts, pushConstants);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipelineLayout), VK_OBJECT_TYPE_PIPELINE_LAYOUT, "Skybox");

    //TODO : Write a shader for transparent pass
    VkShaderModule vertShdr =
        CreateShaderModule(ReadSpv("shaders/Skybox.vert.spv"));
    VkShaderModule fragShdr =
        CreateShaderModule(ReadSpv("shaders/Skybox.frag.spv"));

    InputAssemblyStateCIBuilder iaBuilder;
    RasterizationStateCIBuilder rsBuilder;
    rsBuilder.SetCullMode(VK_CULL_MODE_NONE);
    MultisampleStateCIBuilder msBuilder;
    BlendStateCIBuilder blendBuilder;
    blendBuilder.setAttachments(1);

    // Disable depth write, enable depth test with opaque pass
    DepthStencilCIBuilder depthStencilBuilder;
    depthStencilBuilder.setDepthTestEnabled(true).setDepthWriteEnabled(false);

    PipelineStateBuilder builder;

    // TODO: Enable depth test, disable depth write
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
            .setSubpassIndex(0)
            .Build(GetRenderDevice()->GetDevice());

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr,
                          nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr,
                          nullptr);

    // Set debug name for the pipeline
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipeline),
                            VK_OBJECT_TYPE_PIPELINE, "Skybox");
}

void RenderPassSkybox::DestroyPipeline()
{
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(), m_pipelineLayout, nullptr);
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr);
    m_pipelineLayout = VK_NULL_HANDLE;
    m_pipeline = VK_NULL_HANDLE;
}

void RenderPassSkybox::RecordCommandBuffers()
{
    VkCommandBufferBeginInfo beginInfo = {};

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;
    if (m_vCommandBuffers.empty())
    {
        m_vCommandBuffers.push_back(GetRenderDevice()->AllocateStaticPrimaryCommandbuffer());
    }
    else
    {
        vkResetCommandBuffer(m_vCommandBuffers[0], VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
    }
    VkCommandBuffer& mCommandBuffer = m_vCommandBuffers[0];
    vkBeginCommandBuffer(mCommandBuffer, &beginInfo);
    {
        SCOPED_MARKER(mCommandBuffer, "Skybox Pass");
        // Build render pass
        RenderPassBeginInfoBuilder rpbiBuilder;
        std::vector<VkClearValue> vClearValeus = {{{.color = {0.0f, 0.0f, 0.0f, 0.0f}},
                                                   {.depthStencil = {1.0f, 0}}}};

        VkRenderPassBeginInfo renderPassBeginInfo =
            rpbiBuilder.setRenderArea(m_renderArea)
                .setRenderPass(m_vRenderPasses.back())
                .setFramebuffer(m_frameBuffer)
                .setClearValues(vClearValeus)
                .Build();

        vkCmdBeginRenderPass(mCommandBuffer, &renderPassBeginInfo,
                             VK_SUBPASS_CONTENTS_INLINE);

        // Allocate perview constant buffer
        const UniformBuffer<PerViewData>* perView =
            GetRenderResourceManager()->getUniformBuffer<PerViewData>("perView");

        std::vector<VkDescriptorSet> vDescSets = {
            GetDescriptorManager()->AllocatePerviewDataDescriptorSet(*perView),
            GetDescriptorManager()->AllocateSingleSamplerDescriptorSet(
                GetRenderResourceManager()
                    ->GetColorTarget("irr_cube_map", {0, 0},
                                     VK_FORMAT_B8G8R8A8_UNORM, 1, 6)
                    ->getView())};

        {
            // Draw skybox cube
            const auto& prim = GetGeometryManager()->GetCube()->getPrimitives().at(0);
            VkDeviceSize offset = 0;
            VkBuffer vertexBuffer = prim->getVertexDeviceBuffer();
            VkBuffer indexBuffer = prim->getIndexDeviceBuffer();
            uint32_t nIndexCount = prim->getIndexCount();

            vkCmdBindVertexBuffers(mCommandBuffer, 0, 1, &vertexBuffer,
                                   &offset);
            vkCmdBindIndexBuffer(mCommandBuffer, indexBuffer, 0,
                                 VK_INDEX_TYPE_UINT32);
            vkCmdBindPipeline(mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              m_pipeline);
            vkCmdBindDescriptorSets(
                mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_pipelineLayout, 0, (uint32_t)vDescSets.size(),
                vDescSets.data(), 0, nullptr);
            vkCmdDrawIndexed(mCommandBuffer, nIndexCount, 1, 0, 0, 0);
        }
        vkCmdEndRenderPass(mCommandBuffer);
    }
    vkEndCommandBuffer(mCommandBuffer);
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(mCommandBuffer),
                            VK_OBJECT_TYPE_COMMAND_BUFFER, "Skybox");
}

VkCommandBuffer RenderPassSkybox::GetCommandBuffer(size_t idx) const
{
    return m_vCommandBuffers.back();
}
