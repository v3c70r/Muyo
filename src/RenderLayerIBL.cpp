#include "RenderLayerIBL.h"

#include "DescriptorManager.h"
#include "PushConstantBlocks.h"
#include "RenderResourceManager.h"
#include "VkRenderDevice.h"
#include "PipelineStateBuilder.h"
#include "glm/gtc/matrix_transform.hpp"
void RenderLayerIBL::setupRenderPass()
{
    m_vRenderPasses.resize(RENDERPASS_COUNT);
    //1. Create two attachments, one for env cube map, another for diffuse irradiance map
    std::array<VkAttachmentDescription, RENDERPASS_COUNT> attachments;

    // 2 Create multiview info
    std::array<uint32_t, 1> viewMasks = {
        0b111111};
    VkRenderPassMultiviewCreateInfo multiViewCI = {};
    multiViewCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO;
    multiViewCI.subpassCount = static_cast<uint32_t>(viewMasks.size());
    multiViewCI.pViewMasks = viewMasks.data();

    // Create two render passes for equirectangle cube map and irradiance map

    for (uint32_t passIdx = 0; passIdx < RENDERPASS_COUNT; passIdx++)
    {
        // Create attachment
        VkAttachmentDescription &attDesc = attachments[passIdx];
        attDesc = {};
        attDesc.format = TEX_FORMAT;
        if (passIdx == RENDERPASS_COMPUTE_SPECULAR_BRDF_LUT)
        {
            attDesc.format = VK_FORMAT_R32G32_SFLOAT;
        }
        attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (passIdx == RENDERPASS_COMPUTE_PRE_FILTERED_CUBEMAP)
        {
            attDesc.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        }

        VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        // Subpass
        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &ref;

        // subpass deps
        VkSubpassDependency subpassDep = {};
        subpassDep.srcSubpass = VK_SUBPASS_EXTERNAL;
        subpassDep.dstSubpass = 0;
        subpassDep.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        subpassDep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        subpassDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        // Create the renderpass
        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &attachments[passIdx];
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &subpassDep;
        if (passIdx != RENDERPASS_COMPUTE_SPECULAR_BRDF_LUT)
        {
            renderPassInfo.pNext = &multiViewCI;
        }

        assert(vkCreateRenderPass(GetRenderDevice()->GetDevice(),
                                  &renderPassInfo, nullptr,
                                  &m_vRenderPasses[passIdx]) == VK_SUCCESS);

        setDebugUtilsObjectName(
            reinterpret_cast<uint64_t>(m_vRenderPasses[passIdx]),
            VK_OBJECT_TYPE_RENDER_PASS, m_aRenderPassNames[passIdx].c_str());
    }
}

