#include "RenderPassIBL.h"

#include "DescriptorManager.h"
#include "PipelineManager.h"
#include "PushConstantBlocks.h"
#include "RenderResourceManager.h"
#include "VkRenderDevice.h"
#include "PipelineStateBuilder.h"
#include "glm/gtc/matrix_transform.hpp"
RenderPassIBL::RenderPassIBL()
{
    VkAttachmentDescription attDesc = {};
    // Color attachment
    attDesc.format = TEX_FORMAT;
    attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorReference = {
        0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpassDescription = {};
    subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassDescription.colorAttachmentCount = 1;
    subpassDescription.pColorAttachments = &colorReference;

    // Use subpass dependencies for layout transitions
    std::array<VkSubpassDependency, 2> dependencies;
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // Renderpass
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attDesc;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpassDescription;
    renderPassInfo.dependencyCount = 2;
    renderPassInfo.pDependencies = dependencies.data();

    assert(vkCreateRenderPass(GetRenderDevice()->GetDevice(), &renderPassInfo,
                              nullptr, &m_renderPass) == VK_SUCCESS);
}

RenderPassIBL::~RenderPassIBL()
{
    destroyFramebuffer();
    vkDestroyRenderPass(GetRenderDevice()->GetDevice(), m_renderPass, nullptr);
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
    // const uint32_t numMips = static_cast<uint32_t>(floor(log2(CUBE_DIM))) +
    // 1; VkImageView cube = GetRenderResourceManager()
    //                       ->getRenderTarget("Irradiance map", true,
    //                                         VkExtent2D({CUBE_DIM, CUBE_DIM}),
    //                                         TEX_FORMAT, numMips, 6)
    //                       ->getView();

    // create image to store temporary output image
    VkImageView view =
        GetRenderResourceManager()
            ->getRenderTarget("tempIrradanceOutput", true,
                              VkExtent2D({CUBE_DIM, CUBE_DIM}), TEX_FORMAT)
            ->getView();

    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &view;
    framebufferInfo.width = CUBE_DIM;
    framebufferInfo.height = CUBE_DIM;
    framebufferInfo.layers = 1;
    assert(vkCreateFramebuffer(GetRenderDevice()->GetDevice(), &framebufferInfo,
                               nullptr, &m_framebuffer) == VK_SUCCESS);
    m_renderArea = {CUBE_DIM, CUBE_DIM};

    // Create pipeline
    GetPipelineManager()->CreateIBLPipelines(
        CUBE_DIM, CUBE_DIM,
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
            m_texEnvMap.getImageView());

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

    const uint32_t numMips = static_cast<uint32_t>(floor(log2(CUBE_DIM))) + 1;
    for (uint32_t m = 0; m < numMips; m++)
    {
        for (uint32_t f = 0; f < 6; f++)
        {
            VkViewport viewport;
            viewport.x = 0;
            viewport.y = 0;
            viewport.width = static_cast<float>(CUBE_DIM * std::pow(0.5f, m));
            viewport.height = static_cast<float>(CUBE_DIM * std::pow(0.5f, m));
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

void RenderPassIBL::envMapToCubemap(VkImageView envMap)
{
    // Create descriptors
    //
    // Create pipeline

    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(CUBE_DIM, CUBE_DIM).build();

    VkRect2D scissorRect;
    scissorRect.offset = {0, 0};
    scissorRect.extent = {CUBE_DIM, CUBE_DIM};

    InputAssemblyStateCIBuilder iaBuilder;
    RasterizationStateCIBuilder rasterizerBuilder;
    MultisampleStateCIBuilder msBuilder;
    BlendStateCIBuilder blendStateBuilder;
    blendStateBuilder.setAttachments(1);
    DepthStencilCIBuilder depthStencilBuilder;
    depthStencilBuilder.setDepthTestEnabled(false)
        .setDepthWriteEnabled(false)
        .setDepthCompareOp(VK_COMPARE_OP_LESS_OR_EQUAL);

    envToCubeMapPipeline = CreatePipelineLayout(
        descriptorSetLayout,
        getPushConstantRange<IrradianceMapPushConstBlock>(
            (VkShaderStageFlagBits)(VK_SHADER_STAGE_VERTEX_BIT |
                                    VK_SHADER_STAGE_FRAGMENT_BIT)));

    // Dynmaic state
    std::vector<VkDynamicState> dynamicStateEnables = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkShaderModule vertShdr =
        CreateShaderModule(ReadSpv("shaders/filtercube.vert.spv"));
    VkShaderModule fragShdr =
        CreateShaderModule(ReadSpv("shaders/irradiancecube.frag.spv"));
    PipelineStateBuilder builder;

    envToCubeMapPipeline =
        builder.setShaderModules({vertShdr, fragShdr})
            .setVertextInfo({UIVertex::getBindingDescription()},
                            UIVertex::getAttributeDescriptions())
            .setAssembly(iaBuilder.build())
            .setViewport(viewport, scissorRect)
            .setRasterizer(rasterizerBuilder.build())
            .setMSAA(msBuilder.build())
            .setColorBlending(blendStateBuilder.build())
            .setPipelineLayout(maPipelineLayouts[PIPELINE_TYPE_IBL_IRRADIANCE])
            .setDynamicStates(dynamicStateEnables)
            .setDepthStencil(depthStencilBuilder.build())
            .setRenderPass(pass)
            .build(GetRenderDevice()->GetDevice());

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr, nullptr);

    // Set debug name for the pipeline
    setDebugUtilsObjectName(
        reinterpret_cast<uint64_t>(maPipelines[PIPELINE_TYPE_IBL_IRRADIANCE]),
        VK_OBJECT_TYPE_PIPELINE, "IBL Irradiance");
}
