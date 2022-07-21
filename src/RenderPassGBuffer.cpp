#include "RenderPassGBuffer.h"

#include "Debug.h"
#include "DescriptorManager.h"
#include "PipelineStateBuilder.h"
#include "RenderResourceManager.h"
#include "VkRenderDevice.h"
#include "Geometry.h"
#include "SamplerManager.h"

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
    aAttachmentDesc[LIGHTING_OUTPUT].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Depth target
    aAttachmentDesc[GBUFFER_DEPTH] = desc;
    aAttachmentDesc[GBUFFER_DEPTH].format = VK_FORMAT_D32_SFLOAT;
    aAttachmentDesc[GBUFFER_DEPTH].finalLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL;

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
        subpass.pDepthStencilAttachment = nullptr;
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


    m_vRenderPasses.resize(1, VK_NULL_HANDLE);
    assert(vkCreateRenderPass(GetRenderDevice()->GetDevice(), &renderPassInfo,
                              nullptr, &m_vRenderPasses.back()) == VK_SUCCESS);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_vRenderPasses.back()),
                            VK_OBJECT_TYPE_RENDER_PASS, "Opaque Lighting");

}

RenderPassGBuffer::~RenderPassGBuffer()
{
    DestroyFramebuffer();
    vkDestroyRenderPass(GetRenderDevice()->GetDevice(), m_vRenderPasses.back(), nullptr);

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

void RenderPassGBuffer::SetGBufferImageViews(
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
    framebufferInfo.renderPass = m_vRenderPasses.back();
    framebufferInfo.attachmentCount = static_cast<uint32_t>(views.size());
    framebufferInfo.pAttachments = views.data();
    framebufferInfo.width = nWidth;
    framebufferInfo.height = nHeight;
    framebufferInfo.layers = 1;
    assert(vkCreateFramebuffer(GetRenderDevice()->GetDevice(), &framebufferInfo,
                               nullptr, &mFramebuffer) == VK_SUCCESS);
    mRenderArea = {nWidth, nHeight};
}

void RenderPassGBuffer::DestroyFramebuffer()
{
    vkDestroyFramebuffer(GetRenderDevice()->GetDevice(), mFramebuffer, nullptr);
}

void RenderPassGBuffer::RecordCommandBuffer(const std::vector<const Geometry*>& vpGeometries)
{
    VkCommandBufferBeginInfo beginInfo = {};

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    // assert(m_commandBuffer == VK_NULL_HANDLE &&
    //        "Command buffer has been created");

    m_commandBuffer = GetRenderDevice()->AllocateStaticPrimaryCommandbuffer();
    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
    {
        SCOPED_MARKER(m_commandBuffer, "Opaque Lighting Pass");
        // Build render pass
        RenderPassBeginInfoBuilder rpbiBuilder;
        std::vector<VkClearValue> vClearValeus(
            LightingAttachments::aClearValues.begin(),
            LightingAttachments::aClearValues.end());

        VkRenderPassBeginInfo renderPassBeginInfo =
            rpbiBuilder.setRenderArea(mRenderArea)
                .setRenderPass(m_vRenderPasses.back())
                .setFramebuffer(mFramebuffer)
                .setClearValues(vClearValeus)
                .Build();

        vkCmdBeginRenderPass(m_commandBuffer, &renderPassBeginInfo,
                             VK_SUBPASS_CONTENTS_INLINE);

        // Allocate perview constant buffer
        const UniformBuffer<PerViewData>* perView =
            GetRenderResourceManager()->getUniformBuffer<PerViewData>("perView");

        VkDescriptorSet perViewSets =
            GetDescriptorManager()->AllocatePerviewDataDescriptorSet(*perView);

        {
            SCOPED_MARKER(m_commandBuffer, "GBuffer Pass");
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
                    const UniformBuffer<glm::mat4>* worldMatrixBuffer = pGeometry->GetWorldMatrixBuffer();
                    assert(worldMatrixBuffer != nullptr && "Buffer must be valid");
                    VkDescriptorSet worldMatrixDescSet = GetDescriptorManager()->AllocateUniformBufferDescriptorSet(*worldMatrixBuffer, 0, GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_PER_OBJ_DATA));

                    std::vector<VkDescriptorSet> vGBufferDescSets = {perViewSets,
                                                                     materialDescSet,
                                                                     worldMatrixDescSet};
                    VkDeviceSize offset = 0;
                    VkBuffer vertexBuffer = pPrimitive->getVertexDeviceBuffer();
                    VkBuffer indexBuffer = pPrimitive->getIndexDeviceBuffer();
                    uint32_t nIndexCount = pPrimitive->getIndexCount();
                    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &vertexBuffer,
                                           &offset);
                    vkCmdBindIndexBuffer(m_commandBuffer, indexBuffer, 0,
                                         VK_INDEX_TYPE_UINT32);
                    vkCmdBindPipeline(m_commandBuffer,
                                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      mGBufferPipeline);
                    vkCmdBindDescriptorSets(
                        m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        mGBufferPipelineLayout, 0, vGBufferDescSets.size(),
                        vGBufferDescSets.data(), 0, nullptr);
                    vkCmdDrawIndexed(m_commandBuffer, nIndexCount, 1, 0, 0, 0);
                }
            }
        }
        vkCmdNextSubpass(m_commandBuffer, VK_SUBPASS_CONTENTS_INLINE);
        {
            SCOPED_MARKER(m_commandBuffer, "Lighting Pass");
            // Create gbuffer render target views
            GBufferViews vGBufferRTViews = {
                GetRenderResourceManager()
                    ->GetColorTarget("GBUFFER_POSITION_AO", mRenderArea)
                    ->getView(),

                GetRenderResourceManager()
                    ->GetColorTarget("GBUFFER_ALBEDO_TRANSMITTANCE",
                                     mRenderArea)
                    ->getView(),

                GetRenderResourceManager()
                    ->GetColorTarget("GBUFFER_NORMAL_ROUGHNESS", mRenderArea)
                    ->getView(),

                GetRenderResourceManager()
                    ->GetColorTarget("GBUFFER_METALNESS_TRANSLUCENCY",
                                     mRenderArea)
                    ->getView()};

            const StorageBuffer<LightData>* lightDataBuffer = GetRenderResourceManager()->GetResource<StorageBuffer<LightData>>("light data");
            std::vector<VkDescriptorSet> lightingDescSets = {
                perViewSets,
                // GBuffer descriptor sets
                GetDescriptorManager()->AllocateGBufferDescriptorSet(
                    vGBufferRTViews),
                // IBL descriptor sets
                GetDescriptorManager()->AllocateIBLDescriptorSet(
                    GetRenderResourceManager()
                        ->GetColorTarget("irr_cube_map", {0, 0}, VK_FORMAT_B8G8R8A8_UNORM, 1, 6)
                        ->getView(),
                    GetRenderResourceManager()
                        ->GetColorTarget("prefiltered_cubemap", {0, 0}, VK_FORMAT_B8G8R8A8_UNORM, 1, 6)
                        ->getView(),
                    GetRenderResourceManager()
                        ->GetColorTarget("specular_brdf_lut", {0, 0}, VK_FORMAT_R32G32_SFLOAT, 1, 1)
                        ->getView()),
                // Light data buffer
                GetDescriptorManager()->AllocateLightDataDescriptorSet(lightDataBuffer->GetNumStructs(), *lightDataBuffer),

            };

            // Hack: Another hack to test if we have shadow map. No pipeline layout if no Finalize called
            if (m_renderPassParameters.GetPipelineLayout() != VK_NULL_HANDLE)
            {
                lightingDescSets.push_back(m_renderPassParameters.AllocateDescriptorSet("shadow map desc", m_nShadowMapDescriptorSetIndex));
            }

            const auto& prim = GetGeometryManager()->GetQuad()->getPrimitives().at(0);
            VkDeviceSize offset = 0;
            VkBuffer vertexBuffer = prim->getVertexDeviceBuffer();
            VkBuffer indexBuffer = prim->getIndexDeviceBuffer();
            uint32_t nIndexCount = prim->getIndexCount();

            vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &vertexBuffer,
                                   &offset);
            vkCmdBindIndexBuffer(m_commandBuffer, indexBuffer, 0,
                                 VK_INDEX_TYPE_UINT32);
            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              mLightingPipeline);
            vkCmdBindDescriptorSets(
                m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                mLightingPipelineLayout, 0, lightingDescSets.size(),
                lightingDescSets.data(), 0, nullptr);
            vkCmdDrawIndexed(m_commandBuffer, nIndexCount, 1, 0, 0, 0);
        }
        vkCmdEndRenderPass(m_commandBuffer);
    }
    vkEndCommandBuffer(m_commandBuffer);
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_commandBuffer),
                            VK_OBJECT_TYPE_COMMAND_BUFFER, "OpaqueLighting");
}