void RenderLayerIBL::setupFramebuffer()
{
    std::array<VkImageView, RENDERPASS_COUNT> vImageViews = {
        GetRenderResourceManager()
            ->GetColorTarget("env_cube_map", {ENV_CUBE_DIM, ENV_CUBE_DIM},
                             TEX_FORMAT, 1, 6)
            ->getView(),
        GetRenderResourceManager()
            ->GetColorTarget("irr_cube_map", {IRR_CUBE_DIM, IRR_CUBE_DIM},
                             TEX_FORMAT, 1, 6)
            ->getView(),
        GetRenderResourceManager()
            ->GetColorTarget("prefiltered_cubemap_tmp", {PREFILTERED_CUBE_DIM, PREFILTERED_CUBE_DIM}, TEX_FORMAT, 1, NUM_FACES, VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            ->getView(),
        GetRenderResourceManager()
            ->GetColorTarget("specular_brdf_lut", {SPECULAR_BRDF_LUT_DIM, SPECULAR_BRDF_LUT_DIM}, VK_FORMAT_R32G32_SFLOAT, 1, 1)
            ->getView()};

    // Create framebuffers
    for (uint32_t passIdx = 0; passIdx < RENDERPASS_COUNT; passIdx++)
    {
        VkFramebufferCreateInfo frameBufferCreateInfo = {};
        frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        frameBufferCreateInfo.renderPass = m_vRenderPasses[passIdx];
        frameBufferCreateInfo.attachmentCount = 1;
        frameBufferCreateInfo.pAttachments = &vImageViews[passIdx];
        frameBufferCreateInfo.width = m_aCubemapSizes[passIdx];
        frameBufferCreateInfo.height = m_aCubemapSizes[passIdx];
        frameBufferCreateInfo.layers = 1;

        assert(vkCreateFramebuffer(GetRenderDevice()->GetDevice(),
                                   &frameBufferCreateInfo, nullptr,
                                   &m_aFramebuffers[passIdx]) == VK_SUCCESS);
        setDebugUtilsObjectName(
            reinterpret_cast<uint64_t>(m_aFramebuffers[passIdx]),
            VK_OBJECT_TYPE_FRAMEBUFFER, m_aRenderPassNames[passIdx].c_str());
    }
}

void RenderLayerIBL::CreatePipeline()
{
    // Create two pipelines for two subpasses
    // 1. Generate environment cube map from equirectangle hdr image
    {
        // Viewport
        ViewportBuilder vpBuilder;
        VkViewport viewport = vpBuilder.setWH(IRR_CUBE_DIM, IRR_CUBE_DIM).Build();

        // Scissor
        VkRect2D scissorRect;
        scissorRect.offset = {0, 0};
        scissorRect.extent = {IRR_CUBE_DIM, IRR_CUBE_DIM};

        // Input assembly
        InputAssemblyStateCIBuilder iaBuilder;
        // Rasterizer
        RasterizationStateCIBuilder rasterizerBuilder;
        rasterizerBuilder.SetFrontFace(VK_FRONT_FACE_COUNTER_CLOCKWISE);
        // MSAA
        MultisampleStateCIBuilder msBuilder;
        // Blend
        BlendStateCIBuilder blendStateBuilder;
        blendStateBuilder.setAttachments(1);
        // DS
        DepthStencilCIBuilder depthStencilBuilder;
        depthStencilBuilder.setDepthTestEnabled(false)
            .setDepthWriteEnabled(false)
            .setDepthCompareOp(VK_COMPARE_OP_LESS_OR_EQUAL);

        // Pipeline layout
        std::vector<VkDescriptorSetLayout> descLayouts = {
            GetDescriptorManager()->getDescriptorLayout(
                DESCRIPTOR_LAYOUT_PER_VIEW_DATA),
            GetDescriptorManager()->getDescriptorLayout(
                DESCRIPTOR_LAYOUT_SINGLE_SAMPLER)};

        std::vector<VkPushConstantRange> pushConstants;

        m_envCubeMapPipelineLayout =
            GetRenderDevice()->CreatePipelineLayout(descLayouts, pushConstants);

        // Dynmaic state
        std::vector<VkDynamicState> dynamicStateEnables = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

        VkShaderModule vertShdr = CreateShaderModule(
            ReadSpv("shaders/equirectangularToCubeMap.vert.spv"));
        VkShaderModule fragShdr = CreateShaderModule(
            ReadSpv("shaders/equirectangularToCubeMap.frag.spv"));
        PipelineStateBuilder builder;

        m_envCubeMapPipeline =
            builder.setShaderModules({vertShdr, fragShdr})
                .setVertextInfo({Vertex::getBindingDescription()},
                                Vertex::getAttributeDescriptions())
                .setAssembly(iaBuilder.Build())
                .setViewport(viewport, scissorRect)
                .setRasterizer(rasterizerBuilder.Build())
                .setMSAA(msBuilder.Build())
                .setColorBlending(blendStateBuilder.Build())
                .setPipelineLayout(m_envCubeMapPipelineLayout)
                .setDynamicStates(dynamicStateEnables)
                .setDepthStencil(depthStencilBuilder.Build())
                .setRenderPass(m_vRenderPasses[RENDERPASS_LOAD_ENV_MAP])
                .Build(GetRenderDevice()->GetDevice());

        vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr,
                              nullptr);
        vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr,
                              nullptr);

        // Set debug name for the pipeline
        setDebugUtilsObjectName(
            reinterpret_cast<uint64_t>(m_envCubeMapPipeline),
            VK_OBJECT_TYPE_PIPELINE, "EquirectangularMapToCubeMap");
    }
    // 2. Create the pipeline to generate irradiance map
    {
        // Viewport
        ViewportBuilder vpBuilder;
        VkViewport viewport =
            vpBuilder.setWH(IRR_CUBE_DIM, IRR_CUBE_DIM).Build();

        // Scissor
        VkRect2D scissorRect;
        scissorRect.offset = {0, 0};
        scissorRect.extent = {IRR_CUBE_DIM, IRR_CUBE_DIM};

        // Input assembly
        InputAssemblyStateCIBuilder iaBuilder;
        // Rasterizer
        RasterizationStateCIBuilder rasterizerBuilder;
        rasterizerBuilder.SetFrontFace(VK_FRONT_FACE_COUNTER_CLOCKWISE);
        // MSAA
        MultisampleStateCIBuilder msBuilder;
        // Blend
        BlendStateCIBuilder blendStateBuilder;
        blendStateBuilder.setAttachments(1);
        // DS
        DepthStencilCIBuilder depthStencilBuilder;
        depthStencilBuilder.setDepthTestEnabled(false)
            .setDepthWriteEnabled(false)
            .setDepthCompareOp(VK_COMPARE_OP_LESS_OR_EQUAL);

        // Pipeline layout
        std::vector<VkDescriptorSetLayout> descLayouts = {
            GetDescriptorManager()->getDescriptorLayout(
                DESCRIPTOR_LAYOUT_PER_VIEW_DATA),
            GetDescriptorManager()->getDescriptorLayout(
                DESCRIPTOR_LAYOUT_SINGLE_SAMPLER)};

        std::vector<VkPushConstantRange> pushConstants;

        m_irrCubeMapPipelineLayout =
            GetRenderDevice()->CreatePipelineLayout(descLayouts, pushConstants);

        // Dynmaic state
        std::vector<VkDynamicState> dynamicStateEnables = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

        VkShaderModule vertShdr = CreateShaderModule(
            ReadSpv("shaders/CubeMapToIrradianceMap.vert.spv"));
        VkShaderModule fragShdr = CreateShaderModule(
            ReadSpv("shaders/CubeMapToIrradianceMap.frag.spv"));
        PipelineStateBuilder builder;

        m_irrCubeMapPipeline =
            builder.setShaderModules({vertShdr, fragShdr})
                .setVertextInfo({Vertex::getBindingDescription()},
                                Vertex::getAttributeDescriptions())
                .setAssembly(iaBuilder.Build())
                .setViewport(viewport, scissorRect)
                .setRasterizer(rasterizerBuilder.Build())
                .setMSAA(msBuilder.Build())
                .setColorBlending(blendStateBuilder.Build())
                .setPipelineLayout(m_irrCubeMapPipelineLayout)
                .setDynamicStates(dynamicStateEnables)
                .setDepthStencil(depthStencilBuilder.Build())
                .setRenderPass(m_vRenderPasses[RENDERPASS_COMPUTE_IRR_CUBEMAP])
                .Build(GetRenderDevice()->GetDevice());

        vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr,
                              nullptr);
        vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr,
                              nullptr);

        // Set debug name for the pipeline
        setDebugUtilsObjectName(
            reinterpret_cast<uint64_t>(m_irrCubeMapPipeline),
            VK_OBJECT_TYPE_PIPELINE, "CubeMapToIrradianceMap");
    }

    // 3. Pipeline to generate pre-filtered HDR environment map
    {
        // Viewport
        ViewportBuilder vpBuilder;
        VkViewport viewport =
            vpBuilder.setWH(PREFILTERED_CUBE_DIM, PREFILTERED_CUBE_DIM).Build();

        // Scissor
        VkRect2D scissorRect;
        scissorRect.offset = {0, 0};
        scissorRect.extent = {PREFILTERED_CUBE_DIM, PREFILTERED_CUBE_DIM};

        // Input assembly
        InputAssemblyStateCIBuilder iaBuilder;
        // Rasterizer
        RasterizationStateCIBuilder rasterizerBuilder;
        rasterizerBuilder.SetFrontFace(VK_FRONT_FACE_COUNTER_CLOCKWISE);
        // MSAA
        MultisampleStateCIBuilder msBuilder;
        // Blend
        BlendStateCIBuilder blendStateBuilder;
        blendStateBuilder.setAttachments(1);
        // DS
        DepthStencilCIBuilder depthStencilBuilder;
        depthStencilBuilder.setDepthTestEnabled(false)
            .setDepthWriteEnabled(false);

        // Pipeline layout
        std::vector<VkDescriptorSetLayout> descLayouts = {
            GetDescriptorManager()->getDescriptorLayout(
                DESCRIPTOR_LAYOUT_PER_VIEW_DATA),
            GetDescriptorManager()->getDescriptorLayout(
                DESCRIPTOR_LAYOUT_SINGLE_SAMPLER)};

        std::vector<VkPushConstantRange> pushConstants{
            GetPushConstantRange<SingleFloatPushConstant>(VK_SHADER_STAGE_FRAGMENT_BIT)};

        m_prefilteredCubemapPipelineLayout =
            GetRenderDevice()->CreatePipelineLayout(descLayouts, pushConstants);

        // Dynmaic state
        std::vector<VkDynamicState> dynamicStateEnables = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

        VkShaderModule vertShdr = CreateShaderModule(
            ReadSpv("shaders/CubeMapToIrradianceMap.vert.spv"));
        VkShaderModule fragShdr = CreateShaderModule(
            ReadSpv("shaders/GeneratePrefilteredCubemap.frag.spv"));

        PipelineStateBuilder builder;
        m_prefilteredCubemapPipeline =
            builder.setShaderModules({vertShdr, fragShdr})
                .setVertextInfo({Vertex::getBindingDescription()},
                                Vertex::getAttributeDescriptions())
                .setAssembly(iaBuilder.Build())
                .setViewport(viewport, scissorRect)
                .setRasterizer(rasterizerBuilder.Build())
                .setMSAA(msBuilder.Build())
                .setColorBlending(blendStateBuilder.Build())
                .setPipelineLayout(m_prefilteredCubemapPipelineLayout)
                .setDynamicStates(dynamicStateEnables)
                .setDepthStencil(depthStencilBuilder.Build())
                .setRenderPass(m_vRenderPasses[RENDERPASS_COMPUTE_IRR_CUBEMAP])
                .Build(GetRenderDevice()->GetDevice());

        vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr,
                              nullptr);
        vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr,
                              nullptr);

        // Set debug name for the pipeline
        setDebugUtilsObjectName(
            reinterpret_cast<uint64_t>(m_prefilteredCubemapPipeline),
            VK_OBJECT_TYPE_PIPELINE, "prefiltered cube map");
    }

    // 4. Specular BRDF lookup 
    {
        // Viewport
        ViewportBuilder vpBuilder;
        VkViewport viewport =
            vpBuilder.setWH(SPECULAR_BRDF_LUT_DIM, SPECULAR_BRDF_LUT_DIM).Build();

        // Scissor
        VkRect2D scissorRect;
        scissorRect.offset = {0, 0};
        scissorRect.extent = {SPECULAR_BRDF_LUT_DIM, SPECULAR_BRDF_LUT_DIM};

        // Input assembly
        InputAssemblyStateCIBuilder iaBuilder;
        // Rasterizer
        RasterizationStateCIBuilder rasterizerBuilder;
        // MSAA
        MultisampleStateCIBuilder msBuilder;
        // Blend
        BlendStateCIBuilder blendStateBuilder;
        blendStateBuilder.setAttachments(1, false);
        // DS
        DepthStencilCIBuilder depthStencilBuilder;
        depthStencilBuilder.setDepthTestEnabled(false) .setDepthWriteEnabled(false);

        // Pipeline layout
        std::vector<VkDescriptorSetLayout> descLayouts = {};

        std::vector<VkPushConstantRange> pushConstants;

        m_specularBrdfLutPipelineLayout =
            GetRenderDevice()->CreatePipelineLayout(descLayouts, pushConstants);

        VkShaderModule vertShdr = CreateShaderModule(
            ReadSpv("shaders/lighting.vert.spv"));
        VkShaderModule fragShdr = CreateShaderModule(
            ReadSpv("shaders/GenerateSpecularBrdfLut.frag.spv"));
        PipelineStateBuilder builder;

        m_specularBrdfLutPipeline =
            builder.setShaderModules({vertShdr, fragShdr})
                .setVertextInfo({Vertex::getBindingDescription()},
                                Vertex::getAttributeDescriptions())
                .setAssembly(iaBuilder.Build())
                .setViewport(viewport, scissorRect)
                .setRasterizer(rasterizerBuilder.Build())
                .setMSAA(msBuilder.Build())
                .setColorBlending(blendStateBuilder.Build())
                .setPipelineLayout(m_specularBrdfLutPipelineLayout)
                .setDepthStencil(depthStencilBuilder.Build())
                .setRenderPass(m_vRenderPasses[RENDERPASS_COMPUTE_SPECULAR_BRDF_LUT])
                .Build(GetRenderDevice()->GetDevice());

        vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr,
                              nullptr);
        vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr,
                              nullptr);

        // Set debug name for the pipeline
        setDebugUtilsObjectName(
            reinterpret_cast<uint64_t>(m_prefilteredCubemapPipeline),
            VK_OBJECT_TYPE_PIPELINE, "specular brdf lut");
    }
}

