#include "RenderPassIBL.h"

#include "RenderResourceManager.h"
#include "VkRenderDevice.h"
#include "PipelineManager.h"
#include "DescriptorManager.h"
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
    // Get image view from global resource manager
    VkImageView view =
        GetRenderResourceManager()
            ->getRenderTarget("Irradiance map", true,
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
        GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_IMGUI), // Simply a single input sampler
        m_renderPass);
}

void RenderPassIBL::destroyFramebuffer()
{
    vkDestroyFramebuffer(GetRenderDevice()->GetDevice(), m_framebuffer,
                         nullptr);
    GetPipelineManager()->DestroyIBLPipelines();
}

void RenderPassIBL::initializeIBLPipeline() {}
void RenderPassIBL::destroyIBLPipeline() {}
void RenderPassIBL::recordCommandBuffer()
{

    // TODO: Setup command buffer to render
    VkCommandBufferBeginInfo beginInfo = {};

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    m_commandBuffer = GetRenderDevice()->allocateStaticPrimaryCommandbuffer();
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_commandBuffer),
                            VK_OBJECT_TYPE_COMMAND_BUFFER, "[CB] GBuffer");

    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);

    VkRenderPassBeginInfo renderPassBeginInfo = {};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = m_renderPass;
    renderPassBeginInfo.framebuffer = m_framebuffer;

    renderPassBeginInfo.renderArea.offset = {0, 0};
    renderPassBeginInfo.renderArea.extent = m_renderArea;

    VkClearValue clearValues[1];
    clearValues[0].color = {{0.0f, 0.0f, 0.2f, 0.0f}};
    renderPassBeginInfo.clearValueCount = 1;
    renderPassBeginInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(m_commandBuffer, &renderPassBeginInfo,
                         VK_SUBPASS_CONTENTS_INLINE);

    vkCmdEndRenderPass(m_commandBuffer);
    vkEndCommandBuffer(m_commandBuffer);
}
