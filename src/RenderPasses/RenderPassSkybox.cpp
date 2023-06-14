#include "Debug.h"
#include "DescriptorManager.h"
#include "MeshResourceManager.h"
#include "PipelineStateBuilder.h"
#include "RenderPassSkybox.h"
#include "RenderResourceManager.h"
#include "SamplerManager.h"
#include "VkRenderDevice.h"

namespace Muyo
{

RenderPassSkybox::~RenderPassSkybox()
{
    if (m_pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
}

void RenderPassSkybox::PrepareRenderPass()
{
    // Resources should have been created

    // color attachment
    RenderTarget* colorTarget = GetRenderResourceManager()->GetResource<RenderTarget>("LIGHTING_OUTPUT");
    m_renderPassParameters.AddAttachment(colorTarget, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, false);

    // depth attachment
    RenderTarget* depthTarget = GetRenderResourceManager()->GetResource<RenderTarget>("GBUFFER_DEPTH");
    m_renderPassParameters.AddAttachment(depthTarget, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL, false);

    // Binding 0: Per view data
    UniformBuffer<PerViewData>* perViewDataUniformBuffer = GetRenderResourceManager()->getUniformBuffer<PerViewData>("perView");
    m_renderPassParameters.AddParameter(perViewDataUniformBuffer, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    // Binding 1: skybox texture
    m_renderPassParameters.AddImageParameter(
        GetRenderResourceManager()->GetColorTarget("irr_cube_map", {0, 0}, VK_FORMAT_B8G8R8A8_UNORM, 1, 6),
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, GetSamplerManager()->getSampler(SAMPLER_1_MIPS));

    m_renderPassParameters.Finalize("Render pass skybox");

    CreatePipeline();
}

void RenderPassSkybox::CreatePipeline()
{
    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(m_renderPassParameters.GetRenderArea()).Build();
    VkRect2D scissorRect;
    scissorRect.offset = {0, 0};
    scissorRect.extent = m_renderPassParameters.GetRenderArea();

    VkPipelineLayout pipelineLayout = m_renderPassParameters.GetPipelineLayout();
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(pipelineLayout), VK_OBJECT_TYPE_PIPELINE_LAYOUT, "Skybox");

    VkShaderModule vertShdr = CreateShaderModule(ReadSpv("shaders/Skybox.vert.spv"));
    VkShaderModule fragShdr = CreateShaderModule(ReadSpv("shaders/Skybox.frag.spv"));

    InputAssemblyStateCIBuilder iaBuilder;
    RasterizationStateCIBuilder rsBuilder;
    rsBuilder.SetCullMode(VK_CULL_MODE_NONE);
    MultisampleStateCIBuilder msBuilder;
    BlendStateCIBuilder blendBuilder;
    blendBuilder.setAttachments(1);

    // Disable depth write, enable depth test with opaque pass
    DepthStencilCIBuilder depthStencilBuilder;
    depthStencilBuilder.setDepthTestEnabled(true).setDepthWriteEnabled(false);

    PipelineStateBuilder builder;

    m_pipeline = builder.setShaderModules({vertShdr, fragShdr})
                     .setVertextInfo({Vertex::getBindingDescription()},
                                     Vertex::getAttributeDescriptions())
                     .setAssembly(iaBuilder.Build())
                     .setViewport(viewport, scissorRect)
                     .setRasterizer(rsBuilder.Build())
                     .setMSAA(msBuilder.Build())
                     .setColorBlending(blendBuilder.Build())
                     .setPipelineLayout(pipelineLayout)
                     .setDepthStencil(depthStencilBuilder.Build())
                     .setRenderPass(m_renderPassParameters.GetRenderPass())
                     .setSubpassIndex(0)
                     .Build(GetRenderDevice()->GetDevice());

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr,
                          nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr,
                          nullptr);

    // Set debug name for the pipeline
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipeline),
                            VK_OBJECT_TYPE_PIPELINE, "Skybox");
}

void RenderPassSkybox::RecordCommandBuffers()
{
    VkCommandBufferBeginInfo beginInfo = {};

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;
    if (m_commandBuffer == VK_NULL_HANDLE)
    {
        m_commandBuffer = GetRenderDevice()->AllocateStaticPrimaryCommandbuffer();
    }
    else
    {
        vkResetCommandBuffer(m_commandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
    }
    VkCommandBuffer& mCommandBuffer = m_commandBuffer;
    vkBeginCommandBuffer(mCommandBuffer, &beginInfo);
    {
        SCOPED_MARKER(mCommandBuffer, "Skybox Pass");
        // Build render pass
        RenderPassBeginInfoBuilder rpbiBuilder;
        std::vector<VkClearValue> vClearValeus = {{{.color = {{0.0f, 0.0f, 0.0f, 0.0f}}},
                                                   {.depthStencil = {1.0f, 0}}}};

        VkRenderPassBeginInfo renderPassBeginInfo =
            rpbiBuilder.setRenderArea(m_renderPassParameters.GetRenderArea())
                .setRenderPass(m_renderPassParameters.GetRenderPass())
                .setFramebuffer(m_renderPassParameters.GetFramebuffer())
                .setClearValues(vClearValeus)
                .Build();

        vkCmdBeginRenderPass(mCommandBuffer, &renderPassBeginInfo,
                             VK_SUBPASS_CONTENTS_INLINE);

        VkDescriptorSet descSet = m_renderPassParameters.AllocateDescriptorSet("skybox");

        {
            // Draw skybox cube
            const MeshVertexResources& meshVertexResources = GetMeshResourceManager()->GetMeshVertexResources();
            const Mesh& cube = GetMeshResourceManager()->GetCube();
            VkDeviceSize offset = 0;
            VkBuffer vertexBuffer = meshVertexResources.m_pVertexBuffer->buffer();
            VkBuffer indexBuffer = meshVertexResources.m_pIndexBuffer->buffer();
            uint32_t nIndexCount = cube.m_nIndexCount;
            uint32_t nIndexOffset = cube.m_nIndexOffset;

            vkCmdBindVertexBuffers(mCommandBuffer, 0, 1, &vertexBuffer,
                                   &offset);
            vkCmdBindIndexBuffer(mCommandBuffer, indexBuffer, 0,
                                 VK_INDEX_TYPE_UINT32);
            vkCmdBindPipeline(mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              m_pipeline);
            vkCmdBindDescriptorSets(
                mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_renderPassParameters.GetPipelineLayout(), 0, 1,
                &descSet, 0, nullptr);
            vkCmdDrawIndexed(mCommandBuffer, nIndexCount, 1, nIndexOffset, 0, 0);
        }
        vkCmdEndRenderPass(mCommandBuffer);
    }
    vkEndCommandBuffer(mCommandBuffer);
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(mCommandBuffer),
                            VK_OBJECT_TYPE_COMMAND_BUFFER, "Skybox");
}

}  // namespace Muyo
