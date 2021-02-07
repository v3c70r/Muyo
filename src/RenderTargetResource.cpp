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
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = numMips;
    imageInfo.arrayLayers = numLayers;
    imageInfo.format = format;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (numLayers > 1)
    {
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    GetMemoryAllocator()->AllocateImage(&imageInfo, VMA_MEMORY_USAGE_GPU_ONLY,
                                        m_image, m_allocation);
    assert(m_image != VK_NULL_HANDLE && "Failed to allocate image");

    // Create Image View
    VkImageViewCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = m_image;
    createInfo.viewType =
        numLayers == 1 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_CUBE;
    createInfo.format = format;

    // Swizzles
    createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    // subresource
    createInfo.subresourceRange.aspectMask = aspectMask;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = numMips;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = numLayers;
    assert(vkCreateImageView(GetRenderDevice()->GetDevice(), &createInfo,
                             nullptr, &m_view) == VK_SUCCESS);

    Texture::sTransitionImageLayout(m_image, VK_IMAGE_LAYOUT_UNDEFINED,
                                    imageLayout);
}
RenderTarget::~RenderTarget()
{
    vkDestroyImageView(GetRenderDevice()->GetDevice(), m_view, nullptr);
    GetMemoryAllocator()->FreeImage(m_image, m_allocation);
}
