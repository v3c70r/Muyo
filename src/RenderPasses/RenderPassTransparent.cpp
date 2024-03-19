#include "RenderPassTransparent.h"

#include <cassert>

#include "Debug.h"
#include "DescriptorManager.h"
#include "PerObjResourceManager.h"
#include "PipelineStateBuilder.h"
#include "RenderResourceManager.h"
#include "SamplerManager.h"
#include "VkRenderDevice.h"
#include "MeshResourceManager.h"

namespace Muyo
{

RenderPassTransparent::RenderPassTransparent(VkExtent2D renderArea) : m_renderArea(renderArea)
{
    CreateRenderPasses();
}

void RenderPassTransparent::PrepareRenderPass()
{
    m_renderPassParameters.SetRenderArea(m_renderArea);

    // Attachments
    auto* colorAttachment = GetRenderResourceManager()->GetResource<RenderTarget>("opaqueLightingOutput");
    m_renderPassParameters.AddAttachment(colorAttachment, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, false);

    auto* depthAttachment = GetRenderResourceManager()->GetResource<RenderTarget>("GBufferDepth_");
    m_renderPassParameters.AddAttachment(depthAttachment, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, false);

    // Shader Uniform buffers:
    //
    // Set 0: CAMERA_UBO(0)
    //
    // Set 1: layout(scalar, set = 1, binding = 0) buffer PerObjData_ { PerObjData i[]; }perObjData;
    //
    // Set 2: IBL textures
    // layout(set = 2, binding = 0) uniform samplerCube irradianceMap;
    // layout(set = 2, binding = 1) uniform samplerCube prefilteredMap;
    // layout(set = 2, binding = 2) uniform sampler2D specularBrdfLut;
    //
    // Set 3:
    //
    // All textures and material
    //
    // Set 4: Light UBO
    //
    // Set0, Binding 0
    // Camera UBO
    const UniformBuffer<PerViewData>* perView = GetRenderResourceManager()->GetUniformBuffer<PerViewData>("perView");
    m_renderPassParameters.AddParameter(perView, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);

    // Set 1
    m_renderPassParameters.AddParameter(GetPerObjResourceManager()->GetPerObjResource(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1);

    // Set 2
    const auto& vpUniquePtrTextures = GetTextureResourceManager()->GetTextures();
    std::vector<const ImageResource*> vpTextures;
    vpTextures.reserve(vpUniquePtrTextures.size());
    for (const auto& pTexture : vpUniquePtrTextures)
    {
        vpTextures.push_back(pTexture.get());
    }
    m_renderPassParameters.AddImageParameter(vpTextures, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, GetSamplerManager()->getSampler(SAMPLER_1_MIPS), 1);

    // Set 3
    const auto* materialBuffer = GetMaterialManager()->GetMaterialBuffer();
    m_renderPassParameters.AddParameter(materialBuffer, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 1);

    // Set 2: IBL textures
    const RenderTarget* pIrradianceMap = GetRenderResourceManager()->GetRenderTarget("irr_cube_map", m_renderArea, VK_FORMAT_R16G16B16A16_SFLOAT);
    const RenderTarget* pPrefilteredCubeMap = GetRenderResourceManager()->GetRenderTarget("prefiltered_cubemap", m_renderArea, VK_FORMAT_R16G16B16A16_SFLOAT);
    const RenderTarget* pSpecularBrdfLut = GetRenderResourceManager()->GetRenderTarget("specular_brdf_lut", m_renderArea, VK_FORMAT_R16G16B16A16_SFLOAT);

    m_renderPassParameters.AddImageParameter(pIrradianceMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, GetSamplerManager()->getSampler(SAMPLER_1_MIPS), 2);
    m_renderPassParameters.AddImageParameter(pPrefilteredCubeMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, GetSamplerManager()->getSampler(SAMPLER_8_MIPS), 2);
    m_renderPassParameters.AddImageParameter(pSpecularBrdfLut, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, GetSamplerManager()->getSampler(SAMPLER_8_MIPS), 2);

 
    // Set 3, binding 0 PerObjData
    m_renderPassParameters.AddParameter(GetPerObjResourceManager()->GetPerObjResource(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 3);

    m_renderPassParameters.Finalize("transparent pass");


}

void RenderPassTransparent::CreateRenderPasses()
{
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

    // Lighting output attachment Attachment
    std::array<VkAttachmentDescription, 2> attDescs;
    {
        VkAttachmentDescription& attDesc = attDescs[0];
        attDesc = {};
        attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    {
        VkAttachmentDescription& attDesc = attDescs[1];
        attDesc = {};
        attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attDesc.initialLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL;
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

    assert(vkCreateRenderPass(GetRenderDevice()->GetDevice(), &renderPassInfo,
                              nullptr, &m_renderPass) == VK_SUCCESS);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_renderPass),
                            VK_OBJECT_TYPE_RENDER_PASS, "Transparent Pass");
}

RenderPassTransparent::~RenderPassTransparent()
{
    vkDestroyRenderPass(GetRenderDevice()->GetDevice(), m_renderPass, nullptr);
    DestroyFramebuffer();
    DestroyPipeline();
}

void RenderPassTransparent::CreateFramebuffer(uint32_t uWidth, uint32_t uHeight)
{
    VkExtent2D vp = {uWidth, uHeight};
    std::array<VkImageView, 2> views = {
        GetRenderResourceManager()->GetColorTarget("opaqueLightingOutput", vp)->getView(),
        GetRenderResourceManager()->GetDepthTarget("GBufferDepth_", vp)->getView()};

    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_renderPass;
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
    VkViewport viewport = vpBuilder.setWH(m_renderArea).Build();
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
            DESCRIPTOR_LAYOUT_PER_OBJ_DATA)};

    std::vector<VkPushConstantRange> pushConstants;

    m_pipelineLayout = GetRenderDevice()->CreatePipelineLayout(descLayouts, pushConstants);

    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipelineLayout), VK_OBJECT_TYPE_PIPELINE_LAYOUT, "Transparent");

    // TODO : Write a shader for transparent pass
    VkShaderModule vertShdr =
        CreateShaderModule(ReadSpv("shaders/GBuffer.vert.spv"));
    VkShaderModule fragShdr =
        CreateShaderModule(ReadSpv("shaders/transparent.frag.spv"));

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

    m_pipeline =
        builder.setShaderModules({vertShdr, fragShdr})
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
            .setSubpassIndex(0)
            .Build(GetRenderDevice()->GetDevice());

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), vertShdr,
                          nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), fragShdr,
                          nullptr);

    // Set debug name for the pipeline
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipeline),
                            VK_OBJECT_TYPE_PIPELINE, "Transparent");
}

