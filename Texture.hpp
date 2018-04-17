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
        mDevice(VK_NULL_HANDLE),
        mTextureImage(VK_NULL_HANDLE),
        mDeviceMemory(VK_NULL_HANDLE),
        mImageView(VK_NULL_HANDLE),
        mTextureSampler(VK_NULL_HANDLE)
    {
    }

    ~Texture()
    {
        if (mDevice != VK_NULL_HANDLE)
        {
            vkDestroySampler(mDevice, mTextureSampler, nullptr);
            vkDestroyImageView(mDevice, mImageView, nullptr);
            vkDestroyImage(mDevice, mTextureImage, nullptr);
            vkFreeMemory(mDevice, mDeviceMemory, nullptr);
        }
    }
    void LoadImage(const std::string path, const VkDevice &device,
                   const VkPhysicalDevice &physicalDevice,
                   const VkCommandPool &pool, const VkQueue &queue)
    {
        mDevice = device;

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

        mCreateImage(
            device, physicalDevice, width, height, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mTextureImage, mDeviceMemory);

        // UNDEFINED -> DST_OPTIMAL
        mTransitionImageLayout(
            device, pool, queue, mTextureImage, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // DST_OPTIMAL -> SHADER_READ_ONLY
        mTransitionImageLayout(device, pool, queue, mTextureImage,
                               VK_FORMAT_R8G8B8A8_UNORM,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        mInitImageView();
        mInitSampler();
    }

    VkImageView getImageView() const { return mImageView; }
    VkSampler getSamper() const { return mTextureSampler; }

    static VkDescriptorSetLayoutBinding getSamplerLayoutBinding()
    {
        VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
        samplerLayoutBinding.binding = 1;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        return samplerLayoutBinding;
    }
private:
    void mCreateImage(VkDevice device, VkPhysicalDevice physicalDevice,
                      uint32_t width, uint32_t height, VkFormat format,
                      VkImageTiling tiling, VkImageUsageFlags usage,
                      VkMemoryPropertyFlags properties, VkImage &image,
                      VkDeviceMemory &imageMemory)
    {
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
        imageInfo.tiling = tiling;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.flags = 0;
        assert(vkCreateImage(device, &imageInfo, nullptr, &image) ==
               VK_SUCCESS);

        // Allocate device memory for the image
        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device, mTextureImage, &memRequirements);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = Buffer::findMemoryType(
            physicalDevice, memRequirements.memoryTypeBits, properties);

        assert(vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) ==
               VK_SUCCESS);
        vkBindImageMemory(device, image, imageMemory, 0);
    }

    void mTransitionImageLayout(VkDevice device, VkCommandPool cmdPool,
                                VkQueue queue, VkImage image, VkFormat format,
                                VkImageLayout oldLayout,
                                VkImageLayout newLayout)
    {
        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;

        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
            newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                 newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else {
            throw std::invalid_argument("unsupported layout transition!");
        }

        VkCommandBuffer cmdBuf = beginSingleTimeCommands(device, cmdPool);

        vkCmdPipelineBarrier(cmdBuf, 
                sourceStage, /*srcStage*/ destinationStage,/*dstStage*/
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier
                );

        endSingleTimeCommands(cmdBuf, device, cmdPool, queue);
    }

    void mCopyBufferToImage(VkDevice device, VkCommandPool cmdPool,
                            VkQueue queue, VkBuffer buffer, VkImage image,
                            uint32_t width, uint32_t height){
        VkBufferImageCopy region = {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;

        // Subpart of the image
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;

        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};

        VkCommandBuffer cmdBuf = beginSingleTimeCommands(device, cmdPool);
        vkCmdCopyBufferToImage(cmdBuf, buffer, image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);
        endSingleTimeCommands(cmdBuf, device, cmdPool, queue);
    }
    void mInitImageView()
    {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = mTextureImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        assert(vkCreateImageView(mDevice, &viewInfo, nullptr, &mImageView) == VK_SUCCESS);

    }

    void mInitSampler()
    {
        VkSamplerCreateInfo samplerInfo = {};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;

        // Wrapping
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

        // Anistropic filter
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1;

        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        // Choose of [0, width] or [0, 1]
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        // Used for percentage-closer filter for shadow
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

        // mipmaps
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;

        assert(vkCreateSampler(mDevice, &samplerInfo, nullptr,
                               &mTextureSampler) == VK_SUCCESS);
    }

    VkDevice mDevice;
    VkImage mTextureImage;
    VkDeviceMemory mDeviceMemory;
    VkImageView mImageView;
    VkSampler mTextureSampler;
};
