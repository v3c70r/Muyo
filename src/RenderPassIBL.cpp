#include "RenderPassIBL.h"

#include "DescriptorManager.h"
#include "PipelineManager.h"
#include "PushConstantBlocks.h"
#include "RenderResourceManager.h"
#include "VkRenderDevice.h"
#include "PipelineStateBuilder.h"
#include "glm/gtc/matrix_transform.hpp"
void RenderPassIBL::setupRenderPass()
{
    //1. Create two attachments, one for env cube map, another for diffuse irradiance map
    enum Attachments
    {
        ATTACHMENT_ENV_CUBE_MAP = 0,
        ATTACHMENT_IRR_MAP,
        ATTACHMENT_COUNT
    };
    std::array<VkAttachmentDescription, ATTACHMENT_COUNT> attachments;

    // Env map
    {
        VkAttachmentDescription &attDesc = attachments[ATTACHMENT_ENV_CUBE_MAP];
        attDesc = {};
        attDesc.format = TEX_FORMAT;
        attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    // Irradiance map, will be used later in other passes
    {
        VkAttachmentDescription &attDesc = attachments[ATTACHMENT_IRR_MAP];
        attDesc = {};
        attDesc.format = TEX_FORMAT;
        attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    //2. Create two subpasses, one to convert image from equirectangle map to cube map, another pass to generate irradiance map from the cube map

    enum Subpasses
    {
        SUBPASS_LOAD_ENV_MAP = 0,
        SUBPASS_COMPUTE_IRR_MAP,
        SUBPASS_COUNT
    };
    std::array<VkSubpassDescription, SUBPASS_COUNT> subpassDescs;

    // Subpass descriptions
    VkAttachmentReference envColorRef = {
        ATTACHMENT_ENV_CUBE_MAP, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    {
        VkSubpassDescription& subpassDesc = subpassDescs[SUBPASS_LOAD_ENV_MAP];
        subpassDesc = {};
        subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDesc.colorAttachmentCount = 1;
        subpassDesc.pColorAttachments = &envColorRef;
    }

    VkAttachmentReference irrColorRef = {
        ATTACHMENT_IRR_MAP, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference irrInputReference = {
        ATTACHMENT_ENV_CUBE_MAP, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    {
        VkSubpassDescription& subpassDesc = subpassDescs[SUBPASS_COMPUTE_IRR_MAP];
        subpassDesc = {};
        subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDesc.colorAttachmentCount = 1;
        subpassDesc.pColorAttachments = &irrColorRef;
        subpassDesc.inputAttachmentCount = 1;
        subpassDesc.pInputAttachments = &irrInputReference;
    }
    
    // Subpass layout dependencies 
    // TODO: Investigate why the subpass dependencies between the renderpasses are necessary here
    std::array<VkSubpassDependency, 1> dependencies;

    // First subpass dependency is from external to the first pass, used for layout transition
    //{
    //    VkSubpassDependency& subpassDep = dependencies[0];
    //    subpassDep = {};
    //    subpassDep.srcSubpass = VK_SUBPASS_EXTERNAL;
    //    subpassDep.dstSubpass = 0;
    //    subpassDep.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    //    subpassDep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    //    subpassDep.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    //    subpassDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
    //                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    //    subpassDep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    //}
    // Second subpass dependency to transit the env cube map from rt to srv 
    {
        VkSubpassDependency& subpassDep = dependencies[0];
        subpassDep = {};
        subpassDep.srcSubpass = 0;
        subpassDep.dstSubpass = 1;
        subpassDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        subpassDep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        subpassDep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        subpassDep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    }
    // The final subpass dependency writes the output to external texture
    //{
    //    VkSubpassDependency& subpassDep = dependencies[1];
    //    subpassDep = {};
    //    subpassDep.srcSubpass = 1;
    //    subpassDep.dstSubpass = VK_SUBPASS_EXTERNAL;
    //    subpassDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    //    subpassDep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    //    subpassDep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    //    subpassDep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    //    subpassDep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    //}

    // 2.5 Create multiview info
    std::array<uint32_t, 2> viewMasks = {0b111111, 0b111111};   // Two subpasses, each render to 6 faces of the cube
    VkRenderPassMultiviewCreateInfo multiViewCI = {};
    multiViewCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO;
    multiViewCI.subpassCount = viewMasks.size();
    multiViewCI.pViewMasks = viewMasks.data();
    //multiViewCI.correlationMaskCount = correlationMasks.size();
    //multiViewCI.pCorrelationMasks = correlationMasks.data();

    // 3. Create the renderpass
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount =
        static_cast<uint32_t>(subpassDescs.size());
    renderPassInfo.pSubpasses = subpassDescs.data();
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();
    renderPassInfo.pNext = &multiViewCI;

    assert(vkCreateRenderPass(GetRenderDevice()->GetDevice(), &renderPassInfo,
                              nullptr, &m_renderPass) == VK_SUCCESS);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_renderPass),
                            VK_OBJECT_TYPE_RENDER_PASS, "Diffuse IBL");
}

void RenderPassIBL::setupFramebuffer()
{
    std::array<VkImageView, 2> vImageViews = {
        GetRenderResourceManager()
            ->getColorTarget("env_cube_map", {IRR_CUBE_DIM, IRR_CUBE_DIM},
                             TEX_FORMAT, 1, 6)
            ->getView(),
        GetRenderResourceManager()
            ->getColorTarget("irr_cube_map", {IRR_CUBE_DIM, IRR_CUBE_DIM},
                             TEX_FORMAT, 1, 6)
            ->getView()};

    {
        VkFramebufferCreateInfo frameBufferCreateInfo = {};
        frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        frameBufferCreateInfo.renderPass = m_renderPass;
        frameBufferCreateInfo.attachmentCount = static_cast<uint32_t>(vImageViews.size());
        frameBufferCreateInfo.pAttachments = vImageViews.data();
        frameBufferCreateInfo.width = IRR_CUBE_DIM;
        frameBufferCreateInfo.height = IRR_CUBE_DIM;
        frameBufferCreateInfo.layers = 1;

        assert(vkCreateFramebuffer(GetRenderDevice()->GetDevice(),
                                   &frameBufferCreateInfo, nullptr,
                                   &m_framebuffer) == VK_SUCCESS);
        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_framebuffer),
                                VK_OBJECT_TYPE_FRAMEBUFFER,
                                "Irradiance Map");
    }
}

void RenderPassIBL::setupPipeline()
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
                .setRenderPass(m_renderPass)
                .setSubpassIndex(0)
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
                .setRenderPass(m_renderPass)
                .setSubpassIndex(1)
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

void RenderPassIBL::setupDescriptorSets()
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

void RenderPassIBL::recordCommandBuffer()
{
    // Get skybox vertex data
    m_pSkybox = getSkybox();
    PrimitiveListConstRef primitives = m_pSkybox->getPrimitives();
    assert(primitives.size() == 1);
    VkDeviceSize offset = 0;
    VkBuffer vertexBuffer = primitives[0]->getVertexDeviceBuffer();
    VkBuffer indexBuffer = primitives[0]->getIndexDeviceBuffer();
    uint32_t nIndexCount = primitives[0]->getIndexCount();

    VkCommandBufferBeginInfo cmdBeginInfo = {};

    cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    cmdBeginInfo.pInheritanceInfo = nullptr;

    VkRenderPassBeginInfo renderPassBeginInfo = {};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = m_renderPass;
    renderPassBeginInfo.renderArea.offset.x = 0;
    renderPassBeginInfo.renderArea.offset.y = 0;
    renderPassBeginInfo.renderArea.extent.width = IRR_CUBE_DIM;
    renderPassBeginInfo.renderArea.extent.height = IRR_CUBE_DIM;
    renderPassBeginInfo.clearValueCount = 1;

    std::vector<VkClearValue> clearValues = {{0.0f, 0.0f, 0.0f, 0.0f},
                                             {0.0f, 0.0f, 0.0f, 0.0f}};

    renderPassBeginInfo.clearValueCount = clearValues.size();
    renderPassBeginInfo.pClearValues = clearValues.data();
    renderPassBeginInfo.framebuffer = m_framebuffer;

    m_commandBuffer = GetRenderDevice()->allocateStaticPrimaryCommandbuffer();
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_commandBuffer),
                            VK_OBJECT_TYPE_COMMAND_BUFFER, "[CB] IBL");

    vkBeginCommandBuffer(m_commandBuffer, &cmdBeginInfo);
    vkCmdBeginRenderPass(m_commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &vertexBuffer, &offset);

    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(IRR_CUBE_DIM, IRR_CUBE_DIM).build();
    vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);

    VkRect2D scissor = {0, 0, IRR_CUBE_DIM, IRR_CUBE_DIM};
    vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);

    VkDeviceSize offsets[1] = {0};
    // First sub pass
    // Renders the components of the scene to the G-Buffer atttachments
    {
        //ScopedGPUMarker(m_commandBuffer, "First IBL pass");
        SCOPED_MARKER(m_commandBuffer, "First IBL pass");

        std::array<VkDescriptorSet, 2> sets = {m_perViewDataDescriptorSet,
                                               m_envMapDescriptorSet};

        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_envCubeMapPipeline);

        vkCmdBindDescriptorSets(
            m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_envCubeMapPipelineLayout, 0, 2, sets.data(), 0, NULL);
        // TODO: Update views to render a quad for times to fill the cubemap
        // Possibly to render it with VkRenderPassMultiviewCreateInfo

        vkCmdBindVertexBuffers(m_commandBuffer, 0, 1,
                               &vertexBuffer, offsets);

        vkCmdBindIndexBuffer(m_commandBuffer, indexBuffer, 0,
                             VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(m_commandBuffer, nIndexCount, 1, 0, 0, 0);
    }
    // Second subpass
    {
        //ScopedGPUMarker(m_commandBuffer, "Second IBL Pass");

        std::array<VkDescriptorSet, 2> sets = {m_perViewDataDescriptorSet,
                                               m_irrMapDescriptorSet};
        vkCmdNextSubpass(m_commandBuffer, VK_SUBPASS_CONTENTS_INLINE);
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
    }
    vkCmdEndRenderPass(m_commandBuffer);
    vkEndCommandBuffer(m_commandBuffer);
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_commandBuffer),
                            VK_OBJECT_TYPE_COMMAND_BUFFER, "[CB] IBL");
}

RenderPassIBL::RenderPassIBL()
{
    setupRenderPass();
    setupFramebuffer();
    setupPipeline();
    setupDescriptorSets();
    recordCommandBuffer();
}

RenderPassIBL::~RenderPassIBL()
{
    destroyFramebuffer();
    vkDestroyRenderPass(GetRenderDevice()->GetDevice(), m_renderPass, nullptr);
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(), m_envCubeMapPipelineLayout, nullptr);
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(), m_irrCubeMapPipelineLayout, nullptr);
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_envCubeMapPipeline, nullptr);
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_irrCubeMapPipeline, nullptr);
}



void RenderPassIBL::destroyFramebuffer()
{
    vkDestroyFramebuffer(GetRenderDevice()->GetDevice(), m_framebuffer,
                         nullptr);
    GetPipelineManager()->DestroyIBLPipelines();
}
