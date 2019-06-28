// Utility functions
#pragma once
#include <vulkan/vulkan.h>

/////////////////////

// Command buffers
VkCommandBuffer beginSingleTimeCommands(const VkDevice &device,
                                        const VkCommandPool &commandPool);

void endSingleTimeCommands(VkCommandBuffer commandBuffer,
                           const VkDevice &device,
                           const VkCommandPool &commandPool,
                           const VkQueue &graphicsQueue);
