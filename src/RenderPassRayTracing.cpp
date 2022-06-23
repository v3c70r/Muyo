#include "RenderPassRayTracing.h"

#include <vulkan/vulkan_core.h>

#include "Camera.h"
#include "PipelineStateBuilder.h"
#include "RayTracingSceneManager.h"
#include "RenderResourceManager.h"
#include "ResourceBarrier.h"
#include "SamplerManager.h"
#include "Scene.h"

void RenderPassRayTracing::PrepareRenderPass()
{
    // Prepare render pass parameters
    StorageBuffer<PrimitiveDescription>* pPrimDescBuffer = GetRenderResourceManager()->GetResource<StorageBuffer<PrimitiveDescription>>("primitive descs");

    // Binding 0: PerViewData
    UniformBuffer<PerViewData>* perViewDataUniformBuffer = GetRenderResourceManager()->GetResource<UniformBuffer<PerViewData>>("perView");
    m_renderPassParameters.AddParameter(perViewDataUniformBuffer, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);

    // Binding 1: TLAS
    AccelerationStructure* pTLAS = GetRenderResourceManager()->GetResource<AccelerationStructure>("TLAS");
    m_renderPassParameters.AddParameter(pTLAS, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);

    // Binding 2: Accumulation buffer
    m_renderPassParameters.AddImageParameter(
        GetRenderResourceManager()->GetStorageImageResource("Ray Tracing Accumulated", m_imageSize, VK_FORMAT_R16G16B16A16_SFLOAT),
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR, VK_IMAGE_LAYOUT_GENERAL);

    // Binding 3: Ray tracing output
    m_renderPassParameters.AddImageParameter(
        GetRenderResourceManager()->GetStorageImageResource("Ray Tracing Output", m_imageSize, VK_FORMAT_R16G16B16A16_SFLOAT),
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR, VK_IMAGE_LAYOUT_GENERAL);

    // Binding 4: Primitive descriptions
    m_renderPassParameters.AddParameter(pPrimDescBuffer, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);

    // Binding 5: All of textures in materials
    const auto& vpUniquePtrTextures = GetTextureManager()->GetTextures();
    std::vector<const ImageResource*> vpTextures;
    vpTextures.reserve(vpUniquePtrTextures.size());
    for (const auto& pTexture : vpUniquePtrTextures)
    {
        vpTextures.push_back(pTexture.get());
    }
    m_renderPassParameters.AddImageParameter(vpTextures, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, GetSamplerManager()->getSampler(SAMPLER_1_MIPS));

    // Binding 6: Environment map
    m_renderPassParameters.AddImageParameter(
        GetRenderResourceManager()->GetResource<RenderTarget>("env_cube_map"),
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, GetSamplerManager()->getSampler(SAMPLER_1_MIPS));

    CreatePipeline();
}

