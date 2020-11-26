#include "RenderPassGBuffer.h"

#include "Debug.h"
#include "DescriptorManager.h"
#include "PipelineStateBuilder.h"
#include "RenderResourceManager.h"
#include "VkRenderDevice.h"
#include "Geometry.h"

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
    desc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    aAttachmentDesc[GBUFFER_POSITION_AO] = desc;
    aAttachmentDesc[GBUFFER_POSITION_AO].format = VK_FORMAT_R16G16B16A16_SFLOAT;

    aAttachmentDesc[GBUFFER_ALBEDO_TRANSMITTANCE] = desc;
    aAttachmentDesc[GBUFFER_ALBEDO_TRANSMITTANCE].format = VK_FORMAT_R16G16B16A16_SFLOAT;

    aAttachmentDesc[GBUFFER_METALNESS_TRANSLUCENCY] = desc;
    aAttachmentDesc[GBUFFER_METALNESS_TRANSLUCENCY].format = VK_FORMAT_R16G16B16A16_SFLOAT;

    aAttachmentDesc[GBUFFER_NORMAL_ROUGHNESS] = desc;
    aAttachmentDesc[GBUFFER_NORMAL_ROUGHNESS].format = VK_FORMAT_R16G16B16A16_SFLOAT;

    aAttachmentDesc[LIGHTING_OUTPUT] = desc;
    aAttachmentDesc[LIGHTING_OUTPUT].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    aAttachmentDesc[LIGHTING_OUTPUT].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

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
        subpass.inputAttachmentCount =
            static_cast<uint32_t>(mAttachments.aLightingInputRef.size());
        subpass.pInputAttachments = mAttachments.aLightingInputRef.data();
    }

    // subpass deps
    VkSubpassDependency subpassDep = {};
    subpassDep.srcSubpass = 0;
    subpassDep.dstSubpass = 1;
    subpassDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    subpassDep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    subpassDep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    subpassDep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount =
        static_cast<uint32_t>(mAttachments.aAttachmentDesc.size());
    renderPassInfo.pAttachments = mAttachments.aAttachmentDesc.data();
    renderPassInfo.subpassCount = aSubpasses.size();
    renderPassInfo.pSubpasses = aSubpasses.data();
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &subpassDep;

    assert(vkCreateRenderPass(GetRenderDevice()->GetDevice(), &renderPassInfo,
                              nullptr, &m_renderPass) == VK_SUCCESS);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_renderPass),
                            VK_OBJECT_TYPE_RENDER_PASS, "Opaque Lighting");

    mpQuad = GetQuad();

}

RenderPassGBuffer::~RenderPassGBuffer()
{
    destroyFramebuffer();
    vkDestroyRenderPass(GetRenderDevice()->GetDevice(), m_renderPass, nullptr);

    // Destroy pipelines and pipeline layouts
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), mGBufferPipeline,
                      nullptr);
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), mLightingPipeline,
                      nullptr);
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(),
                            mGBufferPipelineLayout, nullptr);
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(),
                            mLightingPipelineLayout, nullptr);
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

