#pragma once
#include <vulkan/vulkan.h>

class IResourceBarrier
{
public:
    virtual void AddBarrier(VkCommandBuffer cmdBuf) = 0;
};

class ImageResourceBarrier : public IResourceBarrier
{
public:
    ImageResourceBarrier(VkImage img, VkImageLayout targetLayout, VkImageLayout sourceLayout = VK_IMAGE_LAYOUT_UNDEFINED, uint32_t nMipCount = 1, uint32_t nLayerCount = 1);
    void AddBarrier(VkCommandBuffer cmdBuf) override;

protected:
    VkImageMemoryBarrier m_imageBarrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    VkPipelineStageFlags m_sourceStage = VK_NULL_HANDLE;
    VkPipelineStageFlags m_destinationStage = VK_NULL_HANDLE;
};

