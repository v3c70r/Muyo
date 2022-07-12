#include "RenderPassShadow.h"
#include "PipelineStateBuilder.h"
#include "RenderResourceManager.h"
#include "Geometry.h"

void RenderPassShadow::PrepareRenderPass()
{
    // Depth attachments
    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;


    // Attachment reference
    VkAttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Create Subpass
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // subpass dependency
    VkSubpassDependency subpassDep = {};
    subpassDep.srcSubpass = VK_SUBPASS_EXTERNAL;
    subpassDep.dstSubpass = 0;
    subpassDep.srcStageMask = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    subpassDep.srcAccessMask = 0;
    subpassDep.dstStageMask = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    subpassDep.dstAccessMask =  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &subpassDep;
    VK_ASSERT(vkCreateRenderPass(GetRenderDevice()->GetDevice(), &renderPassInfo, nullptr, &m_renderPass));

    // Create output resources
    VkImageView shadowMapView = GetRenderResourceManager()->GetDepthTarget("ShadowMap", m_shadowMapSize, VK_FORMAT_D32_SFLOAT)->getView();

    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &shadowMapView;
    framebufferInfo.width = m_shadowMapSize.width;
    framebufferInfo.height = m_shadowMapSize.height;
    framebufferInfo.layers = 1;
    VK_ASSERT(vkCreateFramebuffer(GetRenderDevice()->GetDevice(), &framebufferInfo, nullptr, &m_frameBuffer));
}

RenderPassShadow::~RenderPassShadow()
{
    vkDestroyFramebuffer(GetRenderDevice()->GetDevice(), m_frameBuffer, nullptr);
    vkDestroyRenderPass(GetRenderDevice()->GetDevice(), m_renderPass, nullptr);
}

void RenderPassShadow::CreatePipeline()
{
    VkDescriptorSetLayout descLayout = m_renderPassParameters.GetDescriptorSetLayout();
    std::vector<VkDescriptorSetLayout> descLayouts = { descLayout };

    std::vector<VkPushConstantRange> pushConstants;

    m_pipelineLayout = GetRenderDevice()->CreatePipelineLayout(descLayouts, pushConstants);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipelineLayout), VK_OBJECT_TYPE_PIPELINE_LAYOUT, "Shadow map pass");

    VkShaderModule vertexShader = CreateShaderModule(ReadSpv("shaders/triangle.vert.spv"));
    VkShaderModule fragShader = CreateShaderModule(ReadSpv("shaders/triangle.frag.spv"));

    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(m_shadowMapSize).Build();
    VkRect2D scissorRect;
    scissorRect.offset = {0, 0};
    scissorRect.extent = m_shadowMapSize;

    InputAssemblyStateCIBuilder iaBuilder;
    RasterizationStateCIBuilder rsBuilder;
    MultisampleStateCIBuilder msBuilder;
    BlendStateCIBuilder blendBuilder;
    blendBuilder.setAttachments(1);
    DepthStencilCIBuilder depthStencilBuilder;
    PipelineStateBuilder builder;
    std::vector<VkDynamicState> vDynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    m_pipeline =
        builder.setShaderModules({vertexShader, fragShader})
            .setVertextInfo({Vertex::getBindingDescription()},
                            Vertex::getAttributeDescriptions())
            .setAssembly(iaBuilder.Build())
            .setViewport(viewport, scissorRect)
            .setRasterizer(rsBuilder.Build())
            .setMSAA(msBuilder.Build())
            .setColorBlending(blendBuilder.Build())
            .setPipelineLayout(m_pipelineLayout)
            .setDepthStencil(depthStencilBuilder.Build())
            .setRenderPass(m_renderPass)
            .setDynamicStates(vDynamicStates)
            .Build(GetRenderDevice()->GetDevice());

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertexShader, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShader, nullptr);

    // Set debug name for the pipeline
    setDebugUtilsObjectName( reinterpret_cast<uint64_t>(m_pipeline), VK_OBJECT_TYPE_PIPELINE, "Shadow pass");
}