void RenderLayerIBL::setupDescriptorSets()
{
    // Per veiw data
    m_perViewDataDescriptorSet =
        GetDescriptorManager()->AllocatePerviewDataDescriptorSet(
            m_uniformBuffer);

    // Environment map sampler descriptor set
    VkImageView envMapView = GetRenderResourceManager()->GetTexture("EnvMap", "assets/hdr/black.hdr")->getView();
    m_envMapDescriptorSet = GetDescriptorManager()->AllocateSingleSamplerDescriptorSet(envMapView);

    // Environment cube map descriptor set
    m_irrMapDescriptorSet = GetDescriptorManager()->AllocateSingleSamplerDescriptorSet(
        GetRenderResourceManager() ->GetColorTarget("env_cube_map", {ENV_CUBE_DIM, ENV_CUBE_DIM}, TEX_FORMAT, 1, 6) ->getView());
}

void RenderLayerIBL::RecordCommandBuffer()
{
    // Get skybox vertex data
    m_pSkybox = getSkybox();
    PrimitiveListConstRef primitives = m_pSkybox->getPrimitives();
    assert(primitives.size() == 1);
    VkBuffer vertexBuffer = primitives[0]->getVertexDeviceBuffer();
    VkBuffer indexBuffer = primitives[0]->getIndexDeviceBuffer();
    uint32_t nIndexCount = primitives[0]->getIndexCount();

    VkCommandBufferBeginInfo cmdBeginInfo = {};

    cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    cmdBeginInfo.pInheritanceInfo = nullptr;
    m_commandBuffer = GetRenderDevice()->AllocateStaticPrimaryCommandbuffer();
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_commandBuffer),
                            VK_OBJECT_TYPE_COMMAND_BUFFER, "[CB] IBL");

    VkDeviceSize offsets[1] = {0};

    // prepare render pass
    std::vector<VkClearValue> clearValues = {{0.0f, 0.0f, 0.0f, 0.0f}};
    std::array<VkRenderPassBeginInfo, RENDERPASS_COUNT> aRenderpassBeginInfos = {};
    std::array<VkViewport, RENDERPASS_COUNT> aViewports;
    std::array<VkRect2D, RENDERPASS_COUNT> aScissors;
    for (uint32_t passIdx = 0; passIdx < RENDERPASS_COUNT; passIdx++)
    {
        RenderPassBeginInfoBuilder beginInfoBuilder;
        aRenderpassBeginInfos[passIdx] =
            beginInfoBuilder
			.setRenderArea(VkExtent2D({ m_aCubemapSizes[passIdx], m_aCubemapSizes[passIdx] }))
			.setRenderPass(m_vRenderPasses[passIdx])
			.setClearValues(clearValues)
			.setFramebuffer(m_aFramebuffers[passIdx])
			.Build();

        ViewportBuilder vpBuilder;
        aViewports[passIdx] =
            vpBuilder.setWH(m_aCubemapSizes[passIdx], m_aCubemapSizes[passIdx])
                .Build();

        aScissors[passIdx] = {0, 0, m_aCubemapSizes[passIdx],
                              m_aCubemapSizes[passIdx]};
    }

    // Begin recording
    vkBeginCommandBuffer(m_commandBuffer, &cmdBeginInfo);
    {
        SCOPED_MARKER(m_commandBuffer, "Generate IBL info");
        {
            SCOPED_MARKER(m_commandBuffer, "First IBL pass");

            vkCmdBeginRenderPass(m_commandBuffer,
                                 &aRenderpassBeginInfos[RENDERPASS_LOAD_ENV_MAP],
                                 VK_SUBPASS_CONTENTS_INLINE);
            vkCmdSetViewport(m_commandBuffer, 0, 1, &aViewports[RENDERPASS_LOAD_ENV_MAP]);
            vkCmdSetScissor(m_commandBuffer, 0, 1, &aScissors[RENDERPASS_LOAD_ENV_MAP]);

            std::array<VkDescriptorSet, 2> sets = {m_perViewDataDescriptorSet,
                                                   m_envMapDescriptorSet};

            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              m_envCubeMapPipeline);

            vkCmdBindDescriptorSets(
                m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_envCubeMapPipelineLayout, 0, 2, sets.data(), 0, NULL);

            vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &vertexBuffer, offsets);

            vkCmdBindIndexBuffer(m_commandBuffer, indexBuffer, 0,
                                 VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(m_commandBuffer, nIndexCount, 1, 0, 0, 0);
            vkCmdEndRenderPass(m_commandBuffer);
        }
        // Second subpass
        {
            SCOPED_MARKER(m_commandBuffer, "Second IBL pass");

            vkCmdBeginRenderPass(m_commandBuffer,
                                 &aRenderpassBeginInfos[RENDERPASS_COMPUTE_IRR_CUBEMAP],
                                 VK_SUBPASS_CONTENTS_INLINE);
            vkCmdSetViewport(m_commandBuffer, 0, 1,
                             &aViewports[RENDERPASS_COMPUTE_IRR_CUBEMAP]);
            vkCmdSetScissor(m_commandBuffer, 0, 1,
                            &aScissors[RENDERPASS_COMPUTE_IRR_CUBEMAP]);

            std::array<VkDescriptorSet, 2> sets = {m_perViewDataDescriptorSet,
                                                   m_irrMapDescriptorSet};
            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              m_irrCubeMapPipeline);

            vkCmdBindDescriptorSets(
                m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_irrCubeMapPipelineLayout, 0, 2, sets.data(), 0, NULL);

            vkCmdBindVertexBuffers(m_commandBuffer, 0, 1,
                                   &vertexBuffer, offsets);
            vkCmdBindIndexBuffer(m_commandBuffer, indexBuffer, 0,
                                 VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(m_commandBuffer, nIndexCount, 1, 0, 0, 0);

            vkCmdEndRenderPass(m_commandBuffer);
        }
        // Compute prefiltered irradiance map
        VkImage prefilteredImage = GetRenderResourceManager()->GetColorTarget("prefiltered_cubemap", {PREFILTERED_CUBE_DIM, PREFILTERED_CUBE_DIM}, TEX_FORMAT, NUM_PREFILTERED_CUBEMAP_MIP, NUM_FACES, VK_IMAGE_USAGE_TRANSFER_DST_BIT)->getImage();
        GetRenderDevice()->TransitImageLayout(m_commandBuffer, prefilteredImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, NUM_PREFILTERED_CUBEMAP_MIP, NUM_FACES);
        VkImage prefilteredImageTmp = GetRenderResourceManager()->GetColorTarget("prefiltered_cubemap_tmp", {PREFILTERED_CUBE_DIM, PREFILTERED_CUBE_DIM}, TEX_FORMAT, 1, NUM_FACES, VK_IMAGE_USAGE_TRANSFER_SRC_BIT)->getImage();

        {
            SCOPED_MARKER(m_commandBuffer, "Computed Prefiltered cubemap");
            for (uint32_t uMip = 0; uMip < NUM_PREFILTERED_CUBEMAP_MIP; uMip++)
            {
                SCOPED_MARKER(m_commandBuffer, "Prefiltered cubemap mips");
                unsigned int uMipWidth = PREFILTERED_CUBE_DIM * std::powf(0.5f, uMip);
                unsigned int uMipHeight = PREFILTERED_CUBE_DIM * std::powf(0.5f, uMip);

                vkCmdBeginRenderPass(m_commandBuffer,
                                     &aRenderpassBeginInfos[RENDERPASS_COMPUTE_PRE_FILTERED_CUBEMAP],
                                     VK_SUBPASS_CONTENTS_INLINE);
                vkCmdSetViewport(m_commandBuffer, 0, 1,
                                 &aViewports[RENDERPASS_COMPUTE_PRE_FILTERED_CUBEMAP]);
                vkCmdSetScissor(m_commandBuffer, 0, 1,
                                &aScissors[RENDERPASS_COMPUTE_PRE_FILTERED_CUBEMAP]);

                std::array<VkDescriptorSet, 2> sets = {m_perViewDataDescriptorSet,
                                                       m_irrMapDescriptorSet};
                vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  m_prefilteredCubemapPipeline);

                // Pass roughness as push constant
                SingleFloatPushConstant pushConstantBlock;
                pushConstantBlock.fValue = (float)uMip / (float)(NUM_PREFILTERED_CUBEMAP_MIP - 1);
                vkCmdPushConstants(m_commandBuffer, m_prefilteredCubemapPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SingleFloatPushConstant), &pushConstantBlock);

                // Set dynamic viewport and scissor
                VkViewport viewport = {
                    0.0, 0.0, (float)uMipWidth, (float)uMipHeight,
                    0.0, 1.0};
                vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);

                VkRect2D scissor = {0, 0, uMipWidth, uMipHeight};
                vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);

                vkCmdBindDescriptorSets(
                    m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    m_prefilteredCubemapPipelineLayout, 0, 2, sets.data(), 0, NULL);

                vkCmdBindVertexBuffers(m_commandBuffer, 0, 1,
                                       &vertexBuffer, offsets);
                vkCmdBindIndexBuffer(m_commandBuffer, indexBuffer, 0,
                                     VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(m_commandBuffer, nIndexCount, 1, 0, 0, 0);

                vkCmdEndRenderPass(m_commandBuffer);

                VkImageCopy copyRegion{};

                copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.srcSubresource.baseArrayLayer = 0;
                copyRegion.srcSubresource.mipLevel = 0;
                copyRegion.srcSubresource.layerCount = NUM_FACES;
                copyRegion.srcOffset = {0, 0, 0};

                copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.dstSubresource.baseArrayLayer = 0;
                copyRegion.dstSubresource.mipLevel = uMip;
                copyRegion.dstSubresource.layerCount = NUM_FACES;
                copyRegion.dstOffset = {0, 0, 0};

                copyRegion.extent.width = uMipWidth;
                copyRegion.extent.height = uMipHeight;
                copyRegion.extent.depth = 1;

                vkCmdCopyImage(
                    m_commandBuffer,
                    prefilteredImageTmp,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    prefilteredImage,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &copyRegion);
            }
            // Prepare prefiltered map for shader usage
            GetRenderDevice()->TransitImageLayout(m_commandBuffer, prefilteredImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, NUM_PREFILTERED_CUBEMAP_MIP, NUM_FACES);
        }
        {
            SCOPED_MARKER(m_commandBuffer, "Computed specular brdf lut");

            vkCmdBeginRenderPass(m_commandBuffer,
                                 &aRenderpassBeginInfos[RENDERPASS_COMPUTE_SPECULAR_BRDF_LUT],
                                 VK_SUBPASS_CONTENTS_INLINE);
            vkCmdSetViewport(m_commandBuffer, 0, 1, &aViewports[RENDERPASS_COMPUTE_SPECULAR_BRDF_LUT]);
            vkCmdSetScissor(m_commandBuffer, 0, 1, &aScissors[RENDERPASS_COMPUTE_SPECULAR_BRDF_LUT]);

            const auto &prim = GetGeometryManager()->GetQuad()->getPrimitives().at(0);
            VkDeviceSize offset = 0;
            VkBuffer quadVertexBuffer = prim->getVertexDeviceBuffer();
            VkBuffer quadIndexBuffer = prim->getIndexDeviceBuffer();
            uint32_t nQuadIndexCount = prim->getIndexCount();

            vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &quadVertexBuffer,
                                   &offset);
            vkCmdBindIndexBuffer(m_commandBuffer, quadIndexBuffer, 0,
                                 VK_INDEX_TYPE_UINT32);
            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              m_specularBrdfLutPipeline);
            //vkCmdBindDescriptorSets(
            //    m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            //    mLightingPipelineLayout, 0, lightingDescSets.size(),
            //    lightingDescSets.data(), 0, nullptr);
            vkCmdDrawIndexed(m_commandBuffer, nQuadIndexCount, 1, 0, 0, 0);
            vkCmdEndRenderPass(m_commandBuffer);
        }
    }
    vkEndCommandBuffer(m_commandBuffer);
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_commandBuffer),
                            VK_OBJECT_TYPE_COMMAND_BUFFER, "[CB] IBL");
}

