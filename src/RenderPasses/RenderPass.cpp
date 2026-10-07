#include "RenderPass.h"

#include "Debug.h"
#include "DescriptorManager.h"
#include "Geometry.h"
#include "MeshResourceManager.h"
#include "PipelineStateBuilder.h"
#include "RenderResourceManager.h"
#include "RenderResourceNames.h"
#include "ResourceBarrier.h"
#include "SamplerManager.h"
#include "VkRenderDevice.h"

namespace Muyo
{

RenderPassFinal::RenderPassFinal(const Swapchain& swapchain, bool bClearAttachments)
{
    m_renderArea = swapchain.GetSwapchainExtent();
    std::vector<std::string> vSwapchainImageResourceNames = swapchain.GetSwapchainResourceNames();
    m_vRenderPassParameters.resize(vSwapchainImageResourceNames.size());
    // For each back buffer, prepare a render pass parameter and a pipeline.
    for (size_t i = 0; i < vSwapchainImageResourceNames.size(); i++)
    {
        // Color attachments
        m_vRenderPassParameters[i].SetRenderArea(m_renderArea);
        m_vRenderPassParameters[i].AddAttachment(
            GetRenderResourceManager()->GetResource<SwapchainImageResource>(vSwapchainImageResourceNames[i]),
            bClearAttachments ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, bClearAttachments);

        // Depth attachment
        m_vRenderPassParameters[i].AddAttachment(
            GetRenderResourceManager()->GetRenderTarget("GBufferDepth_", swapchain.GetSwapchainExtent(),
                                                        VK_FORMAT_D32_SFLOAT),
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, true);

        // Descriptor set 0
        m_vRenderPassParameters[i].AddImageParameter(
            GetRenderResourceManager()->GetRenderTarget(OPAQUE_LIGHTING_OUTPUT_ATTACHMENT_NAME,
                                                        swapchain.GetSwapchainExtent(), VK_FORMAT_R16G16B16A16_SFLOAT),
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, GetSamplerManager()->getSampler(SAMPLER_1_MIPS), 0);
#ifdef FEATURE_RAY_TRACING
        // Descriptor set 1
        // The ray tracing output is a storage image written by the ray tracing pass and
        // read back with imageLoad() in triangle_rt.frag, so it must be bound as a storage
        // image in the GENERAL layout (not a sampled image).
        m_vRenderPassParameters[i].AddImageParameter(
            GetRenderResourceManager()->GetResource<ImageResource>("Ray Tracing Output"),
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_NULL_HANDLE, 1);

        // Descriptor set 2
        // Add perview
        m_vRenderPassParameters[i].AddParameter(GetRenderResourceManager()->GetUniformBuffer<PerViewData>("perView"),
                                                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 2);
#endif

        m_vRenderPassParameters[i].Finalize("FinalPass");

        // Create pipelines
        VkPipelineLayout pipelineLayout = m_vRenderPassParameters[i].GetPipelineLayout();
#ifdef FEATURE_RAY_TRACING
        // The ray tracing final pass blits the ray tracing output with a fullscreen triangle.
        VkShaderModule vertexShader = CreateShaderModule(ReadSpv("shaders/triangle.vert.spv"));
        VkShaderModule fragShader = CreateShaderModule(ReadSpv("shaders/triangle_rt.frag.spv"));
#else
        VkShaderModule vertexShader = CreateShaderModule(ReadSpv("shaders/triangle.vert.spv"));
        VkShaderModule fragShader = CreateShaderModule(ReadSpv("shaders/triangle.frag.spv"));
#endif  // FEATURE_RAY_TRACING

        ViewportBuilder vpBuilder;
        VkViewport viewport = vpBuilder.setWH(swapchain.GetSwapchainExtent()).Build();
        VkRect2D scissorRect;
        scissorRect.offset = {0, 0};
        scissorRect.extent = swapchain.GetSwapchainExtent();

        InputAssemblyStateCIBuilder iaBuilder;
        RasterizationStateCIBuilder rsBuilder;
        MultisampleStateCIBuilder msBuilder;
        BlendStateCIBuilder blendBuilder;
        blendBuilder.setAttachments(1, false);
        DepthStencilCIBuilder depthStencilBuilder;
        PipelineStateBuilder builder;

        m_vPipelines.push_back(
            builder.setShaderModules({vertexShader, fragShader})
                .setVertextInfo({Vertex::getBindingDescription()}, Vertex::getAttributeDescriptions())
                .setAssembly(iaBuilder.Build())
                .setViewport(viewport, scissorRect)
                .setRasterizer(rsBuilder.Build())
                .setMSAA(msBuilder.Build())
                .setColorBlending(blendBuilder.Build())
                .setPipelineLayout(pipelineLayout)
                .setDepthStencil(depthStencilBuilder.Build())
                .setRenderPass(m_vRenderPassParameters[i].GetRenderPass())
                .Build(GetRenderDevice()->GetDevice()));

        vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertexShader, nullptr);
        vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShader, nullptr);

