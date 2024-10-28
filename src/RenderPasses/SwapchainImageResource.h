#pragma once
#include "RenderResource.h"

namespace Muyo
{
// We keep a wrapper around the swapchain image view to be able to track by the resource manager
// This resource doesn't allocate any images because the images are allocated by the swapchain
class SwapchainImageResource : public ImageResource
{
  public:
    SwapchainImageResource(VkImage image, VkFormat format, VkExtent2D extent)
    {
        m_image = image;
        // populate m_imageInfo
        m_imageInfo.extent.width  = extent.width;
        m_imageInfo.extent.height = extent.height;
        m_imageInfo.format        = format;

        m_imageViewInfo.image = image;
        m_imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        m_imageViewInfo.format   = format;

        // Swizzles
        m_imageViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        m_imageViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        m_imageViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        m_imageViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        // subresource
        m_imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        m_imageViewInfo.subresourceRange.baseMipLevel = 0;
        m_imageViewInfo.subresourceRange.levelCount   = 1;
        m_imageViewInfo.subresourceRange.baseArrayLayer = 0;
        m_imageViewInfo.subresourceRange.layerCount     = 1;

        CreateImageViewInternal();
    }
    virtual ~SwapchainImageResource() override
    {
        // Set m_image to VK_NULL_HANDLE to avoid being destroyed by the base class
        m_image = VK_NULL_HANDLE;
    }
};
}    // namespace Muyo
