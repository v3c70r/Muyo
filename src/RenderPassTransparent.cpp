#include "RenderPassTransparent.h"
#include "Debug.h"
#include "VkRenderDevice.h"
#include "RenderResourceManager.h"
#include "PipelineStateBuilder.h"
#include "DescriptorManager.h"
#include <cassert>

RenderPassTransparent::RenderPassTransparent()
{
    CreateRenderPasses();
}

void RenderPassTransparent::CreateRenderPasses()
{
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

    // Lighting output attachment Attachment
    std::array<VkAttachmentDescription,2 > attDescs;
    {
        VkAttachmentDescription& attDesc = attDescs[0];
        attDesc = {};
        attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        attDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    {
        VkAttachmentDescription& attDesc = attDescs[1];
        attDesc = {};
        attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        attDesc.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attDesc.format = VK_FORMAT_D32_SFLOAT;
    }

    renderPassInfo.attachmentCount = static_cast<uint32_t>(attDescs.size());
    renderPassInfo.pAttachments = attDescs.data();

    // Attachment reference
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



    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &subpassDep;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    assert(vkCreateRenderPass(GetRenderDevice()->GetDevice(), &renderPassInfo,
                              nullptr, &renderPass) == VK_SUCCESS);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(renderPass),
                            VK_OBJECT_TYPE_RENDER_PASS, "Transparent Pass");

    m_vRenderPasses.push_back(renderPass);
}

RenderPassTransparent::~RenderPassTransparent()
{
    for (auto& renderPass : m_vRenderPasses)
    {
        vkDestroyRenderPass(GetRenderDevice()->GetDevice(), renderPass, nullptr);
    }
}

void RenderPassTransparent::CreateFramebuffer(uint32_t uWidth, uint32_t uHeight)
{
    VkExtent2D vp = {uWidth, uHeight};
    std::array<VkImageView, 2> views = {
        GetRenderResourceManager()->getColorTarget("LIGHTING_OUTPUT", vp)->getView(),
        GetRenderResourceManager()->getDepthTarget("GBUFFER_DEPTH", vp)->getView()};

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

void RenderPassTransparent::DestroyFramebuffer()
{
    vkDestroyFramebuffer(GetRenderDevice()->GetDevice(), m_frameBuffer, nullptr);
    m_frameBuffer = VK_NULL_HANDLE;
}


void RenderPassTransparent::CreatePipeline()
{
    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(m_renderArea).build();
    VkRect2D scissorRect;
    scissorRect.offset = {0, 0};
    scissorRect.extent = m_renderArea;

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

        m_pipelineLayout = PipelineManager::CreatePipelineLayout(descLayouts, pushConstants);

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipelineLayout), VK_OBJECT_TYPE_PIPELINE_LAYOUT, "Transparent");

        //TODO : Write a shader for transparent pass
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

        // TODO: Enable depth test, disable depth write
        m_pipeline =
            builder.setShaderModules({vertShdr, fragShdr})
                .setVertextInfo({Vertex::getBindingDescription()},
                                Vertex::getAttributeDescriptions())
                .setAssembly(iaBuilder.build())
                .setViewport(viewport, scissorRect)
                .setRasterizer(rsBuilder.build())
                .setMSAA(msBuilder.build())
                .setColorBlending(blendBuilder.build())
                .setPipelineLayout(m_pipelineLayout)
                .setDepthStencil(depthStencilBuilder.build())
                .setRenderPass(m_vRenderPasses.back())
                .setSubpassIndex(1)
                .build(GetRenderDevice()->GetDevice());

        vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr,
                              nullptr);
        vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr,
                              nullptr);

        // Set debug name for the pipeline
        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipeline),
                                VK_OBJECT_TYPE_PIPELINE, "Transparent");
}
