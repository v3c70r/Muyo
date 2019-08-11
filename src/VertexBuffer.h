#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <cassert>
#include <glm/glm.hpp>
#include <iostream>
#include <vector>
#include <vk_mem_alloc.h>

#include "Buffer.h"

/*
VmaAllocatorCreateInfo allocatorInfo = {};
allocatorInfo.physicalDevice = physicalDevice;
allocatorInfo.device = device;
VmaAllocator allocator;
vmaCreateAllocator(&allocatorInfo, &allocator);
*/

class VertexBuffer {
public:
    VertexBuffer(const VkDevice& device, const VkPhysicalDevice& physicalDevice,
                 size_t size = 0)
    {
        m_pBuffer = std::make_unique<Buffer>(device, physicalDevice, size,
                                     VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }
    void setData(void* vertices, size_t size, VkCommandPool commandPool,
                 VkQueue queue)
    {
        // Recreate buffer if size has been changed
        if (m_pBuffer->size() != alignUp(size, m_pBuffer->alignment()))
        {
            // Recreate buffer
            VkDevice device = m_pBuffer->device();
            VkPhysicalDevice physicalDevice = m_pBuffer->physicalDevice();

            m_pBuffer = std::make_unique<Buffer>(device, physicalDevice, size,
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        }


        Buffer stagingBuffer(m_pBuffer->device(), m_pBuffer->physicalDevice(),
                             size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        stagingBuffer.setData(vertices);

        // Allocate command buffer
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(m_pBuffer->device(), &allocInfo, &commandBuffer);

        // record command buffer
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        VkBufferCopy copyRegion = {};
        copyRegion.size = m_pBuffer->size();
        vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer(), m_pBuffer->buffer(),
                        1, &copyRegion);
        vkEndCommandBuffer(commandBuffer);

        // execute it right away
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);  // wait for it to finish

        vkFreeCommandBuffers(m_pBuffer->device(), commandPool, 1, &commandBuffer);

        // stagingBuffer will be automatically destroyed when out of this scope
    }
    VkBuffer buffer() const { return m_pBuffer->buffer(); }
private:
    std::unique_ptr<Buffer> m_pBuffer;
};

class IndexBuffer
{
public:
    IndexBuffer(const VkDevice& device, const VkPhysicalDevice& physicalDevice,
                size_t size = 0)
    {
        m_pBuffer = std::make_unique<Buffer>(
            device, physicalDevice, size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }

    void setData(void* indices, size_t size, VkCommandPool commandPool, VkQueue queue)
    {

        if (m_pBuffer->size() != alignUp(size, m_pBuffer->alignment()))
        {
            // Recreate buffer
            VkDevice device = m_pBuffer->device();
            VkPhysicalDevice physicalDevice = m_pBuffer->physicalDevice();
            m_pBuffer = std::make_unique<Buffer>(device, physicalDevice, size,
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        }

        Buffer stagingBuffer(m_pBuffer->device(), m_pBuffer->physicalDevice(),
                             m_pBuffer->size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        stagingBuffer.setData(indices); 

        // Allocate command buffer
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(m_pBuffer->device(), &allocInfo, &commandBuffer);

        // record command buffer
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        VkBufferCopy copyRegion = {};
        copyRegion.size = m_pBuffer->size();
        vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer(), m_pBuffer->buffer(),
                        1, &copyRegion);
        vkEndCommandBuffer(commandBuffer);

        // execute it right away
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);  // wait for it to finish

        vkFreeCommandBuffers(m_pBuffer->device(), commandPool, 1, &commandBuffer);

        // stagingBuffer will be automatically destroyed when out of this scope
 
    }
    VkBuffer buffer() const { return m_pBuffer->buffer(); }

private:
    std::unique_ptr<Buffer> m_pBuffer;
};
