#pragma once
#include <vulkan/vulkan.h>
#include <string>
#include "Buffer.h"
#include "Util.h"
#define STB_IMAGE_IMPLEMENTATION
#include "thirdparty/stb_image.h"

class Texture
{
public:
    Texture():
        mSize({0, 0})
    {
    }
    void LoadImage(const std::string path, const VkDevice &device,
                   const VkPhysicalDevice &physicalDevice,
                   const VkCommandPool &pool, const VkQueue &queue)
    {
        int width, height, channels;
        stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        assert(pixels);
        assert(channels == 4);
        // Upload pixels to staging buffer
        Buffer stagingBuffer(device, physicalDevice, width * height * channels,
                             VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        stbi_image_free(pixels);

        // Create a vkimage
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage =
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.flags = 0;
        assert(vkCreateImage(device, &imageInfo, nullptr, &mTextureImage) ==
               VK_SUCCESS);

        // Allocate device memory for the image
        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device, mTextureImage, &memRequirements);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = Buffer::findMemoryType(physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        assert(vkAllocateMemory(device, &allocInfo, nullptr, &mDeviceMemory) ==
               VK_SUCCESS);
        vkBindImageMemory(device, mTextureImage, mDeviceMemory, 0);

        // Prepare the image buffer to recieve the data
        VkCommandBuffer cmdBuffer = beginSingleTimeCommands(device, pool);
        endSingleTimeCommands(cmdBuffer, device, pool, queue);

        // Copy staging buffer to image memory
        cmdBuffer = beginSingleTimeCommands(device, pool);
        VkBufferCopy copyRegion = {};
        copyRegion.size = memRequirements.size;
        vkCmdCopyBuffer(cmdBuffer, stagingBuffer, 
        endSingleTimeCommands(cmdBuffer, device, pool, queue);
    }
private:
    VkImage mTextureImage;
    VkDeviceMemory mDeviceMemory;
    VkExtent2D mSize;
};
