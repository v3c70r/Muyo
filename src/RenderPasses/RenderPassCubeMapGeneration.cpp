#include "RenderPassCubeMapGeneration.h"
#include "Camera.h"
#include "MeshResourceManager.h"
#include "MeshVertex.h"
#include "PipelineStateBuilder.h"
#include "RenderResourceManager.h"
#include "SamplerManager.h"

namespace Muyo
{
RenderPassCubeMapGeneration::~RenderPassCubeMapGeneration()
{
    if (m_pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
}

void RenderPassCubeMapGeneration::PrepareRenderPass()
{
    m_renderPassParameters.SetRenderArea(m_sizePerFace);

    m_renderPassParameters.AddAttachment(GetRenderResourceManager()->GetColorTarget("env_cube_map", m_sizePerFace, TEX_FORMAT, 1, 6), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true);

    // Set 0, Binding 0, perview
    m_renderPassParameters.AddParameter(GetRenderResourceManager()->GetResource<UniformBuffer<PerViewData>>("perView"), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    // Set 1, Binding 0, env map
    m_renderPassParameters.AddImageParameter(GetRenderResourceManager()->GetTexture("env_map", "assets/hdr/Mans_Outside_2k.hdr"), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, GetSamplerManager()->getSampler(SAMPLER_1_MIPS), 1);
    
    // Multiview
    m_renderPassParameters.SetMultiviewMask(0b111111);

    m_renderPassParameters.Finalize("Cube map generation");

    CreatePipeline();
}

void RenderPassCubeMapGeneration::CreatePipeline()
{

    VkPipelineLayout pipelineLayout = m_renderPassParameters.GetPipelineLayout();

    VkShaderModule vertexShader = CreateShaderModule(ReadSpv("shaders/equirectangularToCubeMap.vert.spv"));
    VkShaderModule fragShader = CreateShaderModule(ReadSpv("shaders/equirectangularToCubeMap.frag.spv"));

    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(m_sizePerFace).Build();
    VkRect2D scissorRect;
    scissorRect.offset = { 0, 0 };
    scissorRect.extent = m_sizePerFace;

    InputAssemblyStateCIBuilder iaBuilder;
    RasterizationStateCIBuilder rsBuilder;
    rsBuilder.SetFrontFace(VK_FRONT_FACE_COUNTER_CLOCKWISE);
    MultisampleStateCIBuilder msBuilder;
    BlendStateCIBuilder blendBuilder;
    blendBuilder.setAttachments(1, false);
    DepthStencilCIBuilder depthStencilBuilder;
    PipelineStateBuilder builder;

    m_pipeline =
      builder.setShaderModules({ vertexShader, fragShader })
        .setVertextInfo({ Vertex::getBindingDescription() },
                        Vertex::getAttributeDescriptions())
        .setAssembly(iaBuilder.Build())
        .setViewport(viewport, scissorRect)
        .setRasterizer(rsBuilder.Build())
        .setMSAA(msBuilder.Build())
        .setColorBlending(blendBuilder.Build())
        .setPipelineLayout(pipelineLayout)
        .setDepthStencil(depthStencilBuilder.Build())
        .setRenderPass(m_renderPassParameters.GetRenderPass())
        .Build(GetRenderDevice()->GetDevice());

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertexShader, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShader, nullptr);

    // Set debug name for the pipeline
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipeline), VK_OBJECT_TYPE_PIPELINE, "Cubemap Generation");
}

void RenderPassCubeMapGeneration::RecordCommandBuffers()
{
    // Get skybox vertex data
    const Mesh& skyboxMesh = GetMeshResourceManager()->GetCube();
    const MeshVertexResources& meshVertexResources = GetMeshResourceManager()->GetMeshVertexResources();

    VkBuffer vertexBuffer = meshVertexResources.m_pVertexBuffer->buffer();
    VkBuffer indexBuffer = meshVertexResources.m_pIndexBuffer->buffer();
    uint32_t nIndexCount = skyboxMesh.m_nIndexCount;
    uint32_t nIndexOffset = skyboxMesh.m_nIndexOffset;

    // Begin RenderPass
    RenderPassBeginInfoBuilder rpBuilder;
    std::vector<VkClearValue> clearValue = { { .color = { 0.0, 0.0, 0.0, 0.0 } } };
    VkRenderPassBeginInfo renderPassBeginInfo =
      rpBuilder.setRenderPass(m_renderPassParameters.GetRenderPass())
        .setFramebuffer(m_renderPassParameters.GetFramebuffer())
        .setRenderArea(m_sizePerFace)
        .setClearValues(clearValue)
        .Build();

    ViewportBuilder vpBuilder;
    VkViewport viewport = vpBuilder.setWH(m_sizePerFace) .Build();

    
    VkRect2D scissorRect;
    scissorRect.offset = {0, 0};
    scissorRect.extent = m_sizePerFace;

    VkCommandBufferBeginInfo beginInfo = {};

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    m_commandBuffer = GetRenderDevice()->AllocateStaticPrimaryCommandbuffer();
    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
    {
        SCOPED_MARKER(m_commandBuffer, "CubeMap Generation");
        vkCmdBeginRenderPass(m_commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        std::vector<VkDescriptorSet> descriptorSets = m_renderPassParameters.AllocateDescriptorSets();
        vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_renderPassParameters.GetPipelineLayout(), 0, static_cast<uint32_t>(descriptorSets.size()), descriptorSets.data(), 0, nullptr);

        VkDeviceSize offsets[1] = { 0 };
        vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &vertexBuffer, offsets);

        vkCmdBindIndexBuffer(m_commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(m_commandBuffer, nIndexCount, 1, nIndexOffset, 0, 0);
        vkCmdEndRenderPass(m_commandBuffer);

    }
    vkEndCommandBuffer(m_commandBuffer);
}
}
