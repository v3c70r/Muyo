#pragma once
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <array>
#include <cassert>
#include <glm/glm.hpp>
#include <iostream>
#include <memory>
#include <vector>

#include "Buffer.h"
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
                size, BUFFER_USAGE, MEMORY_USAGE, m_buffer, m_allocation);
        }
    }
    ~VertexBuffer()
    {
        if (m_buffer != VK_NULL_HANDLE || m_allocation != VK_NULL_HANDLE)
        {
            GetMemoryAllocator()->FreeBuffer(m_buffer, m_allocation);
        }
    }
    void setData(void* vertices, size_t size, VkCommandPool commandPool,
                 VkQueue queue)
    {
        if (m_buffer != VK_NULL_HANDLE || m_allocation != VK_NULL_HANDLE)
        {
            GetMemoryAllocator()->FreeBuffer(m_buffer, m_allocation);
        }
        GetMemoryAllocator()->AllocateBuffer(size, BUFFER_USAGE, MEMORY_USAGE,
                                             m_buffer, m_allocation);

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

        // Allocate command buffer
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(GetRenderDevice()->GetDevice(), &allocInfo,
                                 &commandBuffer);

        // record command buffer
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        VkBufferCopy copyRegion = {};
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, stagingBuffer, m_buffer, 1, &copyRegion);
        vkEndCommandBuffer(commandBuffer);

        // execute it right away
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);  // wait for it to finish

        vkFreeCommandBuffers(GetRenderDevice()->GetDevice(), commandPool, 1,
                             &commandBuffer);
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

    void setData(void* indices, size_t size, VkCommandPool commandPool,
                 VkQueue queue)
    {
        if (m_buffer != VK_NULL_HANDLE || m_allocation != VK_NULL_HANDLE)
        {
            GetMemoryAllocator()->FreeBuffer(m_buffer, m_allocation);
        }
        GetMemoryAllocator()->AllocateBuffer(size, BUFFER_USAGE, MEMORY_USAGE,
                                             m_buffer, m_allocation);

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

        // Allocate command buffer
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(GetRenderDevice()->GetDevice(), &allocInfo,
                                 &commandBuffer);

        // record command buffer
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        VkBufferCopy copyRegion = {};
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, stagingBuffer, m_buffer, 1, &copyRegion);
        vkEndCommandBuffer(commandBuffer);

        // execute it right away
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);  // wait for it to finish

        vkFreeCommandBuffers(GetRenderDevice()->GetDevice(), commandPool, 1,
                             &commandBuffer);
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
