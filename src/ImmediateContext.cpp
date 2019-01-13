#include "ImmediateContext.h"
#include "vulkan/vulkan.h"

ImmediateContext::ImmediateContext(const VkDevice &device, const VkCommandPool& commandPool, const VkQueue& queue) : m_queue(queue)
{
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;
    m_commandBuffer = std::make_unique<VkCommandBuffer>();

    vkAllocateCommandBuffers(device, &allocInfo, m_commandBuffer.get());
}

void ImmediateContext::startRecording()
{
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(*(m_commandBuffer.get()), &beginInfo);
}
void ImmediateContext::endRecording() {
    vkEndCommandBuffer(m_commandBuffer);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
}
