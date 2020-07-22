#include "RenderPassGBuffer.h"

#include "Debug.h"
#include "DescriptorManager.h"
#include "PipelineManager.h"
#include "PipelineStateBuilder.h"
#include "RenderResourceManager.h"
#include "VkRenderDevice.h"

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
            mAttachments.aGBufferColorAttachmentRef.size();
        subpass.pColorAttachments =
            mAttachments.aGBufferColorAttachmentRef.data();
        subpass.pDepthStencilAttachment = &mAttachments.m_depthAttachment;
    }
    // Second subpass, render to lighting output
    {
        VkSubpassDescription& subpass = aSubpasses[1];
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount =
            mAttachments.aLightingColorAttachmentRef.size();
        subpass.pColorAttachments =
            mAttachments.aLightingColorAttachmentRef.data();
        subpass.pDepthStencilAttachment = &mAttachments.m_depthAttachment;
    }

    VkSubpassDescription& subpass = aSubpasses[0];

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
        static_cast<uint32_t>(mAttachments.aAttachmentDesc.size());
    renderPassInfo.pAttachments = mAttachments.aAttachmentDesc.data();
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

void RenderPassGBuffer::setGBufferImageViews(
    VkImageView positionView, VkImageView albedoView, VkImageView normalView,
    VkImageView uvView, VkImageView lightingOutput, VkImageView depthView,
    uint32_t nWidth, uint32_t nHeight)
{
    if (mFramebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(GetRenderDevice()->GetDevice(), mFramebuffer,
                             nullptr);
        mFramebuffer = VK_NULL_HANDLE;
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
                               nullptr, &mFramebuffer) == VK_SUCCESS);
    mRenderArea = {nWidth, nHeight};
}

void RenderPassGBuffer::destroyFramebuffer()
{
    vkDestroyFramebuffer(GetRenderDevice()->GetDevice(), mFramebuffer, nullptr);
}

