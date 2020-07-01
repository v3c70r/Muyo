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
        ATTACHMENT_ENV_MAP = 0,
        ATTACHMENT_IRR_MAP,
        ATTACHMENT_COUNT
    };
    std::array<VkAttachmentDescription, ATTACHMENT_COUNT> attachments;

    // Env map
    {
        VkAttachmentDescription &attDesc = attachments[ATTACHMENT_ENV_MAP];
        attDesc = {};
        attDesc.format = TEX_FORMAT;
        attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attDesc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
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
    VkAttachmentReference colorRef;
    colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    {
        VkSubpassDescription& subpassDesc = subpassDescs[SUBPASS_LOAD_ENV_MAP];
        subpassDesc = {};
        subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDesc.colorAttachmentCount = 1;
        subpassDesc.pColorAttachments = &colorRef;
    }
    {
        VkSubpassDescription& subpassDesc = subpassDescs[SUBPASS_COMPUTE_IRR_MAP];
        subpassDesc = {};
        subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDesc.colorAttachmentCount = 1;
        subpassDesc.pColorAttachments = &colorRef;
    }
    
    // Subpass layout dependencies 
    std::array<VkSubpassDependency, 3> dependencies;

    // First subpass dependency is from external to the first pass, used for layout transition
    {
        VkSubpassDependency& subpassDep = dependencies[0];
        subpassDep = {};
        subpassDep.srcSubpass = VK_SUBPASS_EXTERNAL;
        subpassDep.dstSubpass = 0;
        subpassDep.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        subpassDep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        subpassDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        subpassDep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    }
    // Second subpass dependency to transit the env cube map from rt to srv 
    {
        VkSubpassDependency& subpassDep = dependencies[1];
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
    {
        VkSubpassDependency& subpassDep = dependencies[2];
        subpassDep = {};
        subpassDep.srcSubpass = 1;
        subpassDep.dstSubpass = VK_SUBPASS_EXTERNAL;
        subpassDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        subpassDep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        subpassDep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        subpassDep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    }

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

    assert(vkCreateRenderPass(GetRenderDevice()->GetDevice(), &renderPassInfo,
                              nullptr, &m_renderPass) == VK_SUCCESS);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_renderPass),
                            VK_OBJECT_TYPE_RENDER_PASS, "Diffuse IBL");
}