void RenderPassTransparent::DestroyPipeline()
{
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(), m_pipelineLayout, nullptr);
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr);
    m_pipelineLayout = VK_NULL_HANDLE;
    m_pipeline = VK_NULL_HANDLE;
}

void RenderPassTransparent::RecordCommandBuffers(const std::vector<const Geometry*>& vpGeometries)
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
    VkCommandBuffer mCommandBuffer = m_commandBuffer;
    vkBeginCommandBuffer(mCommandBuffer, &beginInfo);
    {
        SCOPED_MARKER(mCommandBuffer, "Transparent Pass");
        // Build render pass
        RenderPassBeginInfoBuilder rpbiBuilder;
        std::vector<VkClearValue> vClearValeus = {{{.color = {0.0f, 0.0f, 0.0f, 0.0f}},
                                                   {.depthStencil = {1.0f, 0}}}};

        VkRenderPassBeginInfo renderPassBeginInfo =
            rpbiBuilder.setRenderArea(m_renderArea)
                .setRenderPass(m_renderPass)
                .setFramebuffer(m_frameBuffer)
                .setClearValues(vClearValeus)
                .Build();

        vkCmdBeginRenderPass(mCommandBuffer, &renderPassBeginInfo,
                             VK_SUBPASS_CONTENTS_INLINE);

        // Allocate perview constant buffer
        const UniformBuffer<PerViewData>* perView =
            GetRenderResourceManager()->GetUniformBuffer<PerViewData>("perView");

        VkDescriptorSet perViewSets =
            GetDescriptorManager()->AllocatePerviewDataDescriptorSet(*perView);

        {
            // Handle Geometires
            for (const Geometry* pGeometry : vpGeometries)
            {
                // each geometry has their own transformation
                // TODO: Update perViewSets buffer data based on the geometries
                // transformation
                for (const auto& pSubmesh : pGeometry->getSubmeshes())
                {
                    VkDescriptorSet materialDescSet = GetMaterialManager()->GetDefaultMaterial().GetDescriptorSet();
                    if (pSubmesh->HasMaterial())
                    {
                        materialDescSet = pSubmesh->GetMaterial().GetDescriptorSet();
                    }
                    const UniformBuffer<glm::mat4>* worldMatrixBuffer = pGeometry->GetWorldMatrixBuffer();
                    assert(worldMatrixBuffer != nullptr && "Buffer must be valid");
                    VkDescriptorSet worldMatrixDescSet = GetDescriptorManager()->AllocateUniformBufferDescriptorSet(*worldMatrixBuffer, 0, GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_PER_OBJ_DATA));

                    std::vector<VkDescriptorSet> vGBufferDescSets = {perViewSets,
                                                                     materialDescSet,
                                                                     worldMatrixDescSet};

                    const Mesh& mesh = GetMeshResourceManager()->GetMesh(pSubmesh->GetMeshIndex());
                    const MeshVertexResources& vertexResource = GetMeshResourceManager()->GetMeshVertexResources();
                    VkDeviceSize offset = 0;
                    VkBuffer vertexBuffer = vertexResource.m_pVertexBuffer->buffer();
                    VkBuffer indexBuffer = vertexResource.m_pIndexBuffer->buffer();
                    uint32_t nIndexCount = mesh.m_nIndexCount;
                    uint32_t nIndexOffset = mesh.m_nIndexOffset;

                   vkCmdBindVertexBuffers(mCommandBuffer, 0, 1, &vertexBuffer,
                                           &offset);
                    vkCmdBindIndexBuffer(mCommandBuffer, indexBuffer, 0,
                                         VK_INDEX_TYPE_UINT32);
                    vkCmdBindPipeline(mCommandBuffer,
                                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      m_pipeline);
                    vkCmdBindDescriptorSets(
                        mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        m_pipelineLayout, 0, vGBufferDescSets.size(),
                        vGBufferDescSets.data(), 0, nullptr);
                    vkCmdDrawIndexed(mCommandBuffer, nIndexCount, 1, nIndexOffset, 0, 0);
                }
            }
        }
        vkCmdEndRenderPass(mCommandBuffer);
    }
    vkEndCommandBuffer(mCommandBuffer);
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(mCommandBuffer),
                            VK_OBJECT_TYPE_COMMAND_BUFFER, "Transparent");
}

}  // namespace Muyo