void RenderPassGBuffer::recordCommandBuffer(const std::vector<const Geometry*>& vpGeometries)
{
    VkCommandBufferBeginInfo beginInfo = {};

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    assert(mCommandBuffer == VK_NULL_HANDLE &&
           "Command buffer has been created");
    mCommandBuffer = GetRenderDevice()->allocateStaticPrimaryCommandbuffer();
    vkBeginCommandBuffer(mCommandBuffer, &beginInfo);
    {
        SCOPED_MARKER(mCommandBuffer, "Opaque Lighting Pass");
        // Build render pass
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

        // Allocate perview constant buffer
        const UniformBuffer<PerViewData>* perView =
            GetRenderResourceManager()->getUniformBuffer<PerViewData>("perView");

        VkDescriptorSet perViewSets =
            GetDescriptorManager()->allocatePerviewDataDescriptorSet(*perView);

        // Allocate material descriptor set with default materials
        const Material* pDefaultMaterial =
            GetMaterialManager()->m_mMaterials["plasticpattern"].get();

        {
            // Handle Geometires
            for (const Geometry* pGeometry : vpGeometries)
            {
                // each geometry has their own transformation
                // TODO: Update perViewSets buffer data based on the geometries
                // transformation
                for (const auto& pPrimitive : pGeometry->getPrimitives())
                {
                    VkDescriptorSet materialDescSet = GetMaterialManager()->GetDefaultMaterial()->GetDescriptorSet();
                    if (pPrimitive->GetMaterial() != nullptr)
                    {
                        materialDescSet = pPrimitive->GetMaterial()->GetDescriptorSet();
                    }

                    std::vector<VkDescriptorSet> vGBufferDescSets = {perViewSets,
                                                                     materialDescSet};
                    VkDeviceSize offset = 0;
                    VkBuffer vertexBuffer = pPrimitive->getVertexDeviceBuffer();
                    VkBuffer indexBuffer = pPrimitive->getIndexDeviceBuffer();
                    uint32_t nIndexCount = pPrimitive->getIndexCount();
                    vkCmdBindVertexBuffers(mCommandBuffer, 0, 1, &vertexBuffer,
                                           &offset);
                    vkCmdBindIndexBuffer(mCommandBuffer, indexBuffer, 0,
                                         VK_INDEX_TYPE_UINT32);
                    vkCmdBindPipeline(mCommandBuffer,
                                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      mGBufferPipeline);
                    vkCmdBindDescriptorSets(
                        mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        mGBufferPipelineLayout, 0, vGBufferDescSets.size(),
                        vGBufferDescSets.data(), 0, nullptr);
                    vkCmdDrawIndexed(mCommandBuffer, nIndexCount, 1, 0, 0, 0);
                }
            }
        }
        vkCmdNextSubpass(mCommandBuffer, VK_SUBPASS_CONTENTS_INLINE);
        {
            // Create gbuffer render target views
            GBufferViews vGBufferRTViews = {
                GetRenderResourceManager()
                    ->getColorTarget("GBUFFER_POSITION_AO", mRenderArea)
                    ->getView(),

                GetRenderResourceManager()
                    ->getColorTarget("GBUFFER_ALBEDO_TRANSMITTANCE",
                                     mRenderArea)
                    ->getView(),

                GetRenderResourceManager()
                    ->getColorTarget("GBUFFER_NORMAL_ROUGHNESS", mRenderArea)
                    ->getView(),

                GetRenderResourceManager()
                    ->getColorTarget("GBUFFER_METALNESS_TRANSLUCENCY",
                                     mRenderArea)
                    ->getView()};

            std::vector<VkDescriptorSet> lightingDescSets = {
                perViewSets,
                GetDescriptorManager()->allocateGBufferDescriptorSet(
                    vGBufferRTViews),
                GetDescriptorManager()->allocateSingleSamplerDescriptorSet(
                    GetRenderResourceManager()
                        ->getColorTarget("irr_cube_map", {0, 0},
                                         VK_FORMAT_B8G8R8A8_UNORM, 1, 6)
                        ->getView())};
            SCOPED_MARKER(mCommandBuffer, "Lighting Pass");
            const Primitive* prim = mpQuad->getPrimitives().at(0).get();
            VkDeviceSize offset = 0;
            VkBuffer vertexBuffer = prim->getVertexDeviceBuffer();
            VkBuffer indexBuffer = prim->getIndexDeviceBuffer();
            uint32_t nIndexCount = prim->getIndexCount();

            vkCmdBindVertexBuffers(mCommandBuffer, 0, 1, &vertexBuffer,
                                   &offset);
            vkCmdBindIndexBuffer(mCommandBuffer, indexBuffer, 0,
                                 VK_INDEX_TYPE_UINT32);
            vkCmdBindPipeline(mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              mLightingPipeline);
            vkCmdBindDescriptorSets(
                mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                mLightingPipelineLayout, 0, lightingDescSets.size(),
                lightingDescSets.data(), 0, nullptr);
            vkCmdDrawIndexed(mCommandBuffer, nIndexCount, 1, 0, 0, 0);
        }
        vkCmdEndRenderPass(mCommandBuffer);
    }
    vkEndCommandBuffer(mCommandBuffer);
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(mCommandBuffer),
                            VK_OBJECT_TYPE_COMMAND_BUFFER, "OpaqueLighting");
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

