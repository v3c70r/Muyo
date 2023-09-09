#pragma once
#include <vulkan/vulkan.h>

#include <cstring>
#include <glm/glm.hpp>

#include "RenderResource.h"
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"

namespace Muyo
{
class DrawCommandBuffer : public BufferResource
{
public:
    DrawCommandBuffer()
        : BufferResource(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU)
    {
    }
};
}


