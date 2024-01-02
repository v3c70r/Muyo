#include "ResourceBarrier.h"

#include <vulkan/vulkan_core.h>

#include <stdexcept>

namespace Muyo
{

ImageResourceBarrier::ImageResourceBarrier(VkImage image, VkImageLayout targetLayout, VkImageLayout sourceLayout, uint32_t nMipCount, uint32_t nLayerCount)
{
    m_imageBarrier.oldLayout = sourceLayout;
    m_imageBarrier.newLayout = targetLayout;

    m_imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    m_imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    m_imageBarrier.image = image;

    // deduce access masks from layouts
    // UNDEFINED -> DST
    if (sourceLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        targetLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        m_imageBarrier.srcAccessMask = 0;
        m_imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        m_sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        m_destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    // DST -> SHADER READ ONLY
    else if (sourceLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             targetLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        m_imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        m_imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        m_sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        m_destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    // UNDEFINED -> DEPTH_ATTACHMENT
    else if (sourceLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             targetLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        m_imageBarrier.srcAccessMask = 0;
        m_imageBarrier.dstAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        m_sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        m_destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }
    // UNDEFINED -> COLOR_ATTACHMENT
    else if (sourceLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             targetLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        m_imageBarrier.srcAccessMask = 0;
        m_imageBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        m_sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        m_destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    // UNDEFINED -> GENERAL
    else if (sourceLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             targetLayout == VK_IMAGE_LAYOUT_GENERAL)
    {
        m_imageBarrier.srcAccessMask = 0;
        m_imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_READ_BIT;

        m_sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        m_destinationStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    }
    else
    {
        throw std::invalid_argument("unsupported layout transition!");
    }

    // subresources
    if (targetLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        m_imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    else
    {
        m_imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    m_imageBarrier.subresourceRange.baseMipLevel = 0;
    m_imageBarrier.subresourceRange.levelCount = nMipCount;
    m_imageBarrier.subresourceRange.baseArrayLayer = 0;
    m_imageBarrier.subresourceRange.layerCount = nLayerCount;
}

void ImageResourceBarrier::AddToCommandBuffer(VkCommandBuffer cmdBuf)
{
    vkCmdPipelineBarrier(cmdBuf, m_sourceStage, m_destinationStage, 0, 0, nullptr, 0, nullptr, 1, &m_imageBarrier);
}

}  // namespace Muyo
