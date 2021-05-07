#include "ImageStorageResource.h"
#include "Texture.h"
#include <vulkan/vulkan_core.h>

ImageStorageResource::ImageStorageResource(VkFormat format, uint32_t nWidth, uint32_t nHeight)
{
    m_imageInfo.imageType = VK_IMAGE_TYPE_2D;
    m_imageInfo.extent.width = nWidth;
    m_imageInfo.extent.height = nHeight;
    m_imageInfo.extent.depth = 1;
    m_imageInfo.mipLevels = 1;
    m_imageInfo.arrayLayers = 1;
    m_imageInfo.format = format;
    m_imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    m_imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    m_imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT;
    m_imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    CreateImageInternal(VMA_MEMORY_USAGE_GPU_ONLY);
    assert(m_image != VK_NULL_HANDLE && "Failed to allocate image");

    // Create Image View
    m_imageViewInfo.image = m_image;
    m_imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    m_imageViewInfo.format = format;

    // Swizzles
    m_imageViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    m_imageViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    m_imageViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    m_imageViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    // subresource
    m_imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    m_imageViewInfo.subresourceRange.baseMipLevel = 0;
    m_imageViewInfo.subresourceRange.levelCount = 1;
    m_imageViewInfo.subresourceRange.baseArrayLayer = 0;
    m_imageViewInfo.subresourceRange.layerCount = 1;
    CreateImageViewInternal();

    // Try synchronization 2
    // https://github.com/KhronosGroup/Vulkan-Docs/wiki/Synchronization-Examples
    //
    // typedef struct VkMemoryBarrier2KHR {
    //     VkStructureType             sType;
    //     const void*                 pNext;
    //     VkPipelineStageFlags2KHR    srcStageMask;
    //     VkAccessFlags2KHR           srcAccessMask;
    //     VkPipelineStageFlags2KHR    dstStageMask;
    //     VkAccessFlags2KHR           dstAccessMask;
    // } VkMemoryBarrier2KHR;

    //VkMemoryBarrier2KHR memoryBarrier = {
    //    VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR,
    //    nullptr,
    //    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,

    //    VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,

    //};
    //Texture::sTransitionImageLayout(m_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
}