void RenderPassGBuffer::recordCommandBuffer(const PrimitiveList& primitives)
{
    VkCommandBufferBeginInfo beginInfo = {};

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    assert(mCommandBuffer == VK_NULL_HANDLE &&
           "Command buffer has been created");
    mCommandBuffer = GetRenderDevice()->allocateStaticPrimaryCommandbuffer();
    vkBeginCommandBuffer(mCommandBuffer, &beginInfo);

    RenderPassBeginInfoBuilder rpbiBuilder;

    std::vector<VkClearValue> vClearValeus(
        LightingAttachments::aClearValues.begin(),
        LightingAttachments::aClearValues.end());
    VkRenderPassBeginInfo renderPassBeginInfo =
        rpbiBuilder.setRenderArea(mRenderArea)
            .setRenderPass(m_renderPass)
            .setFramebuffer(mFramebuffer)
            .setClearValues(vClearValeus)
            .build();

    vkCmdBeginRenderPass(mCommandBuffer, &renderPassBeginInfo,
                         VK_SUBPASS_CONTENTS_INLINE);

    for (const auto& prim : primitives)
    {
        VkDeviceSize offset = 0;
        VkBuffer vertexBuffer = prim->getVertexDeviceBuffer();
        VkBuffer indexBuffer = prim->getIndexDeviceBuffer();
        uint32_t nIndexCount = prim->getIndexCount();
        vkCmdBindVertexBuffers(mCommandBuffer, 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(mCommandBuffer, indexBuffer, 0,
                             VK_INDEX_TYPE_UINT32);
        vkCmdBindPipeline(mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          mGBufferPipeline);
        vkCmdBindDescriptorSets(mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                mGBufferPipelineLayout, 0, 1, &mGBufferDescriptorSet, 0,
                                nullptr);

        // vkCmdDraw(s_commandBuffers[i], 3, 1, 0, 0);
        vkCmdDrawIndexed(mCommandBuffer, nIndexCount, 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(mCommandBuffer);
    vkEndCommandBuffer(mCommandBuffer);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(mCommandBuffer),
                            VK_OBJECT_TYPE_COMMAND_BUFFER, "[CB] GBuffer");
}

void RenderPassGBuffer::createGBufferViews(VkExtent2D size)
{
    std::array<VkImageView, LightingAttachments::ATTACHMENTS_COUNT> views;
    // Color attachments
    for (int i = 0; i < LightingAttachments::COLOR_ATTACHMENTS_COUNT; i++)
    {
        views[i] = GetRenderResourceManager()
                       ->getColorTarget(mAttachments.aNames[i], size,
                                        mAttachments.aFormats[i])
                       ->getView();
    }

    // Depth attachments
    for (int i = LightingAttachments::COLOR_ATTACHMENTS_COUNT;
         i < LightingAttachments::ATTACHMENTS_COUNT; i++)
    {
        views[i] = GetRenderResourceManager()
                       ->getDepthTarget(mAttachments.aNames[i], size,
                                        mAttachments.aFormats[i])
                       ->getView();
    }

    setGBufferImageViews(views[0], views[1], views[2], views[3], views[4],
                         views[5], size.width, size.height);
}
void RenderPassGBuffer::removeGBufferViews()
{
    for (int i = 0; i < LightingAttachments::ATTACHMENTS_COUNT; i++)
    {
        GetRenderResourceManager()->removeResource(mAttachments.aNames[i]);
    }
    destroyFramebuffer();
}

void RenderPassGBuffer::allocateDescriptorSets()
{
    const Material* pMaterial =
        GetMaterialManager()->m_mMaterials["plasticpattern"].get();
    Material::PBRViews views = {
        pMaterial->getImageView(Material::TEX_ALBEDO),
        pMaterial->getImageView(Material::TEX_NORMAL),
        pMaterial->getImageView(Material::TEX_METALNESS),
        pMaterial->getImageView(Material::TEX_ROUGHNESS),
        pMaterial->getImageView(Material::TEX_AO),
    };
    const UniformBuffer<PerViewData>* perView =
        GetRenderResourceManager()->getUniformBuffer<PerViewData>("perView");

    mGBufferDescriptorSet = GetDescriptorManager()->allocateGBufferDescriptorSet(*perView,
                                                         views);
}

void RenderPassGBuffer::createPipelines()
{
    // Create GBuffer Pipeline
    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(mRenderArea).build();
    VkRect2D scissorRect;
    scissorRect.offset = {0, 0};
    scissorRect.extent = mRenderArea;

    mGBufferPipelineLayout = PipelineManager::CreatePipelineLayout(
        GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_GBUFFER));

    VkShaderModule vertShdr =
        CreateShaderModule(ReadSpv("shaders/gbuffer.vert.spv"));
    VkShaderModule fragShdr =
        CreateShaderModule(ReadSpv("shaders/gbuffer.frag.spv"));
    PipelineStateBuilder builder;

    InputAssemblyStateCIBuilder iaBuilder;
    RasterizationStateCIBuilder rsBuilder;
    MultisampleStateCIBuilder msBuilder;
    BlendStateCIBuilder blendBuilder;
    blendBuilder.setAttachments(4);
    DepthStencilCIBuilder depthStencilBuilder;

    mGBufferPipeline = builder.setShaderModules({vertShdr, fragShdr})
                           .setVertextInfo({Vertex::getBindingDescription()},
                                           Vertex::getAttributeDescriptions())
                           .setAssembly(iaBuilder.build())
                           .setViewport(viewport, scissorRect)
                           .setRasterizer(rsBuilder.build())
                           .setMSAA(msBuilder.build())
                           .setColorBlending(blendBuilder.build())
                           .setPipelineLayout(mGBufferPipelineLayout)
                           .setDepthStencil(depthStencilBuilder.build())
                           .setRenderPass(m_renderPass)
                           .build(GetRenderDevice()->GetDevice());

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr, nullptr);

    // Set debug name for the pipeline
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(mGBufferPipeline),
                            VK_OBJECT_TYPE_PIPELINE, "GBuffer");
}