void RenderPassGBuffer::createPipelines()
{
    // Pipeline should be created after mRenderArea been updated
    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(mRenderArea).build();
    VkRect2D scissorRect;
    scissorRect.offset = {0, 0};
    scissorRect.extent = mRenderArea;

    {
        // Create GBuffer Pipeline

        // Descriptor layouts
        std::vector<VkDescriptorSetLayout> descLayouts = {
            GetDescriptorManager()->getDescriptorLayout(
                DESCRIPTOR_LAYOUT_PER_VIEW_DATA),
            GetDescriptorManager()->getDescriptorLayout(
                DESCRIPTOR_LAYOUT_MATERIALS)};

        std::vector<VkPushConstantRange> pushConstants;

        mGBufferPipelineLayout =
            PipelineManager::CreatePipelineLayout(descLayouts, pushConstants);

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(mGBufferPipelineLayout),
                                VK_OBJECT_TYPE_PIPELINE_LAYOUT, "GBuffer");

        VkShaderModule vertShdr =
            CreateShaderModule(ReadSpv("shaders/gbuffer.vert.spv"));
        VkShaderModule fragShdr =
            CreateShaderModule(ReadSpv("shaders/gbuffer.frag.spv"));
        PipelineStateBuilder builder;

        InputAssemblyStateCIBuilder iaBuilder;
        RasterizationStateCIBuilder rsBuilder;
        MultisampleStateCIBuilder msBuilder;
        BlendStateCIBuilder blendBuilder;
        blendBuilder.setAttachments(LightingAttachments::GBUFFER_ATTACHMENTS_COUNT, false);
        DepthStencilCIBuilder depthStencilBuilder;

        mGBufferPipeline =
            builder.setShaderModules({vertShdr, fragShdr})
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
                .setSubpassIndex(0)
                .build(GetRenderDevice()->GetDevice());

        vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr,
                              nullptr);
        vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr,
                              nullptr);

        // Set debug name for the pipeline
        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(mGBufferPipeline),
                                VK_OBJECT_TYPE_PIPELINE, "GBuffer");
    }

    {
        // Create lighting pipeline
        std::vector<VkDescriptorSetLayout> descLayouts = {
            GetDescriptorManager()->getDescriptorLayout(
                DESCRIPTOR_LAYOUT_PER_VIEW_DATA),  // MPV matrices
            GetDescriptorManager()->getDescriptorLayout(
                DESCRIPTOR_LAYOUT_GBUFFER),  // GBuffer textures
            GetDescriptorManager()->getDescriptorLayout(
                DESCRIPTOR_LAYOUT_SINGLE_SAMPLER)  // Irradiance map
        };

        std::vector<VkPushConstantRange> pushConstants;

        mLightingPipelineLayout =
            PipelineManager::CreatePipelineLayout(descLayouts, pushConstants);

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(mLightingPipelineLayout),
                                VK_OBJECT_TYPE_PIPELINE_LAYOUT, "Lighting");

        VkShaderModule vertShdr =
            CreateShaderModule(ReadSpv("shaders/lighting.vert.spv"));
        VkShaderModule fragShdr =
            CreateShaderModule(ReadSpv("shaders/lighting.frag.spv"));

        InputAssemblyStateCIBuilder iaBuilder;
        RasterizationStateCIBuilder rsBuilder;
        MultisampleStateCIBuilder msBuilder;
        BlendStateCIBuilder blendBuilder;
        blendBuilder.setAttachments(1);
        DepthStencilCIBuilder depthStencilBuilder;

        PipelineStateBuilder builder;

        mLightingPipeline =
            builder.setShaderModules({vertShdr, fragShdr})
                .setVertextInfo({Vertex::getBindingDescription()},
                                Vertex::getAttributeDescriptions())
                .setAssembly(iaBuilder.build())
                .setViewport(viewport, scissorRect)
                .setRasterizer(rsBuilder.build())
                .setMSAA(msBuilder.build())
                .setColorBlending(blendBuilder.build())
                .setPipelineLayout(mLightingPipelineLayout)
                .setDepthStencil(depthStencilBuilder.build())
                .setRenderPass(m_renderPass)
                .setSubpassIndex(1)
                .build(GetRenderDevice()->GetDevice());

        vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr,
                              nullptr);
        vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr,
                              nullptr);

        // Set debug name for the pipeline
        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(mGBufferPipeline),
                                VK_OBJECT_TYPE_PIPELINE, "Lighting");
    }
}
