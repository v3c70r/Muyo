#pragma once
#include "RenderResource.h"
#include "VkMemoryAllocator.h"

class AccelerationStructureBuffer : public IRenderResource
{
public:
    explicit AccelerationStructureBuffer(uint32_t size)
    {
        GetMemoryAllocator()->
            AllocateBuffer(size, BUFFER_USAGE, MEMORY_USAGE, m_buffer, m_allocation, "AccelerationStrucutre");
    }
    VkBuffer buffer() const { return m_buffer; }
private:
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    const VkBufferUsageFlags BUFFER_USAGE = 
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_EXT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const VmaMemoryUsage MEMORY_USAGE = VMA_MEMORY_USAGE_GPU_ONLY;
};
