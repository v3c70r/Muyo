#include "RenderLayerIBL.h"

#include "DescriptorManager.h"
#include "PipelineManager.h"
#include "PushConstantBlocks.h"
#include "RenderResourceManager.h"
#include "VkRenderDevice.h"
#include "PipelineStateBuilder.h"
#include "glm/gtc/matrix_transform.hpp"
void RenderLayerIBL::setupRenderPass()
{
    //1. Create two attachments, one for env cube map, another for diffuse irradiance map
    std::array<VkAttachmentDescription, RENDERPASS_COUNT> attachments;

    // 2 Create multiview info
    std::array<uint32_t, 1> viewMasks = {
        0b111111};
    VkRenderPassMultiviewCreateInfo multiViewCI = {};
    multiViewCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO;
    multiViewCI.subpassCount = viewMasks.size();
    multiViewCI.pViewMasks = viewMasks.data();
    //multiViewCI.correlationMaskCount = correlationMasks.size();
    //multiViewCI.pCorrelationMasks = correlationMasks.data();

    // Create two render passes for equirectangle cube map and irradiance map

    for (uint32_t passIdx = 0; passIdx < RENDERPASS_COUNT; passIdx++)
    {
        // Create attachment
        VkAttachmentDescription &attDesc = attachments[passIdx];
        attDesc = {};
        attDesc.format = TEX_FORMAT;
        attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference ref = {0,
                                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
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
        renderPassInfo.pNext = &multiViewCI;

        assert(vkCreateRenderPass(GetRenderDevice()->GetDevice(),
                                  &renderPassInfo, nullptr,
                                  &m_aRenderPasses[passIdx]) == VK_SUCCESS);

        setDebugUtilsObjectName(
            reinterpret_cast<uint64_t>(m_aRenderPasses[passIdx]),
            VK_OBJECT_TYPE_RENDER_PASS, m_aRenderPassNames[passIdx].c_str());
    }
}

void RenderLayerIBL::setupFramebuffer()
{
    std::array<VkImageView, 2> vImageViews = {
        GetRenderResourceManager()
            ->getColorTarget("env_cube_map", {ENV_CUBE_DIM, ENV_CUBE_DIM},
                             TEX_FORMAT, 1, 6)
            ->getView(),
        GetRenderResourceManager()
            ->getColorTarget("irr_cube_map", {IRR_CUBE_DIM, IRR_CUBE_DIM},
                             TEX_FORMAT, 1, 6)
            ->getView()};

    // Create two framebuffers
    for (uint32_t passIdx = 0; passIdx < RENDERPASS_COUNT; passIdx++)
    {
        VkFramebufferCreateInfo frameBufferCreateInfo = {};
        frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        frameBufferCreateInfo.renderPass = m_aRenderPasses[passIdx];
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

void RenderLayerIBL::setupPipeline()
{
    // Create two pipelines for two subpasses
    // 1. Generate environment cube map from equirectangle hdr image
    {
        // Viewport
        ViewportBuilder vpBuilder;
        VkViewport viewport =
            vpBuilder.setWH(IRR_CUBE_DIM, IRR_CUBE_DIM).build();

        // Scissor
        VkRect2D scissorRect;
        scissorRect.offset = {0, 0};
        scissorRect.extent = {IRR_CUBE_DIM, IRR_CUBE_DIM};

        // Input assembly
        InputAssemblyStateCIBuilder iaBuilder;
        // Rasterizer
        RasterizationStateCIBuilder rasterizerBuilder;
        rasterizerBuilder.setFrontFace(VK_FRONT_FACE_COUNTER_CLOCKWISE);
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
            PipelineManager::CreatePipelineLayout(descLayouts, pushConstants);

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
                .setAssembly(iaBuilder.build())
                .setViewport(viewport, scissorRect)
                .setRasterizer(rasterizerBuilder.build())
                .setMSAA(msBuilder.build())
                .setColorBlending(blendStateBuilder.build())
                .setPipelineLayout(m_envCubeMapPipelineLayout)
                .setDynamicStates(dynamicStateEnables)
                .setDepthStencil(depthStencilBuilder.build())
                .setRenderPass(m_aRenderPasses[RENDERPASS_LOAD_ENV_MAP])
                .build(GetRenderDevice()->GetDevice());

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
            vpBuilder.setWH(IRR_CUBE_DIM, IRR_CUBE_DIM).build();

        // Scissor
        VkRect2D scissorRect;
        scissorRect.offset = {0, 0};
        scissorRect.extent = {IRR_CUBE_DIM, IRR_CUBE_DIM};

        // Input assembly
        InputAssemblyStateCIBuilder iaBuilder;
        // Rasterizer
        RasterizationStateCIBuilder rasterizerBuilder;
        rasterizerBuilder.setFrontFace(VK_FRONT_FACE_COUNTER_CLOCKWISE);
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
            PipelineManager::CreatePipelineLayout(descLayouts, pushConstants);

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
                .setAssembly(iaBuilder.build())
                .setViewport(viewport, scissorRect)
                .setRasterizer(rasterizerBuilder.build())
                .setMSAA(msBuilder.build())
                .setColorBlending(blendStateBuilder.build())
                .setPipelineLayout(m_irrCubeMapPipelineLayout)
                .setDynamicStates(dynamicStateEnables)
                .setDepthStencil(depthStencilBuilder.build())
                .setRenderPass(m_aRenderPasses[RENDERPASS_COMPUTE_IRR_MAP])
                .build(GetRenderDevice()->GetDevice());

        vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr,
                              nullptr);
        vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr,
                              nullptr);

        // Set debug name for the pipeline
        setDebugUtilsObjectName(
            reinterpret_cast<uint64_t>(m_irrCubeMapPipeline),
            VK_OBJECT_TYPE_PIPELINE, "CubeMapToIrradianceMap");
    }
}

void RenderLayerIBL::setupDescriptorSets()
{
    // Per veiw data
    m_perViewDataDescriptorSet =
        GetDescriptorManager()->allocatePerviewDataDescriptorSet(
            m_uniformBuffer);

    // Environment map sampler descriptor set
    VkImageView envMapView =
        GetRenderResourceManager()->getTexture("EnvMap", "assets/hdr/Walk_Of_Fame/Mans_Outside_2k.hdr")->getView();
    m_envMapDescriptorSet =
        GetDescriptorManager()->allocateImGuiDescriptorSet(envMapView);

    // Environment cube map descriptor set
    m_irrMapDescriptorSet = GetDescriptorManager()->allocateImGuiDescriptorSet(
        GetRenderResourceManager()
            ->getColorTarget("env_cube_map", {IRR_CUBE_DIM, IRR_CUBE_DIM},
                             TEX_FORMAT, 1, 6)
            ->getView());
}

void RenderLayerIBL::recordCommandBuffer()
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
    m_commandBuffer = GetRenderDevice()->allocateStaticPrimaryCommandbuffer();
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
                .setRenderArea(
                    VkRect2D({static_cast<int32_t>(m_aCubemapSizes[passIdx]),
                              static_cast<int32_t>(m_aCubemapSizes[passIdx])}))
                .setRenderPass(m_aRenderPasses[passIdx])
                .setClearValues(clearValues)
                .setFramebuffer(m_aFramebuffers[passIdx])
                .build();

        ViewportBuilder vpBuilder;
        aViewports[passIdx] =
            vpBuilder.setWH(m_aCubemapSizes[passIdx], m_aCubemapSizes[passIdx])
                .build();

        aScissors[passIdx] = {0, 0, m_aCubemapSizes[passIdx],
                              m_aCubemapSizes[passIdx]};
    }

    // Begin recording
    vkBeginCommandBuffer(m_commandBuffer, &cmdBeginInfo);
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
                             &aRenderpassBeginInfos[RENDERPASS_COMPUTE_IRR_MAP],
                             VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetViewport(m_commandBuffer, 0, 1,
                         &aViewports[RENDERPASS_COMPUTE_IRR_MAP]);
        vkCmdSetScissor(m_commandBuffer, 0, 1,
                        &aScissors[RENDERPASS_COMPUTE_IRR_MAP]);

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
    vkEndCommandBuffer(m_commandBuffer);
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_commandBuffer),
                            VK_OBJECT_TYPE_COMMAND_BUFFER, "[CB] IBL");
}

RenderLayerIBL::RenderLayerIBL()
{
    setupRenderPass();
    setupFramebuffer();
    setupPipeline();
    setupDescriptorSets();
    recordCommandBuffer();
}

RenderLayerIBL::~RenderLayerIBL()
{
    destroyFramebuffer();
    for (auto renderpass : m_aRenderPasses)
    {
        vkDestroyRenderPass(GetRenderDevice()->GetDevice(), renderpass,
                            nullptr);
    }
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(), m_envCubeMapPipelineLayout, nullptr);
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(), m_irrCubeMapPipelineLayout, nullptr);
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_envCubeMapPipeline, nullptr);
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_irrCubeMapPipeline, nullptr);
}



void RenderLayerIBL::destroyFramebuffer()
{
    for (auto framebuffer : m_aFramebuffers)
    {
        vkDestroyFramebuffer(GetRenderDevice()->GetDevice(), framebuffer,
                             nullptr);
    }
    GetPipelineManager()->DestroyIBLPipelines();
}
