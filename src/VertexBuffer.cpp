#include "VertexBuffer.h"
#include <cassert>

MemoryBuffer::MemoryBuffer(bool stagedUpload)
{
    m_bufferUsageFlags = stagedUpload ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0;
    m_memoryUsage = stagedUpload ?  VMA_MEMORY_USAGE_GPU_ONLY : VMA_MEMORY_USAGE_CPU_TO_GPU;
}

MemoryBuffer::~MemoryBuffer()
{
    if (m_buffer != VK_NULL_HANDLE || m_allocation != VK_NULL_HANDLE)
    {
        GetMemoryAllocator()->FreeBuffer(m_buffer, m_allocation);
    }
}

void* MemoryBuffer::map()
{
    void* pMappedMemory;
    GetMemoryAllocator()->MapBuffer(m_allocation, &pMappedMemory);
    return pMappedMemory;
}

void MemoryBuffer::unmap() { GetMemoryAllocator()->UnmapBuffer(m_allocation); }

void MemoryBuffer::setData(void* data, size_t size)
{
    if (m_buffer != VK_NULL_HANDLE && size > m_nSize)
    {
        GetMemoryAllocator()->FreeBuffer(m_buffer, m_allocation);
    }
    else if (m_buffer == VK_NULL_HANDLE)
    {
        GetMemoryAllocator()->AllocateBuffer(size, m_bufferUsageFlags,
                                             m_memoryUsage, m_buffer,
                                             m_allocation, m_sBufferName.data());
    }
    m_nSize = size;

    if (m_bufferUsageFlags & VK_BUFFER_USAGE_TRANSFER_DST_BIT)
    {
        assert(data != nullptr && "Need to have data to upload");
        // Create staging buffer
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VmaAllocation stagingAllocation = VK_NULL_HANDLE;
        GetMemoryAllocator()->AllocateBuffer(
            size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU,
            stagingBuffer, stagingAllocation);

        void* pMappedMemory = nullptr;
        GetMemoryAllocator()->MapBuffer(stagingAllocation, &pMappedMemory);
        memcpy(pMappedMemory, data, size);
        GetMemoryAllocator()->UnmapBuffer(stagingAllocation);

        // Submit the copy immedietly
        GetRenderDevice()->executeImmediateCommand(
            [&](VkCommandBuffer commandBuffer) {
                VkBufferCopy copyRegion = {};
                copyRegion.size = size;
                vkCmdCopyBuffer(commandBuffer, stagingBuffer, m_buffer, 1,
                                &copyRegion);
            });
        GetMemoryAllocator()->FreeBuffer(stagingBuffer, stagingAllocation);
    }
    else
    {
        if (data != nullptr)
        {
            void* pMappedMemory = nullptr;
            GetMemoryAllocator()->MapBuffer(m_allocation, &pMappedMemory);
            memcpy(pMappedMemory, data, size);
            GetMemoryAllocator()->UnmapBuffer(m_allocation);
        }
    }
}
