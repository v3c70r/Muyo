#pragma once
#include <vulkan/vulkan.h>

#include "Texture.h"
// Depth image
class DepthResource {
public:
    DepthResource(uint32_t width, uint32_t height)
    {
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        // Create a vkimage
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_D32_SFLOAT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.flags = 0;

        GetMemoryAllocator()->AllocateImage(
            &imageInfo, VMA_MEMORY_USAGE_GPU_ONLY, m_image, m_allocation);
        assert(m_image != VK_NULL_HANDLE && "Failed to allocate image for DS");

        ///// Create the view
        VkImageViewCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_image;
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = depthFormat;

        // Swizzles

        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        // subresource
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        assert(vkCreateImageView(GetRenderDevice()->GetDevice(), &createInfo,
                                 nullptr, &m_view) == VK_SUCCESS);

        //// transition depth layout
        Texture::sTransitionImageLayout(
            m_image, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    }

    ~DepthResource()
    {
        vkDestroyImageView(GetRenderDevice()->GetDevice(), m_view, nullptr);
        GetMemoryAllocator()->FreeImage(m_image, m_allocation);

    }
    
    VkImageView getView() const
    {
        return m_view;
    }

private:
    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
};
