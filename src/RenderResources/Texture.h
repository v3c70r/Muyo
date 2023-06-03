#pragma once
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cassert>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "RenderResource.h"
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"

namespace Muyo
{
class TextureResource : public ImageResource
{
public:
    TextureResource();
    ~TextureResource();

    void LoadPixels(void* pixels, int width, int height);

    void LoadImage(const std::string path);

    VkSampler getSamper() const { return m_textureSampler; }

    void createImage(uint32_t width, uint32_t height, VkFormat format,
                     VkImageTiling tiling, VkImageUsageFlags usage,
                     VmaMemoryUsage memoryUsage)
    {
        // Create a vkimage
        m_imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        m_imageInfo.imageType = VK_IMAGE_TYPE_2D;
        m_imageInfo.extent.width = width;
        m_imageInfo.extent.height = height;
        m_imageInfo.extent.depth = 1;
        m_imageInfo.mipLevels = 1;
        m_imageInfo.arrayLayers = 1;
        m_imageInfo.format = format;
        m_imageInfo.tiling = tiling;
        m_imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        m_imageInfo.usage = usage;
        m_imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        m_imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        m_imageInfo.flags = 0;
        CreateImageInternal(memoryUsage);
    }

    // TODO: Move this barrier out of the function
    static void sTransitionImageLayout(VkImage image,
                                       VkImageLayout oldLayout,
                                       VkImageLayout newLayout,
                                       uint32_t nMipCount = 1,
                                       uint32_t nLayerCount = 1)
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
        if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        else
        {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        }
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = nMipCount;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = nLayerCount;

        // UNDEFINED -> DST
        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
            newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        // DST -> SHADER READ ONLY
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                 newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        // UNDEFINED -> DEPTH_ATTACHMENT
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
                 newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask =
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        }
        // UNDEFINED -> COLOR_ATTACHMENT
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
                 newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else
        {
            throw std::invalid_argument("unsupported layout transition!");
        }

        GetRenderDevice()->ExecuteImmediateCommand(
            [&](VkCommandBuffer commandBuffer)
            {
                vkCmdPipelineBarrier(commandBuffer, sourceStage,
                                     /*srcStage*/ destinationStage, /*dstStage*/
                                     0, 0, nullptr, 0, nullptr, 1, &barrier);
            });
    }

private:
    void mCopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width,
                            uint32_t height)
    {
        VkBufferImageCopy region = {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};

        GetRenderDevice()->ExecuteImmediateCommand(
            [&](VkCommandBuffer commandBuffer)
            {
                vkCmdCopyBufferToImage(commandBuffer, buffer, image,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                       &region);
            });
    }
    void mInitImageView()
    {
        m_imageViewInfo.image = m_image;
        m_imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        m_imageViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        m_imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        m_imageViewInfo.subresourceRange.baseMipLevel = 0;
        m_imageViewInfo.subresourceRange.levelCount = 1;
        m_imageViewInfo.subresourceRange.baseArrayLayer = 0;
        m_imageViewInfo.subresourceRange.layerCount = 1;
        CreateImageViewInternal();
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

        assert(vkCreateSampler(GetRenderDevice()->GetDevice(), &samplerInfo,
                               nullptr, &m_textureSampler) == VK_SUCCESS);
    }

    VkSampler m_textureSampler;
};

class TextureResourceManager
{
public:
    const TextureResource* CreateAndLoadOrGetTexture(const std::string& name, const std::string& path)
    {
        if (m_mTextureIndices.find(name) == m_mTextureIndices.end())
        {
            m_mTextureIndices[name] = (uint32_t)m_vpTextures.size();
            m_vpTextures.emplace_back(std::make_unique<TextureResource>());
            m_vpTextures.back()->LoadImage(path);
            m_vpTextures.back()->SetDebugName(name);
            return m_vpTextures.back().get();
        }
        else
        {
            return m_vpTextures[m_mTextureIndices[name]].get();
        }
    }
    uint32_t GetTextureIndex(const std::string& name) const
    {
        return m_mTextureIndices.at(name);
    }
    const std::vector<std::unique_ptr<TextureResource>>& GetTextures() const { return m_vpTextures; }
    void Destroy()
    {
        m_vpTextures.clear();
        m_mTextureIndices.clear();
    }

private:
    std::vector<std::unique_ptr<TextureResource>> m_vpTextures;
    std::unordered_map<std::string, uint32_t> m_mTextureIndices;
};
TextureResourceManager* GetTextureResourceManager();
}  // namespace Muyo