void RenderPassRayTracing::CreatePipeline()
{
    // shader stages
    VkShaderModule rayGenShdr = CreateShaderModule(ReadSpv("shaders/pathTracing.rgen.spv"));
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(rayGenShdr), VK_OBJECT_TYPE_SHADER_MODULE, "pathTracing.rgen.spv");

    VkShaderModule missShdr = CreateShaderModule(ReadSpv("shaders/pathTracing.rmiss.spv"));
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(missShdr), VK_OBJECT_TYPE_SHADER_MODULE, "pathTracing.rmiss.spv");

    VkShaderModule chitShdr = CreateShaderModule(ReadSpv("shaders/pathTracing.rchit.spv"));
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(chitShdr), VK_OBJECT_TYPE_SHADER_MODULE, "pathTracing.rchit.spv");

    RayTracingPipelineBuilder builder;
    builder.AddShaderModule(rayGenShdr, VK_SHADER_STAGE_RAYGEN_BIT_KHR)
        .AddShaderModule(missShdr, VK_SHADER_STAGE_MISS_BIT_KHR)
        .AddShaderModule(chitShdr, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);

    // pipeline layout
    std::vector<VkPushConstantRange> pushConstants = {};
     m_renderPassParameters.CreateDescriptorSetLayout();

    std::vector<VkDescriptorSetLayout> descLayouts = {
        m_renderPassParameters.GetDescriptorSetLayout(),
        GetDescriptorManager()->getDescriptorLayout(DESCRIPTOR_LAYOUT_LIGHT_DATA)};

    m_pipelineLayout = GetRenderDevice()->CreatePipelineLayout(descLayouts, pushConstants);
    setDebugUtilsObjectName(reinterpret_cast<uint64_t>(m_pipelineLayout), VK_OBJECT_TYPE_PIPELINE_LAYOUT, "Ray Tracing");
    builder.SetPipelineLayout(m_pipelineLayout).SetMaxRecursionDepth(2);
    VkRayTracingPipelineCreateInfoKHR createInfo = builder.Build();

    VK_ASSERT(VkExt::vkCreateRayTracingPipelinesKHR(GetRenderDevice()->GetDevice(),
                                                    VK_NULL_HANDLE,
                                                    VK_NULL_HANDLE,
                                                    1,
                                                    &createInfo,
                                                    nullptr,
                                                    &m_pipeline));

    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), rayGenShdr, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), missShdr, nullptr);
    vkDestroyShaderModule(GetRenderDevice()->GetDevice(), chitShdr, nullptr);

    AllocateShaderBindingTable();
}

void RenderPassRayTracing::DestroyPipeline()
{
    vkDestroyPipelineLayout(GetRenderDevice()->GetDevice(), m_pipelineLayout, nullptr);
    vkDestroyPipeline(GetRenderDevice()->GetDevice(), m_pipeline, nullptr);
    m_pipelineLayout = VK_NULL_HANDLE;
    m_pipeline = VK_NULL_HANDLE;
}