        // Set debug name for the pipeline
        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_vPipelines.back()), VK_OBJECT_TYPE_PIPELINE,
                                "Final pass " + std::to_string(i));
    }
}

RenderPassFinal::~RenderPassFinal()
{
    for (auto& pipeline : m_vPipelines)
    {
        vkDestroyPipeline(GetRenderDevice()->GetDevice(), pipeline, nullptr);
    }
}

void RenderPassFinal::RecordCommandBuffers()
{
    VkCommandBufferBeginInfo beginInfo = {};

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    // Resolve inputs at record time: the ray tracing output is created by
    // RenderPassRayTracing::PrepareRenderPass(), which runs after this pass is constructed.
    std::vector<const IRenderResource*> vSet0Resources = {
        GetRenderResourceManager()->GetResource<ImageResource>(OPAQUE_LIGHTING_OUTPUT_ATTACHMENT_NAME)};
#ifdef FEATURE_RAY_TRACING
    std::vector<const IRenderResource*> vSet1Resources = {
        GetRenderResourceManager()->GetResource<ImageResource>("Ray Tracing Output")};
    std::vector<const IRenderResource*> vSet2Resources = {
        GetRenderResourceManager()->GetResource<UniformBuffer<PerViewData>>("perView")};
#endif

    for (size_t i = 0; i < m_vRenderPassParameters.size(); i++)
    {
        VkCommandBuffer curCmdBuf = VK_NULL_HANDLE;
        if (m_vCommandBuffers.size() <= i)
        {
            curCmdBuf = GetRenderDevice()->AllocateStaticPrimaryCommandbuffer();
            m_vCommandBuffers.push_back(curCmdBuf);
        }
        else
        {
            vkResetCommandBuffer(m_vCommandBuffers[i], VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
            curCmdBuf = m_vCommandBuffers[i];
        }

        vkBeginCommandBuffer(curCmdBuf, &beginInfo);
        {
            SCOPED_MARKER(m_vCommandBuffers[i], "Final pass " + std::to_string(i));

            std::vector<VkClearValue> clearValues = {{.color = {0.0f, 0.0f, 0.0f, 0.0f}}, {.depthStencil = {1.0f, 0}}};

            RenderPassParameters& renderPassParameters = m_vRenderPassParameters[i];
            RenderPassBeginInfoBuilder builder;
            VkRenderPassBeginInfo renderPassBeginInfo = builder.setRenderPass(renderPassParameters.GetRenderPass())
                                                            .setFramebuffer(renderPassParameters.GetFramebuffer())
                                                            .setRenderArea(m_renderArea)
                                                            .setClearValues(clearValues)
                                                            .Build();

            vkCmdBeginRenderPass(curCmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            {
                // Allocate the descriptor sets from this pass's own layouts so that they are
                // compatible with GetPipelineLayout() (the descriptor set layouts must be
                // identically defined, including stageFlags).
                std::vector<VkDescriptorSet> descSets;
                descSets.push_back(renderPassParameters.AllocateDescriptorSet("Final pass set 0", vSet0Resources, 0));
#ifdef FEATURE_RAY_TRACING
                descSets.push_back(renderPassParameters.AllocateDescriptorSet("Final pass set 1", vSet1Resources, 1));
                descSets.push_back(renderPassParameters.AllocateDescriptorSet("Final pass set 2", vSet2Resources, 2));
#endif

                const Mesh& quadMesh = GetMeshResourceManager()->GetQuad();
                const MeshVertexResources& meshVertexResources = GetMeshResourceManager()->GetMeshVertexResources();
                VkDeviceSize offset = 0;
                VkBuffer vertexBuffer = meshVertexResources.m_pVertexBuffer->buffer();
                VkBuffer indexBuffer = meshVertexResources.m_pIndexBuffer->buffer();
                uint32_t nIndexCount = quadMesh.m_nIndexCount;
                uint32_t nIndexOffset = quadMesh.m_nIndexOffset;

                vkCmdBindVertexBuffers(curCmdBuf, 0, 1, &vertexBuffer, &offset);
                vkCmdBindIndexBuffer(curCmdBuf, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdBindPipeline(curCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_vPipelines[i]);
                vkCmdBindDescriptorSets(curCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        renderPassParameters.GetPipelineLayout(), 0,
                                        static_cast<uint32_t>(descSets.size()), descSets.data(), 0, nullptr);
                vkCmdDrawIndexed(curCmdBuf, nIndexCount, 1, nIndexOffset, 0, 0);
            }
            vkCmdEndRenderPass(curCmdBuf);
        }
        vkEndCommandBuffer(curCmdBuf);

        setDebugUtilsObjectName(reinterpret_cast<uint64_t>(curCmdBuf), VK_OBJECT_TYPE_COMMAND_BUFFER, "Final");
    }
}
}  // namespace Muyo
