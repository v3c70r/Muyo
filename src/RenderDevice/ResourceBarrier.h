#pragma once

#include <vulkan/vulkan.h>
namespace Muyo
{

class IResourceBarrier
{
public:
    virtual void AddToCommandBuffer(VkCommandBuffer cmdBuf) = 0;
};

class ImageResourceBarrier : public IResourceBarrier
{
public:
    ImageResourceBarrier(VkImage img, VkImageLayout targetLayout, VkImageLayout sourceLayout = VK_IMAGE_LAYOUT_UNDEFINED, uint32_t nMipCount = 1, uint32_t nLayerCount = 1);
    void AddToCommandBuffer(VkCommandBuffer cmdBuf) override;

protected:
    VkImageMemoryBarrier m_imageBarrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    VkPipelineStageFlags m_sourceStage = VK_PIPELINE_STAGE_NONE_KHR;
    VkPipelineStageFlags m_destinationStage = VK_PIPELINE_STAGE_NONE_KHR;
};

}  // namespace Muyo