void RenderPassRayTracing::RecordCommandBuffer()
{


    const StorageBuffer<LightData>* lightDataBuffer = GetRenderResourceManager()->GetResource<StorageBuffer<LightData>>("light data");

    std::vector<VkDescriptorSet> vDescSets =
        {
            m_renderPassParameters.AllocateDescriptorSet("Ray Tracing Pass"),
            GetDescriptorManager()->AllocateLightDataDescriptorSet(lightDataBuffer->GetNumStructs(), *lightDataBuffer)};

    m_commandBuffer = GetRenderDevice()->AllocateReusablePrimaryCommandbuffer();


    {
        StorageImageResource* pOutputImage = GetRenderResourceManager()->GetResource<StorageImageResource>("Ray Tracing Output");

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        beginInfo.pInheritanceInfo = nullptr;
        vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
        {
            // Transit output image to general layout using Image memory barrier 2

            VkImage outputImage = pOutputImage->getImage();
#ifdef FEATURE_SYNCHRONIZATION2
            VkImageMemoryBarrier2KHR imgBarrier = {
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,  //VkStructureType             sType;
                nullptr,                                       //const void*                 pNext;
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,             //VkPipelineStageFlags2KHR    srcStageMask;
                0,                                             //VkAccessFlags2KHR           srcAccessMask;
                VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,  //VkPipelineStageFlags2KHR    dstStageMask;
                0,                                             //VkAccessFlags2KHR           dstAccessMask;
                VK_IMAGE_LAYOUT_UNDEFINED,                     //VkImageLayout               oldLayout;
                VK_IMAGE_LAYOUT_GENERAL,                       //VkImageLayout               newLayout;
                VK_QUEUE_FAMILY_IGNORED,                       //uint32_t                    srcQueueFamilyIndex;
                VK_QUEUE_FAMILY_IGNORED,                       //uint32_t                    dstQueueFamilyIndex;
                outputImage,                                   //VkImage                     image;
                {
                    //VkImageSubresourceRange     subresourceRange;
                    VK_IMAGE_ASPECT_COLOR_BIT,  //VkImageAspectFlags    aspectMask;
                    0,                          //uint32_t              baseMipLevel;
                    1,                          //uint32_t              levelCount;
                    0,                          //uint32_t              baseArrayLayer;
                    1,                          //uint32_t              layerCount;
                },
            };

            StorageImageResource* pAccumulatedStorage = GetRenderResourceManager()->GetResource<StorageImageResource>("Ray Tracing Accumulated");
            VkImageMemoryBarrier2KHR accumulatedImageBarrier = imgBarrier;
            accumulatedImageBarrier.image = pAccumulatedStorage->getImage();
            accumulatedImageBarrier.newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;

            std::array<VkImageMemoryBarrier2KHR, 2> barriers = { imgBarrier, accumulatedImageBarrier };
            VkDependencyInfoKHR dependency =
                {
                    VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,   // sType;
                    nullptr,                                 // pNext;
                    0,                                       // dependencyFlags;
                    0,                                       // memoryBarrierCount;
                    nullptr,                                 // pMemoryBarriers;
                    0,                                       // bufferMemoryBarrierCount;
                    nullptr,                                 // pBufferMemoryBarriers;
                    static_cast<uint32_t>(barriers.size()),  // imageMemoryBarrierCount;
                    barriers.data()                          // pImageMemoryBarriers;
                };
            VkExt::vkCmdPipelineBarrier2KHR(m_commandBuffer, &dependency);
#else
            ImageResourceBarrier barrier(outputImage, VK_IMAGE_LAYOUT_GENERAL);
            ImageResourceBarrier accumulatedImageBarrier(pAccumulatedStorage->getImage, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
            GetRenderDevice()->AddResourceBarrier(m_commandBuffer, barrier);
            GetRenderDevice()->AddResourceBarrier(m_commandBuffer, accumulatedImageBarrier);
#endif

        SCOPED_MARKER(m_commandBuffer, "Trace Ray");
        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipeline);
        vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipelineLayout, 0, (uint32_t)vDescSets.size(), vDescSets.data(), 0, nullptr);

        VkStridedDeviceAddressRegionKHR callable = {};

        VkExt::vkCmdTraceRaysKHR(
            m_commandBuffer,
            &m_aSBTRegions[SBT_REGION_RAY_GEN],
            &m_aSBTRegions[SBT_REGION_RAY_MISS],
            &m_aSBTRegions[SBT_REGION_RAY_HIT],
            &callable,
            m_imageSize.width, m_imageSize.height, 1);

        // Copy current output to accumulated output
        VkImageCopy copyInfo = {};
        copyInfo.extent.height = m_imageSize.height;
        copyInfo.extent.width = m_imageSize.width;
        copyInfo.extent.depth = 1;
        copyInfo.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyInfo.srcSubresource.layerCount = 1;
        copyInfo.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyInfo.dstSubresource.layerCount = 1;
        
        ImageResourceBarrier accumulatedImageBarrierCopy(pAccumulatedStorage->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        GetRenderDevice()->AddResourceBarrier(m_commandBuffer, accumulatedImageBarrierCopy);

        vkCmdCopyImage(m_commandBuffer,
                pOutputImage->getImage(), VK_IMAGE_LAYOUT_GENERAL,
                pAccumulatedStorage->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &copyInfo);


        };
        vkEndCommandBuffer(m_commandBuffer);
    }
}


uint32_t AlignUp(uint32_t nSize, uint32_t nAlignment)
{
    return (nSize + nAlignment - 1) / nAlignment * nAlignment;
}