void RenderPassGBuffer::createGBufferViews(VkExtent2D size)
{
    std::array<VkImageView, LightingAttachments::ATTACHMENTS_COUNT> views;
    // Color attachments
    for (int i = 0; i < LightingAttachments::COLOR_ATTACHMENTS_COUNT; i++)
    {
        views[i] = GetRenderResourceManager()
                       ->GetColorTarget(mAttachments.aNames[i], size,
                                        mAttachments.aFormats[i])
                       ->getView();
    }

    // Depth attachments
    for (int i = LightingAttachments::COLOR_ATTACHMENTS_COUNT;
         i < LightingAttachments::ATTACHMENTS_COUNT; i++)
    {
        views[i] = GetRenderResourceManager()
                       ->GetDepthTarget(mAttachments.aNames[i], size,
                                        mAttachments.aFormats[i])
                       ->getView();
    }

    SetGBufferImageViews(views[0], views[1], views[2], views[3], views[4],
                         views[5], size.width, size.height);
}
void RenderPassGBuffer::removeGBufferViews()
{
    for (int i = 0; i < LightingAttachments::ATTACHMENTS_COUNT; i++)
    {
        GetRenderResourceManager()->RemoveResource(mAttachments.aNames[i]);
    }
    DestroyFramebuffer();
}