void RenderPassIBL::setupFramebuffer()
{
    std::array<VkImageView, 2> vImageViews = {
        GetRenderResourceManager()
            ->getColorTarget("env_cube_map", {ENV_CUBE_DIM, ENV_CUBE_DIM},
                             TEX_FORMAT, 1, 6)
            ->getView(),
        GetRenderResourceManager()
            ->getColorTarget("irr_cube_map", {ENV_CUBE_DIM, ENV_CUBE_DIM},
                             TEX_FORMAT, 1, 6)
            ->getView()};

    {
        VkFramebufferCreateInfo frameBufferCreateInfo = {};
        frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        frameBufferCreateInfo.renderPass = m_renderPass;
        frameBufferCreateInfo.attachmentCount = static_cast<uint32_t>(vImageViews.size());
        frameBufferCreateInfo.pAttachments = vImageViews.data();
        frameBufferCreateInfo.width = ENV_CUBE_DIM;
        frameBufferCreateInfo.height = ENV_CUBE_DIM;
        frameBufferCreateInfo.layers = NUM_FACES;

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
            vpBuilder.setWH(ENV_CUBE_DIM, ENV_CUBE_DIM).build();

        // Scissor
        VkRect2D scissorRect;
        scissorRect.offset = {0, 0};
        scissorRect.extent = {ENV_CUBE_DIM, ENV_CUBE_DIM};

        // Input assembly
        InputAssemblyStateCIBuilder iaBuilder;
        // Rasterizer
        RasterizationStateCIBuilder rasterizerBuilder;
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
            vpBuilder.setWH(ENV_CUBE_DIM, ENV_CUBE_DIM).build();

        // Scissor
        VkRect2D scissorRect;
        scissorRect.offset = {0, 0};
        scissorRect.extent = {IRR_CUBE_DIM, IRR_CUBE_DIM};

        // Input assembly
        InputAssemblyStateCIBuilder iaBuilder;
        // Rasterizer
        RasterizationStateCIBuilder rasterizerBuilder;
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
            ->getColorTarget("env_cube_map", {ENV_CUBE_DIM, ENV_CUBE_DIM},
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
    renderPassBeginInfo.renderArea.extent.width = ENV_CUBE_DIM;
    renderPassBeginInfo.renderArea.extent.height = ENV_CUBE_DIM;
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
    VkViewport viewport = vpBuilder.setWH(ENV_CUBE_DIM, ENV_CUBE_DIM).build();
    vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);

    VkRect2D scissor = {0, 0, ENV_CUBE_DIM, ENV_CUBE_DIM};
    vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);

    VkDeviceSize offsets[1] = {0};
    // First sub pass
    // Renders the components of the scene to the G-Buffer atttachments
    {
        std::array<VkDescriptorSet, 2> sets = {m_perViewDataDescriptorSet,
                                               m_envMapDescriptorSet};

        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_envCubeMapPipeline);

        vkCmdBindDescriptorSets(
            m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_envCubeMapPipelineLayout, 0, 2, sets.data(), 0, NULL);

        vkCmdBindVertexBuffers(m_commandBuffer, 0, 1,
                               &vertexBuffer, offsets);

        vkCmdBindIndexBuffer(m_commandBuffer, indexBuffer, 0,
                             VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(m_commandBuffer, nIndexCount, 1, 0, 0, 0);
    }
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

void RenderPassIBL::initializeIBLResources()
{
    // Create a framebuffer render to a cube
    if (m_framebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(GetRenderDevice()->GetDevice(), m_framebuffer,
                             nullptr);
        m_framebuffer = VK_NULL_HANDLE;
    }

    // TODO: Move it to initialization
    // Create image to store irradiance cube output
    // const uint32_t numMips = static_cast<uint32_t>(floor(log2(ENV_CUBE_DIM))) +
    // 1; VkImageView cube = GetRenderResourceManager()
    //                       ->getRenderTarget("Irradiance map", true,
    //                                         VkExtent2D({ENV_CUBE_DIM, ENV_CUBE_DIM}),
    //                                         TEX_FORMAT, numMips, 6)
    //                       ->getView();

    // create image to store temporary output image
    VkImageView view =
        GetRenderResourceManager()
            ->getRenderTarget("tempIrradanceOutput", true,
                              VkExtent2D({ENV_CUBE_DIM, ENV_CUBE_DIM}), TEX_FORMAT)
            ->getView();

    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &view;
    framebufferInfo.width = ENV_CUBE_DIM;
    framebufferInfo.height = ENV_CUBE_DIM;
    framebufferInfo.layers = 1;
    assert(vkCreateFramebuffer(GetRenderDevice()->GetDevice(), &framebufferInfo,
                               nullptr, &m_framebuffer) == VK_SUCCESS);
    m_renderArea = {ENV_CUBE_DIM, ENV_CUBE_DIM};

    // Create pipeline
    GetPipelineManager()->CreateIBLPipelines(
        ENV_CUBE_DIM, ENV_CUBE_DIM,
        GetDescriptorManager()->getDescriptorLayout(
            DESCRIPTOR_LAYOUT_SINGLE_SAMPLER),  // Simply a single input sampler
        m_renderPass);

    // get geometry
    m_pSkybox = getSkybox();
}

void RenderPassIBL::destroyFramebuffer()
{
    vkDestroyFramebuffer(GetRenderDevice()->GetDevice(), m_framebuffer,
                         nullptr);
    GetPipelineManager()->DestroyIBLPipelines();
}

void RenderPassIBL::generateIrradianceCube()
{
    // TODO: Setup command buffer to render
    // TODO: Convert Equirectangluar to cube map
    std::array<glm::mat4, 6> matrices = {
        // POSITIVE_X
        glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f),
                                glm::vec3(0.0f, 1.0f, 0.0f)),
                    glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
        // NEGATIVE_X
        glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f),
                                glm::vec3(0.0f, 1.0f, 0.0f)),
                    glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
        // POSITIVE_Y
        glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f),
                    glm::vec3(1.0f, 0.0f, 0.0f)),
        // NEGATIVE_Y
        glm::rotate(glm::mat4(1.0f), glm::radians(90.0f),
                    glm::vec3(1.0f, 0.0f, 0.0f)),
        // POSITIVE_Z
        glm::rotate(glm::mat4(1.0f), glm::radians(180.0f),
                    glm::vec3(1.0f, 0.0f, 0.0f)),

        glm::rotate(glm::mat4(1.0f), glm::radians(180.0f),
                    glm::vec3(0.0f, 0.0f, 1.0f)),
    };

    // create descriptor set from the view
    VkDescriptorSet descriptorSet =
        GetDescriptorManager()->allocateImGuiDescriptorSet(
            m_texEnvCupMap.getView());

    VkCommandBufferBeginInfo beginInfo = {};

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    m_commandBuffer = GetRenderDevice()->allocateStaticPrimaryCommandbuffer();
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_commandBuffer),
                            VK_OBJECT_TYPE_COMMAND_BUFFER, "[CB] Generate IBL");

    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);

    IrradianceMapPushConstBlock pushBlock = {
        glm::mat4(1.0),
        // Sampling deltas
        (2.0f * float(M_PI)) / 180.0f,
        (0.5f * float(M_PI)) / 64.0f,
    };

    const uint32_t numMips = static_cast<uint32_t>(floor(log2(ENV_CUBE_DIM))) + 1;
    for (uint32_t m = 0; m < numMips; m++)
    {
        for (uint32_t f = 0; f < 6; f++)
        {
            VkViewport viewport;
            viewport.x = 0;
            viewport.y = 0;
            viewport.width = static_cast<float>(ENV_CUBE_DIM * std::pow(0.5f, m));
            viewport.height = static_cast<float>(ENV_CUBE_DIM * std::pow(0.5f, m));
            viewport.minDepth = 0.0;
            viewport.maxDepth = 1.0;

            vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);
            pushBlock.mMvp =
                glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) *
                matrices[f];

            VkPipelineLayout pipelineLayout =
                GetPipelineManager()->GetIBLIrradiancePipelineLayout();
            vkCmdPushConstants(
                m_commandBuffer, pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                sizeof(pushBlock), &pushBlock);
            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              GetPipelineManager()->GetIBLIrradiancePipeline());
            vkCmdBindDescriptorSets(
                m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

            // Bind skybox vertex
            PrimitiveListConstRef primitives = m_pSkybox->getPrimitives();
            assert(primitives.size() == 1);

            VkDeviceSize offset = 0;
            VkBuffer vertexBuffer = primitives[0]->getVertexDeviceBuffer();
            VkBuffer indexBuffer = primitives[0]->getIndexDeviceBuffer();
            uint32_t nIndexCount = primitives[0]->getIndexCount();
            vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &vertexBuffer,
                                   &offset);
            vkCmdBindIndexBuffer(m_commandBuffer, indexBuffer, 0,
                                 VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(m_commandBuffer, nIndexCount, 1, 0, 0, 0);
        }
    }
    // VkRenderPassBeginInfo renderPassBeginInfo = {};
    // renderPassBeginInfo.sType =
    //    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    // renderPassBeginInfo.renderPass = m_renderPass;
    // renderPassBeginInfo.framebuffer = m_framebuffer;

    // renderPassBeginInfo.renderArea.offset = {0, 0};
    // renderPassBeginInfo.renderArea.extent = m_renderArea;

    // VkClearValue clearValues[1];
    // clearValues[0].color = {{0.0f, 0.0f, 0.2f, 0.0f}};
    // renderPassBeginInfo.clearValueCount = 1;
    // renderPassBeginInfo.pClearValues = clearValues;

    // vkCmdBeginRenderPass(m_commandBuffer, &renderPassBeginInfo,
    //                     VK_SUBPASS_CONTENTS_INLINE);

    // vkCmdEndRenderPass(m_commandBuffer);
    vkEndCommandBuffer(m_commandBuffer);
}

