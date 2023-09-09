#pragma once
#include <vulkan/vulkan.h>

#include <cstring>
#include <glm/glm.hpp>

#include "RenderResource.h"
#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"

namespace Muyo
{
template <class T>
class DrawCommandBuffer : public BufferResource
{
public:
    DrawCommandBuffer()
        : BufferResource(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU)
    {
        const size_t size = sizeof(T);
        GetMemoryAllocator()->AllocateBuffer(size, BUFFER_USAGE, MEMORY_USAGE, "Draw Command Buffer");
        m_nSize = size;
    }
    void SetData(const T& buffer)
    {
        BufferResource::SetData(&buffer, sizeof(T));
    }
};
}


