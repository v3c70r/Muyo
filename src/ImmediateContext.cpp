#include "ImmediateContext.h"
#include "vulkan/vulkan.h"

ImmediateContext::ImmediateContext(VkDevice &device, VkCommandPool& commandPool, VkQueue& queue) : m_device(device), m_pool(commandPool), m_queue(queue)
{
    initialize(1, &device, &commandPool);
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;
    m_commandBuffer = std::make_unique<VkCommandBuffer>();

    vkAllocateCommandBuffers(device, &allocInfo, m_commandBuffer.get());
}

void ImmediateContext::initialize(size_t numBuffers, VkDevice* pDevice,
                                  VkCommandPool* pPool)

{
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = *pPool;
    allocInfo.commandBufferCount = 1;
    m_commandBuffer = std::make_unique<VkCommandBuffer>();

    vkAllocateCommandBuffers(*pDevice, &allocInfo, m_commandBuffer.get());
}

void ImmediateContext::finalize()
{
    vkFreeCommandBuffers(m_device, m_pool, 1, m_commandBuffer.get());
}

void ImmediateContext::startRecording()
{
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(*(m_commandBuffer.get()), &beginInfo);
}
void ImmediateContext::endRecording() {
    vkEndCommandBuffer(*(m_commandBuffer).get());

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = m_commandBuffer.get();

    vkQueueSubmit(m_queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_queue);
}

bool ImmediateContext::isRecording() const
{
    return true;
}

VkCommandBuffer& ImmediateContext::getCommandBuffer()
{
    return *m_commandBuffer.get();
}