void RenderPassGBuffer::CreatePipeline(const std::vector<RenderTarget*> vpShadowMaps)
{
    // Pipeline should be created after mRenderArea been updated
    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(mRenderArea).Build();
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
                DESCRIPTOR_LAYOUT_MATERIALS),
            GetDescriptorManager()->getDescriptorLayout(
                DESCRIPTOR_LAYOUT_PER_OBJ_DATA)
        };

        std::vector<VkPushConstantRange> pushConstants;

        mGBufferPipelineLayout = GetRenderDevice()->CreatePipelineLayout(descLayouts, pushConstants);

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
                .setAssembly(iaBuilder.Build())
                .setViewport(viewport, scissorRect)
                .setRasterizer(rsBuilder.Build())
                .setMSAA(msBuilder.Build())
                .setColorBlending(blendBuilder.Build())
                .setPipelineLayout(mGBufferPipelineLayout)
                .setDepthStencil(depthStencilBuilder.Build())
                .setRenderPass(m_vRenderPasses.back())
                .setSubpassIndex(0)
                .Build(GetRenderDevice()->GetDevice());

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
            GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_PER_VIEW_DATA),  // MPV matrices
            GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_GBUFFER),        // GBuffer textures
            GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_IBL),            // Irradiance map
            GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_LIGHT_DATA),     // Irradiance map
        };

        if (vpShadowMaps.size() > 0)
        {
            // Hack: Create shadow map desc set on last descset with render pass parameters
            std::vector<const ImageResource*> vpImageResources;
            std::for_each(vpShadowMaps.begin(), vpShadowMaps.end(),
                          [&vpImageResources](const RenderTarget* pShadowMap)
                          {
                              vpImageResources.push_back(static_cast<const ImageResource*>(pShadowMap));
                          });

            // Create on 4th descriptor set
            m_renderPassParameters.AddImageParameter(vpImageResources, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, GetSamplerManager()->getSampler(SAMPLER_1_MIPS), m_nShadowMapDescriptorSetIndex);
            m_renderPassParameters.Finalize("GBuffer");

            descLayouts.push_back(m_renderPassParameters.GetDescriptorSetLayout(m_nShadowMapDescriptorSetIndex));  // shadow map desc set
        }

        std::vector<VkPushConstantRange> pushConstants;

        mLightingPipelineLayout = GetRenderDevice()->CreatePipelineLayout(descLayouts, pushConstants);

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
        blendBuilder.setAttachments(1, false);
        DepthStencilCIBuilder depthStencilBuilder;
        depthStencilBuilder.setDepthTestEnabled(false).setDepthWriteEnabled(false);

        PipelineStateBuilder builder;

        mLightingPipeline =
            builder.setShaderModules({vertShdr, fragShdr})
                .setVertextInfo({Vertex::getBindingDescription()},
                                Vertex::getAttributeDescriptions())
                .setAssembly(iaBuilder.Build())
                .setViewport(viewport, scissorRect)
                .setRasterizer(rsBuilder.Build())
                .setMSAA(msBuilder.Build())
                .setColorBlending(blendBuilder.Build())
                .setPipelineLayout(mLightingPipelineLayout)
                .setDepthStencil(depthStencilBuilder.Build())
                .setRenderPass(m_vRenderPasses.back())
                .setSubpassIndex(1)
                .Build(GetRenderDevice()->GetDevice());

        vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr,
                              nullptr);
        vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr,
                              nullptr);

        // Set debug name for the pipeline
        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(mGBufferPipeline),
                                VK_OBJECT_TYPE_PIPELINE, "Lighting");
    }
}
