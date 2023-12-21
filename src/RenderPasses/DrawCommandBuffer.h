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
    DrawCommandBuffer(const T* drawCommands, uint32_t drawCommandCount)
        : BufferResource(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU), m_nDrawCommandCount(drawCommandCount)
    {
        m_nSize = sizeof(T) * drawCommandCount;
        GetMemoryAllocator()->AllocateBuffer(m_nSize, BUFFER_USAGE, MEMORY_USAGE, m_buffer, m_allocation, "DrawCommandBuffer");
        SetData((void*)drawCommands, m_nSize);
    }

    uint32_t GetStride() const { return sizeof(T); }

    uint32_t GetDrawCommandCount() const {return m_nDrawCommandCount;}

private:
    uint32_t m_nDrawCommandCount = 0;
};
}


