#include "RenderTargetResource.h"

#include <cassert>

#include "Texture.h"
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"

RenderTarget::RenderTarget(VkFormat format, VkImageUsageFlags usage,
                           uint32_t width, uint32_t height, uint32_t numMips, uint32_t numLayers)
{
    VkImageAspectFlags aspectMask = 0;
    VkImageLayout imageLayout=VK_IMAGE_LAYOUT_UNDEFINED;

    if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
    {
        aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
    {
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    assert(aspectMask > 0);
    m_imageInfo.imageType = VK_IMAGE_TYPE_2D;
    m_imageInfo.extent.width = width;
    m_imageInfo.extent.height = height;
    m_imageInfo.extent.depth = 1;
    m_imageInfo.mipLevels = numMips;
    m_imageInfo.arrayLayers = numLayers;
    m_imageInfo.format = format;
    m_imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    m_imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    m_imageInfo.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT;
    m_imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (numLayers > 1)
    {
        m_imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    CreateImageInternal(VMA_MEMORY_USAGE_GPU_ONLY);
    assert(m_image != VK_NULL_HANDLE && "Failed to allocate image");

    // Create Image View
    m_imageViewInfo.viewType = numLayers == 1 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_CUBE;
    // Swizzles
    m_imageViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    m_imageViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    m_imageViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    m_imageViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    // subresource
    m_imageViewInfo.subresourceRange.aspectMask = aspectMask;
    m_imageViewInfo.subresourceRange.baseMipLevel = 0;
    m_imageViewInfo.subresourceRange.levelCount = numMips;
    m_imageViewInfo.subresourceRange.baseArrayLayer = 0;
    m_imageViewInfo.subresourceRange.layerCount = numLayers;
    CreateImageViewInternal();

    TextureResource::sTransitionImageLayout(m_image, VK_IMAGE_LAYOUT_UNDEFINED,
                                    imageLayout);
}