RenderLayerIBL::RenderLayerIBL()
{
    setupRenderPass();
    setupFramebuffer();
    CreatePipeline();
    setupDescriptorSets();
    RecordCommandBuffer();
}

RenderLayerIBL::~RenderLayerIBL()
{
    DestroyFramebuffer();
    for (auto renderpass : m_vRenderPasses)
    {
        vkDestroyRenderPass(GetRenderDevice()->GetDevice(), renderpass,
                            nullptr);
    }
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(), m_envCubeMapPipelineLayout, nullptr);
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(), m_irrCubeMapPipelineLayout, nullptr);
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(), m_prefilteredCubemapPipelineLayout, nullptr);
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(), m_specularBrdfLutPipelineLayout, nullptr);

    vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_envCubeMapPipeline, nullptr);
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_irrCubeMapPipeline, nullptr);
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_prefilteredCubemapPipeline, nullptr);
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_specularBrdfLutPipeline, nullptr);
}



void RenderLayerIBL::DestroyFramebuffer()
{
    for (auto framebuffer : m_aFramebuffers)
    {
        vkDestroyFramebuffer(GetRenderDevice()->GetDevice(), framebuffer,
                             nullptr);
    }
}

void RenderLayerIBL::ReloadEnvironmentMap(const std::string& sNewEnvMapPath)
{
    // Destroy and reload env map resource
    GetRenderResourceManager()->RemoveResource("EnvMap");
    // Recreate env map resource with new texture
    VkImageView envMapView = GetRenderResourceManager()->GetTexture("EnvMap", sNewEnvMapPath)->getView();
    // Update descriptor set with new envmap view
    GetDescriptorManager()->UpdateSingleSamplerDescriptorSet(m_envMapDescriptorSet, envMapView);
    RecordCommandBuffer();
}
