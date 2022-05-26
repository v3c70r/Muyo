#include "RenderPassRayTracing.h"
#include "RenderResourceManager.h"
#include "Camera.h"
#include "RayTracingSceneManager.h"
#include "Scene.h"
#include "DescriptorManager.h"
#include "ResourceBarrier.h"

void RenderPassRayTracing::RecordCommandBuffer(VkExtent2D imgSize, 
        const VkStridedDeviceAddressRegionKHR* pRayGenRegion,
        const VkStridedDeviceAddressRegionKHR* pRayMissRegion,
        const VkStridedDeviceAddressRegionKHR* pRayHitRegion,
        VkPipelineLayout rayTracingPipelineLayout,
        VkPipeline rayTracingPipeline
        )

{
    const auto* pStorageImageRes = GetRenderResourceManager()->GetStorageImageResource("Ray Tracing Output", imgSize, VK_FORMAT_R16G16B16A16_SFLOAT);
    // clear storage image

    const UniformBuffer<PerViewData>* perView = GetRenderResourceManager()->getUniformBuffer<PerViewData>("perView");
    StorageBuffer<PrimitiveDescription>* primDescBuffer = GetRenderResourceManager()->GetResource<StorageBuffer<PrimitiveDescription>>("primitive descs");
    const StorageBuffer<LightData>* lightDataBuffer = GetRenderResourceManager()->GetResource<StorageBuffer<LightData>>("light data");

    static const std::string TLAS_NAME = "TLAS";

    AccelerationStructure* pTLAS = GetRenderResourceManager()->GetResource<AccelerationStructure>(TLAS_NAME);
    std::vector<VkDescriptorSet> vDescSets =
	{
		GetDescriptorManager()->AllocatePerviewDataDescriptorSet(*perView),
		GetDescriptorManager()->AllocateRayTracingDescriptorSet(pTLAS->GetAccelerationStructure(), pStorageImageRes->getView(), *primDescBuffer, GetTextureManager()->GetTextures()),
		// IBL descriptor sets
		GetDescriptorManager()->AllocateIBLDescriptorSet(
			GetRenderResourceManager()
				->GetColorTarget("irr_cube_map", {0, 0}, VK_FORMAT_B8G8R8A8_UNORM, 1, 6)
				->getView(),
			GetRenderResourceManager()
				->GetColorTarget("prefiltered_cubemap", {0, 0}, VK_FORMAT_B8G8R8A8_UNORM, 1, 6)
				->getView(),
			GetRenderResourceManager()
				->GetColorTarget("specular_brdf_lut", {0, 0}, VK_FORMAT_R32G32_SFLOAT, 1, 1)
				->getView()),
        // Light data buffer
        GetDescriptorManager()->AllocateLightDataDescriptorSet(lightDataBuffer->GetNumStructs(), *lightDataBuffer)
    };

    m_commandBuffer = GetRenderDevice()->AllocateReusablePrimaryCommandbuffer();

    {
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        beginInfo.pInheritanceInfo = nullptr;
        vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
        {
            // Transit output image to general layout using Image memory barrier 2

            VkImage outputImage = pStorageImageRes->getImage();
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
            VkDependencyInfoKHR dependency =
                {
                    VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,  //sType;
                    nullptr,                                //pNext;
                    0,                                      //dependencyFlags;
                    0,                                      //memoryBarrierCount;
                    nullptr,                                //pMemoryBarriers;
                    0,                                      //bufferMemoryBarrierCount;
                    nullptr,                                //pBufferMemoryBarriers;
                    1,                                      //imageMemoryBarrierCount;
                    &imgBarrier                             //pImageMemoryBarriers;
                };
            VkExt::vkCmdPipelineBarrier2KHR(m_commandBuffer, &dependency);
#else
            ImageResourceBarrier barrier(outputImage, VK_IMAGE_LAYOUT_GENERAL);
            GetRenderDevice()->AddResourceBarrier(m_commandBuffer, barrier);
#endif
        };

        SCOPED_MARKER(m_commandBuffer, "Trace Ray");
        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rayTracingPipeline);
        vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rayTracingPipelineLayout, 0, (uint32_t)vDescSets.size(), vDescSets.data(), 0, nullptr);

        VkStridedDeviceAddressRegionKHR callable = {};

        VkExt::vkCmdTraceRaysKHR(
            m_commandBuffer,
            pRayGenRegion,
            pRayMissRegion,
            pRayHitRegion,
            &callable,
            imgSize.width, imgSize.height, 1);

        vkEndCommandBuffer(m_commandBuffer);
    }
}