void RenderPassRayTracing::AllocateShaderBindingTable()
{
    // https://www.willusher.io/graphics/2019/11/20/the-sbt-three-ways
    // https://nvpro-samples.github.io/vk_raytracing_tutorial_KHR/#shaderbindingtable

    uint32_t nMissGroupCount = 1;
    uint32_t nHitGroupCount = 1;
    uint32_t nRayGenGroupCount = 1;      // can have only one

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR raytracingProperties = {};
    GetRenderDevice()->GetPhysicalDeviceProperties(raytracingProperties);

    uint32_t nHandleSize = raytracingProperties.shaderGroupHandleSize;
    uint32_t nHandleSizeAligned = AlignUp(nHandleSize, raytracingProperties.shaderGroupHandleAlignment);


    // ray gen shader takes a shader group base alignment
    // There should be just 1 ray gen shader
    VkStridedDeviceAddressRegionKHR& rgenRegion = m_aSBTRegions[SBT_REGION_RAY_GEN];
    rgenRegion.stride = AlignUp(nHandleSizeAligned, raytracingProperties.shaderGroupBaseAlignment);
    rgenRegion.size = rgenRegion.stride;

    VkStridedDeviceAddressRegionKHR& hitRegion = m_aSBTRegions[SBT_REGION_RAY_HIT];
    hitRegion.stride = nHandleSizeAligned;
    hitRegion.size = AlignUp(nHitGroupCount * nHandleSizeAligned, raytracingProperties.shaderGroupBaseAlignment);

    VkStridedDeviceAddressRegionKHR& missRegion = m_aSBTRegions[SBT_REGION_RAY_MISS];
    missRegion.stride = nHandleSizeAligned;
    missRegion.size = AlignUp(nMissGroupCount * nHandleSizeAligned, raytracingProperties.shaderGroupBaseAlignment);

    uint32_t sbtSize = rgenRegion.size + hitRegion.size + missRegion.size;


    uint32_t nHandleCount = nRayGenGroupCount + nMissGroupCount + nHitGroupCount;
    uint32_t nDataSize = nHandleCount * nHandleSize;
    std::vector<uint8_t> handleBuffer(nDataSize);
    (VkExt::vkGetRayTracingShaderGroupHandlesKHR(GetRenderDevice()->GetDevice(), m_pipeline, 0, nHandleCount, nDataSize, handleBuffer.data()));
    ShaderBindingTableBuffer *pSBTBuffer = GetRenderResourceManager()->GetShaderBindingTableBuffer("SBT", sbtSize);

    // Copy handles to GPU
    VkDeviceAddress SBTAddress = GetRenderDevice()->GetBufferDeviceAddress(pSBTBuffer->buffer());
    rgenRegion.deviceAddress = SBTAddress;
    missRegion.deviceAddress = SBTAddress + rgenRegion.size;
    hitRegion.deviceAddress = missRegion.deviceAddress + missRegion.size;

    uint8_t* pRayGenHandleGPU = (uint8_t*)pSBTBuffer->Map();
    uint32_t nHandleOffset = 0;

    // copy ray gen at begining of the handle data
    memcpy(pRayGenHandleGPU, handleBuffer.data(), nHandleSize);


    // copy miss
    nHandleOffset += nHandleSize;
    uint8_t* pRayMissHandleGPU = pRayGenHandleGPU + rgenRegion.size;
    for (uint32_t i = 0; i < nMissGroupCount; i++)
    {
        memcpy(pRayMissHandleGPU, &(handleBuffer[nHandleOffset]), nHandleSize);
        nHandleOffset += nHandleSize;
        pRayMissHandleGPU += missRegion.stride;
    }

    // Copy Hit
    uint8_t* pRayHitHandleGPU = pRayGenHandleGPU + rgenRegion.size + missRegion.size;
    for (uint32_t i = 0; i < nHitGroupCount; i++)
    {
        memcpy(pRayHitHandleGPU, &(handleBuffer[nHandleOffset]), nHandleSize);
        nHandleOffset += nHandleSize;
        pRayHitHandleGPU += hitRegion.stride;
    }

    pSBTBuffer->Unmap();
}
