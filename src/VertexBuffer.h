#pragma once
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <array>
#include <cassert>
#include <glm/glm.hpp>
#include <iostream>
#include <memory>
#include <vector>

#include "VkMemoryAllocator.h"
#include "VkRenderDevice.h"

class VertexBuffer
{
public:
    VertexBuffer(size_t size = 0)
    {
        if (size != 0)
        {
            GetMemoryAllocator()->AllocateBuffer(
                size, BUFFER_USAGE, MEMORY_USAGE, m_buffer, m_allocation, "VertexBuffer");
        }
    }
    ~VertexBuffer()
    {
        if (m_buffer != VK_NULL_HANDLE || m_allocation != VK_NULL_HANDLE)
        {
            GetMemoryAllocator()->FreeBuffer(m_buffer, m_allocation);
        }
    }
    void setData(void* vertices, size_t size)
    {
        if (m_buffer != VK_NULL_HANDLE || m_allocation != VK_NULL_HANDLE)
        {
            GetMemoryAllocator()->FreeBuffer(m_buffer, m_allocation);
        }
        GetMemoryAllocator()->AllocateBuffer(size, BUFFER_USAGE, MEMORY_USAGE,
                                             m_buffer, m_allocation, "VertexBuffer");

        // Create staging buffer
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VmaAllocation stagingAllocation = VK_NULL_HANDLE;
        GetMemoryAllocator()->AllocateBuffer(
            size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU,
            stagingBuffer, stagingAllocation);

        void* pMappedMemory = nullptr;
        GetMemoryAllocator()->MapBuffer(stagingAllocation, &pMappedMemory);
        memcpy(pMappedMemory, vertices, size);
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
    VkBuffer buffer() const { return m_buffer; }

private:
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;

    const VkBufferUsageFlags BUFFER_USAGE =
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    const VmaMemoryUsage MEMORY_USAGE = VMA_MEMORY_USAGE_GPU_ONLY;
};

class IndexBuffer
{
public:
    IndexBuffer(size_t size = 0) {}
    ~IndexBuffer()
    {
        if (m_buffer != VK_NULL_HANDLE || m_allocation != VK_NULL_HANDLE)
        {
            GetMemoryAllocator()->FreeBuffer(m_buffer, m_allocation);
        }
    }

    void setData(void* indices, size_t size)
    {
        if (m_buffer != VK_NULL_HANDLE || m_allocation != VK_NULL_HANDLE)
        {
            GetMemoryAllocator()->FreeBuffer(m_buffer, m_allocation);
        }
        GetMemoryAllocator()->AllocateBuffer(size, BUFFER_USAGE, MEMORY_USAGE,
                                             m_buffer, m_allocation, "IndexBuffer");

        // Create staging buffer
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VmaAllocation stagingAllocation = VK_NULL_HANDLE;
        GetMemoryAllocator()->AllocateBuffer(
            size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU,
            stagingBuffer, stagingAllocation);

        void* pMappedMemory = nullptr;
        GetMemoryAllocator()->MapBuffer(stagingAllocation, &pMappedMemory);
        memcpy(pMappedMemory, indices, size);
        GetMemoryAllocator()->UnmapBuffer(stagingAllocation);

        GetRenderDevice()->executeImmediateCommand(
            [&](VkCommandBuffer commandBuffer) {
                VkBufferCopy copyRegion = {};
                copyRegion.size = size;
                vkCmdCopyBuffer(commandBuffer, stagingBuffer, m_buffer, 1,
                                &copyRegion);
            });
        GetMemoryAllocator()->FreeBuffer(stagingBuffer, stagingAllocation);
    }
    VkBuffer buffer() const { return m_buffer; }

private:
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;

    const VkBufferUsageFlags BUFFER_USAGE =
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    const VmaMemoryUsage MEMORY_USAGE = VMA_MEMORY_USAGE_GPU_ONLY;
};
