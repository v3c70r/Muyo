#pragma once
#include <vulkan/vulkan.h>

class RenderContext
{
    uint32_t m_uFrameIdxToPresent;
    VkViewport m_viewPort;
};
