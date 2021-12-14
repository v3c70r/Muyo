#pragma once
#include <vulkan/vulkan.h>
#include <vector>

// Render context maintain an arry of command buffer to be submitted to GPU. 
// Render passes are command buffer agnostic so that it can record to any given command buffer

class RenderContext
{
private:
    std::vector<VkCommandBuffer> m_vCommandBuffers;
    std::vector<VkCommandBuffer> m_vPresentCommandBuffers;
};
