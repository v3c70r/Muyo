#pragma once
#include <vulkan/vulkan.h>

#include "Texture.h"
// Depth image
class DepthResource {
public:
    DepthResource(VkDevice device, VkPhysicalDevice physicalDevice,
                  VkCommandPool pool, VkQueue queue, uint32_t width,
                  uint32_t height)
        : mImage(VK_NULL_HANDLE), mMemory(VK_NULL_HANDLE), mView(VK_NULL_HANDLE)
    {
        mDevice = device;
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        Texture::createImage(device, physicalDevice, width, height, depthFormat,
                             VK_IMAGE_TILING_OPTIMAL,
                             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mImage,
                             mMemory);

        ///// Create the view
        VkImageViewCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = mImage;
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


        assert(vkCreateImageView(device, &createInfo, nullptr, &mView) ==
               VK_SUCCESS);

        //// transition depth layout
        Texture::sTransitionImageLayout(device, pool, queue, mImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    }

    ~DepthResource()
    {
        vkDestroyImageView(mDevice, mView, nullptr);
        vkDestroyImage(mDevice, mImage, nullptr);
        vkFreeMemory(mDevice, mMemory, nullptr);

    }
    
    VkImageView getView() const
    {
        return mView;
    }

private:
    VkImage mImage;
    VkDeviceMemory mMemory;
    VkImageView mView;
    VkDevice mDevice;   // cache device to destroy the memory
};
