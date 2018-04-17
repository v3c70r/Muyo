#include <vulkan/vulkan.h>
#include <array>
#include <cassert>
#include <glm/glm.hpp>
#include <iostream>
#include <vector>
#include "Buffer.h"

struct Vertex {
    glm::vec3 pos;
    glm::vec3 textureCoord;
    static VkVertexInputBindingDescription getBindingDescription()
    {
        VkVertexInputBindingDescription desc = {};
        desc.binding = 0;
        desc.stride = sizeof(Vertex);
        desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return desc;
    }

    static std::array<VkVertexInputAttributeDescription, 2>
    getAttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 2> attribDesc = {};
        attribDesc[0].location = 0;
        attribDesc[0].binding = 0;
        attribDesc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribDesc[0].offset = offsetof(Vertex, pos);

        attribDesc[1].location = 1;
        attribDesc[1].binding = 0;
        attribDesc[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribDesc[1].offset = offsetof(Vertex, textureCoord);
        return attribDesc;
    }
};

class VertexBuffer {
public:
    VertexBuffer(const VkDevice& device, const VkPhysicalDevice& physicalDevice,
                 size_t size)
        : mBuffer(device, physicalDevice, size,
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    {
    }
    VertexBuffer(const VkDevice& device, const VkPhysicalDevice& physicalDevice,
                 const std::vector<Vertex>& vertices, VkCommandPool commandPool,
                 VkQueue queue)
        : VertexBuffer(device, physicalDevice, sizeof(Vertex) * vertices.size())
    {
        setData(vertices, commandPool, queue);
    }
    void setData(const std::vector<Vertex>& vertices, VkCommandPool commandPool,
                 VkQueue queue)
    {
        Buffer stagingBuffer(mBuffer.device(), mBuffer.physicalDevice(),
                             mBuffer.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        stagingBuffer.setData((void*)vertices.data());

        // Allocate command buffer
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(mBuffer.device(), &allocInfo, &commandBuffer);

        // record command buffer
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        VkBufferCopy copyRegion = {};
        copyRegion.size = mBuffer.size();
        vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer(), mBuffer.buffer(),
                        1, &copyRegion);
        vkEndCommandBuffer(commandBuffer);

        // execute it right away
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);  // wait for it to finish

        vkFreeCommandBuffers(mBuffer.device(), commandPool, 1, &commandBuffer);

        // stagingBuffer will be automatically destroyed when out of this scope
    }
    VkBuffer buffer() const { return mBuffer.buffer(); }
private:
    Buffer mBuffer;
};

class IndexBuffer
{
public:
    IndexBuffer(const VkDevice& device, const VkPhysicalDevice& physicalDevice,
                size_t size)
        : mBuffer(device, physicalDevice, size,
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    {
    }

    IndexBuffer(const VkDevice& device, const VkPhysicalDevice& physicalDevice,
                 const std::vector<uint16_t>& indices, VkCommandPool commandPool,
                 VkQueue queue)
        : IndexBuffer(device, physicalDevice, sizeof(uint16_t) * indices.size())
    {
        setData(indices, commandPool, queue);
    }
 
    void setData(const std::vector<uint16_t>& indices, VkCommandPool commandPool, VkQueue queue)
    {
        Buffer stagingBuffer(mBuffer.device(), mBuffer.physicalDevice(),
                             mBuffer.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        stagingBuffer.setData((void*)indices.data()); 

        // Allocate command buffer
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(mBuffer.device(), &allocInfo, &commandBuffer);

        // record command buffer
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        VkBufferCopy copyRegion = {};
        copyRegion.size = mBuffer.size();
        vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer(), mBuffer.buffer(),
                        1, &copyRegion);
        vkEndCommandBuffer(commandBuffer);

        // execute it right away
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);  // wait for it to finish

        vkFreeCommandBuffers(mBuffer.device(), commandPool, 1, &commandBuffer);

        // stagingBuffer will be automatically destroyed when out of this scope
 
    }
    VkBuffer buffer() const { return mBuffer.buffer(); }

private:
    Buffer mBuffer;
};