void RenderPassIBL::createEquirectangularMapToCubeMapPipeline()
{
    // Create descriptors
    //
    // Create pipeline

    //{
    //    ViewportBuilder vpBuilder;
    //    VkViewport viewport =
    //        vpBuilder.setWH(ENV_CUBE_DIM, ENV_CUBE_DIM).build();

    //    VkRect2D scissorRect;
    //    scissorRect.offset = {0, 0};
    //    scissorRect.extent = {ENV_CUBE_DIM, ENV_CUBE_DIM};

    //    InputAssemblyStateCIBuilder iaBuilder;
    //    RasterizationStateCIBuilder rasterizerBuilder;
    //    MultisampleStateCIBuilder msBuilder;
    //    BlendStateCIBuilder blendStateBuilder;
    //    blendStateBuilder.setAttachments(1);
    //    DepthStencilCIBuilder depthStencilBuilder;
    //    depthStencilBuilder.setDepthTestEnabled(false)
    //        .setDepthWriteEnabled(false)
    //        .setDepthCompareOp(VK_COMPARE_OP_LESS_OR_EQUAL);

    //    m_envCubeMapPipelineLayout = PipelineManager::CreatePipelineLayout(
    //        GetDescriptorManager()->getDescriptorLayout(
    //            DESCRIPTOR_LAYOUT_GBUFFER));

    //    //,getPushConstantRange<IrradianceMapPushConstBlock>(
    //    //    (VkShaderStageFlagBits)(VK_SHADER_STAGE_VERTEX_BIT |
    //    //                            VK_SHADER_STAGE_FRAGMENT_BIT)));

    //    // Dynmaic state
    //    std::vector<VkDynamicState> dynamicStateEnables = {
    //        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    //    VkShaderModule vertShdr = CreateShaderModule(
    //        ReadSpv("shaders/equirectangularToCubeMap.vert.spv"));
    //    VkShaderModule fragShdr = CreateShaderModule(
    //        ReadSpv("shaders/equirectangularToCubeMap.frag.spv"));
    //    PipelineStateBuilder builder;

    //    m_envCubeMapPipeline =
    //        builder.setShaderModules({vertShdr, fragShdr})
    //            .setVertextInfo({Vertex::getBindingDescription()},
    //                            Vertex::getAttributeDescriptions())
    //            .setAssembly(iaBuilder.build())
    //            .setViewport(viewport, scissorRect)
    //            .setRasterizer(rasterizerBuilder.build())
    //            .setMSAA(msBuilder.build())
    //            .setColorBlending(blendStateBuilder.build())
    //            .setPipelineLayout(m_envCubeMapPipelineLayout)
    //            .setDynamicStates(dynamicStateEnables)
    //            .setDepthStencil(depthStencilBuilder.build())
    //            .setRenderPass(m_renderPass)
    //            .build(GetRenderDevice()->GetDevice());

    //    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr,
    //                          nullptr);
    //    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr,
    //                          nullptr);

    //    // Set debug name for the pipeline
    //    setDebugUtilsObjectName(
    //        reinterpret_cast<uint64_t>(m_envCubeMapPipeline),
    //        VK_OBJECT_TYPE_PIPELINE, "EquirectangularMapToCubeMap");
    //}
    //{
    //}
}
void RenderPassIBL::recordEquirectangularMapToCubeMapCmd(VkImageView envMap) {}
