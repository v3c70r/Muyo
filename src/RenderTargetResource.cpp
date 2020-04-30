#include "RenderTargetResource.h"
#include "VkRenderDevice.h"
#include "VkMemoryAllocator.h"

#include <cassert>

RenderTarget::RenderTarget(VkFormat format, VkImageUsageFlagBits usage,
                           uint32_t width, uint32_t height)
{
    VkImageAspectFlags aspectMask = 0;
    //VkImageLayout imageLayout;
    
    if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
    {
        aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        //imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
    {
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        //imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

	assert(aspectMask > 0);
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT;

    GetMemoryAllocator()->AllocateImage(&imageInfo, VMA_MEMORY_USAGE_GPU_ONLY,
                                        m_image, m_allocation);
    assert(m_image != VK_NULL_HANDLE && "Failed to allocate image");

    // Create Image View
    VkImageViewCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = m_image;
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = format;

    // Swizzles
    createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    // subresource
    createInfo.subresourceRange.aspectMask = aspectMask;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;
    assert(vkCreateImageView(GetRenderDevice()->GetDevice(), &createInfo,
                             nullptr, &m_view) == VK_SUCCESS);
}
RenderTarget::~RenderTarget()
{
    vkDestroyImageView(GetRenderDevice()->GetDevice(), m_view, nullptr);
    GetMemoryAllocator()->FreeImage(m_image, m_allocation);
}
